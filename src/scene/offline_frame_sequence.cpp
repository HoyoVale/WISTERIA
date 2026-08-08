#include "wisteria/common/pch.hpp"
#include "wisteria/scene/offline_frame_sequence.hpp"

#include "wisteria/common/png_encoder.hpp"
#include "wisteria/mmd/mmd_determinism.hpp"
#include "wisteria/mmd/mmd_presentation.hpp"
#include "wisteria/runtime/checkpoint_serialization.hpp"
#include "wisteria/scene/scene.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iomanip>
#include <iterator>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace wisteria
{
namespace
{
std::string FrameName(MotionFrameIndex frame, std::string_view extension)
{
    std::ostringstream stream;
    stream << std::setw(8) << std::setfill('0') << frame << extension;
    return stream.str();
}

std::uint64_t HashBytes(std::span<const std::uint8_t> bytes)
{
    return Fnv1a64(bytes.data(), bytes.size());
}

std::uint64_t HashString(std::string_view text)
{
    return Fnv1a64(
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()
    );
}

inline constexpr MotionFrameIndex kMaxSequenceFrame = 1U << 24;

bool FlushDurably(FILE* file)
{
    if (std::fflush(file) != 0)
        return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

// Atomic replace of `temporary` onto `finalPath`; durable on Windows via
// MOVEFILE_WRITE_THROUGH, atomic rename on POSIX plus a directory fsync.
bool AtomicReplace(
    const std::filesystem::path& temporary,
    const std::filesystem::path& finalPath
)
{
#if defined(_WIN32)
    return MoveFileExW(
        temporary.wstring().c_str(),
        finalPath.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, finalPath, error);
    if (error)
        return false;
    const int directory = ::open(finalPath.parent_path().c_str(), O_RDONLY);
    if (directory >= 0)
    {
        const bool synced = fsync(directory) == 0;
        ::close(directory);
        if (!synced)
            return false;
    }
    return true;
#endif
}

// Appends one newline-terminated record and makes it durable.
bool AppendDurable(
    const std::filesystem::path& path,
    const std::string& record
)
{
    FILE* file = std::fopen(path.string().c_str(), "ab");
    if (file == nullptr)
        return false;
    const bool ok = std::fwrite(
        record.data(),
        1,
        record.size(),
        file
    ) == record.size() && std::fputc('\n', file) != EOF;
    const bool durable = ok && FlushDurably(file);
    const bool closed = std::fclose(file) == 0;
    return durable && closed;
}

// Removes a partial trailing JSONL record after a crash by truncating the
// file IN PLACE after the last complete '\n'. Never rewrites the file
// (a rewrite would create a zero-length crash window that could lose the
// entire committed history). Returns false when truncation/sync fails.
bool TruncateJsonlTail(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return true;  // missing manifest is handled by callers
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    input.close();
    std::size_t lastNewline = std::string::npos;
    for (std::size_t index = bytes.size(); index > 0U; --index)
    {
        if (bytes[index - 1U] == '\n')
        {
            lastNewline = index - 1U;
            break;
        }
    }
    const std::size_t keep =
        lastNewline == std::string::npos ? 0U : lastNewline + 1U;
    if (keep == bytes.size())
        return true;
    FILE* file = std::fopen(path.string().c_str(), "rb+");
    if (file == nullptr)
        return false;
    bool truncated = false;
#if defined(_WIN32)
    truncated = _chsize_s(
        _fileno(file),
        static_cast<__int64>(keep)
    ) == 0 && _commit(_fileno(file)) == 0;
#else
    truncated = ftruncate(fileno(file), static_cast<off_t>(keep)) == 0 &&
        fsync(fileno(file)) == 0;
#endif
    const bool closed = std::fclose(file) == 0;
    return truncated && closed;
}
}

OfflineFrameSequence::OfflineFrameSequence(
    Scene& scene,
    Renderer& renderer,
    MmdRuntimeModel& runtime,
    ModelInstance& modelInstance,
    OfflineFrameSequenceConfig config
)
    : scene(&scene),
      renderer(&renderer),
      runtime(&runtime),
      modelInstance(&modelInstance),
      config(std::move(config))
{
    if (this->config.outputDirectory.empty())
        throw std::invalid_argument("Offline sequence output directory is empty");
    if (modelInstance.TryGetMmdRuntime() != &runtime)
    {
        throw std::invalid_argument(
            "ModelInstance runtime does not match the sequence driver"
        );
    }
    if (!this->config.writePng && !this->config.writeRaw)
    {
        throw std::invalid_argument(
            "At least one persistent output format must be enabled"
        );
    }
}

std::filesystem::path OfflineFrameSequence::ManifestPath() const
{
    return this->config.outputDirectory / "manifest.jsonl";
}

std::filesystem::path OfflineFrameSequence::FramePath(
    MotionFrameIndex frame,
    std::string_view extension
) const
{
    return this->config.outputDirectory /
        FrameName(frame, extension);
}

std::filesystem::path OfflineFrameSequence::CheckpointPath(
    std::string_view slot
) const
{
    return this->config.outputDirectory /
        ("checkpoint-" + std::string(slot) + ".bin");
}

std::string OfflineFrameSequence::SessionIdentity() const
{
    std::vector<std::uint8_t> payload;
    const auto fold = [&payload](const void* data, std::size_t size)
    {
        const std::uint8_t* bytes =
            static_cast<const std::uint8_t*>(data);
        payload.insert(payload.end(), bytes, bytes + size);
    };
    const auto foldString = [&payload, &fold](std::string_view text)
    {
        const std::uint64_t length = text.size();
        fold(&length, sizeof(length));
        payload.insert(
            payload.end(),
            reinterpret_cast<const std::uint8_t*>(text.data()),
            reinterpret_cast<const std::uint8_t*>(text.data()) + text.size()
        );
    };
    const std::uint64_t build = CurrentBuildCompatibilityId();
    fold(&build, sizeof(build));
    fold(&this->config.renderRequest.width, sizeof(std::uint32_t));
    fold(&this->config.renderRequest.height, sizeof(std::uint32_t));
    const CameraParam& camera = this->config.renderRequest.camera.GetParam();
    fold(&camera.Position, sizeof(camera.Position));
    fold(&camera.Target, sizeof(camera.Target));
    fold(&camera.Up, sizeof(camera.Up));
    fold(
        &camera.VerticalFovDegrees,
        sizeof(camera.VerticalFovDegrees)
    );
    fold(&camera.NearClip, sizeof(camera.NearClip));
    fold(&camera.FarClip, sizeof(camera.FarClip));
    for (glm::length_t column = 0; column < 4; ++column)
    {
        fold(
            &this->config.renderRequest.projection[column],
            sizeof(glm::vec4)
        );
    }
    fold(&this->config.renderRequest.clearColor, sizeof(glm::vec4));
    foldString(this->config.scenePresentationIdentity);
    std::ostringstream stream;
    stream << std::hex << Fnv1a64(payload.data(), payload.size());
    return stream.str();
}

void OfflineFrameSequence::Fail(const std::string& message)
{
    this->failed = true;
    throw std::runtime_error(
        "OfflineFrameSequence failed: " + message
    );
}

bool OfflineFrameSequence::Failed() const noexcept
{
    return this->failed;
}

std::optional<MotionFrameIndex> OfflineFrameSequence::LastCommittedFrame()
    const noexcept
{
    return this->lastCommitted;
}

void OfflineFrameSequence::StepTo(MotionFrameIndex target)
{
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(this->runtime);
    if (stepper == nullptr)
    {
        this->Fail("runtime does not implement deterministic stepping");
        return;
    }
    if (stepper->PrepareFrameZero({}) != TimelineStatus::Ok)
    {
        this->Fail("PrepareFrameZero failed");
        return;
    }
    for (MotionFrameIndex frame = 1U; frame <= target; ++frame)
    {
        if (stepper->StepMotionFrameExact(frame, {}) != TimelineStatus::Ok)
        {
            this->Fail("sequential pre-roll failed at frame " +
                std::to_string(frame));
            return;
        }
    }
}

void OfflineFrameSequence::RenderRange(
    MotionFrameIndex start,
    MotionFrameIndex end
)
{
    try
    {
        if (this->failed)
            this->Fail("sequence is already failed");
        if (start > end || end > kMaxSequenceFrame)
        {
            this->Fail("sequence frame range is outside the supported domain");
            return;
        }
        // Deterministic sequences require non-looping playback.
        this->runtime->SetMotionLooping(false);
        std::error_code directoryError;
        std::filesystem::create_directories(
            this->config.outputDirectory,
            directoryError
        );
        if (directoryError)
        {
            this->Fail("cannot create output directory: " +
                directoryError.message());
            return;
        }
        // Initialize the A/B alternation from an existing manifest so a
        // non-sequential RenderRange never overwrites the committed slot.
        if (this->lastCommittedCheckpointSlot.empty())
        {
            const std::optional<FrameRecord> existing =
                this->ReadLastCommittedRecord();
            if (existing.has_value())
            {
                this->lastCommitted = existing->frame;
                this->lastCommittedCheckpointSlot =
                    existing->checkpointSlot;
            }
        }

        this->StepTo(start);
        for (MotionFrameIndex frame = start; frame <= end; ++frame)
        {
            this->CommitFrame(frame);
            if (frame != end)
            {
                auto* stepper =
                    dynamic_cast<IDeterministicFrameStepper*>(
                        this->runtime
                    );
                if (stepper->StepMotionFrameExact(frame + 1U, {}) !=
                    TimelineStatus::Ok)
                {
                    this->Fail("exact step failed at frame " +
                        std::to_string(frame + 1U));
                    return;
                }
            }
        }
    }
    catch (...)
    {
        this->failed = true;
        throw;
    }
}

void OfflineFrameSequence::Resume(MotionFrameIndex end)
{
    try
    {
        if (this->failed)
            this->Fail("sequence is already failed");
        if (end > kMaxSequenceFrame)
        {
            this->Fail("resume target is outside the supported domain");
            return;
        }
        // Deterministic sequences require non-looping playback.
        this->runtime->SetMotionLooping(false);
        if (!std::filesystem::is_regular_file(this->ManifestPath()))
        {
            this->Fail("no manifest to resume from");
            return;
        }
        const std::optional<FrameRecord> record =
            this->ReadLastCommittedRecord();
        if (!record.has_value())
        {
            this->Fail("no committed frame record to resume from");
            return;
        }

        // Always validate and restore the last committed checkpoint, even
        // when no new frame is requested; this also proves the directory is
        // resumable and refreshes LastCommittedFrame().
        const std::filesystem::path checkpointPath =
            this->CheckpointPath(record->checkpointSlot);
        std::ifstream checkpointStream(checkpointPath, std::ios::binary);
        if (!checkpointStream)
        {
            this->Fail("persisted checkpoint is missing for frame " +
                std::to_string(record->frame));
            return;
        }
        const std::vector<std::uint8_t> wire{
            std::istreambuf_iterator<char>(checkpointStream),
            std::istreambuf_iterator<char>()
        };
        if (HashBytes(wire) != record->checkpointWireHash)
        {
            this->Fail("checkpoint wire hash mismatch");
            return;
        }
        FrameCheckpoint checkpoint;
        if (DeserializeCheckpoint(
                wire.data(),
                wire.size(),
                {},
                checkpoint
            ) != TimelineStatus::Ok)
        {
            this->Fail("checkpoint wire deserialize failed");
            return;
        }
        if (checkpoint.frame != record->frame)
        {
            this->Fail("checkpoint frame does not match committed record");
            return;
        }
        if (this->runtime->RestoreCheckpoint(checkpoint) !=
            TimelineStatus::Ok)
        {
            this->Fail("checkpoint restore failed");
            return;
        }
        this->lastCommitted = record->frame;
        this->lastCommittedCheckpointSlot = record->checkpointSlot;
        if (record->frame >= end)
            return;

        auto* stepper =
            dynamic_cast<IDeterministicFrameStepper*>(this->runtime);
        for (MotionFrameIndex frame = record->frame + 1U;
             frame <= end;
             ++frame)
        {
            if (stepper->StepMotionFrameExact(frame, {}) !=
                TimelineStatus::Ok)
            {
                this->Fail("resume step failed at frame " +
                    std::to_string(frame));
                return;
            }
            this->CommitFrame(frame);
        }
    }
    catch (...)
    {
        this->failed = true;
        throw;
    }
}

void OfflineFrameSequence::ApplyPresentation(
    MotionFrameIndex frame,
    OfflineRenderRequest& request
) const
{
    // Same-frame camera sample decides whether the projection must be
    // rebuilt from the applied FOV (perspective) or kept at fallback.
    const std::optional<CameraTrackSample> cameraSample =
        this->runtime->SampleCameraMotion(static_cast<float>(frame));
    const bool cameraApplied = ApplyMmdCameraFrame(
        *this->runtime,
        static_cast<float>(frame),
        request.camera,
        this->config.renderRequest.camera.GetParam()
    );
    // Only an actually applied perspective camera sample may rebuild the
    // projection; a host-provided custom projection stays untouched when no
    // camera track exists.
    if (cameraApplied &&
        cameraSample.has_value() &&
        cameraSample->perspective.value_or(true))
    {
        const float aspect =
            request.width > 0U && request.height > 0U
            ? static_cast<float>(request.width) /
                static_cast<float>(request.height)
            : 1.0f;
        request.projection = request.camera.GetProjection(aspect);
    }
    if (!this->scene->DirectionalLights().empty())
    {
        DirectionalLight& light =
            *this->scene->DirectionalLights().front();
        const DirectionalLightData fallback{
            light.Direction(),
            light.Color(),
            light.Intensity()
        };
        ApplyMmdLightFrame(
            *this->runtime,
            static_cast<float>(frame),
            light,
            fallback
        );
    }
}

void OfflineFrameSequence::WriteArtifactAtomic(
    const std::filesystem::path& finalPath,
    std::span<const std::uint8_t> bytes
)
{
    const std::filesystem::path temporary =
        finalPath.string() + ".tmp";
    FILE* file = std::fopen(temporary.string().c_str(), "wb");
    if (file == nullptr)
    {
        this->Fail("cannot open temporary artifact: " +
            temporary.string());
        return;
    }
    const bool written = std::fwrite(
        bytes.data(),
        1,
        bytes.size(),
        file
    ) == bytes.size();
    const bool durable = written && FlushDurably(file);
    const bool closed = std::fclose(file) == 0;
    if (!written || !durable || !closed)
    {
        this->Fail("cannot write temporary artifact: " +
            temporary.string());
        return;
    }
    if (!AtomicReplace(temporary, finalPath))
    {
        this->Fail("cannot atomically commit artifact: " +
            finalPath.string());
    }
}

std::uint64_t OfflineFrameSequence::FileHash(
    const std::filesystem::path& path
) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return 0U;
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    };
    return HashBytes(bytes);
}

void OfflineFrameSequence::WriteSessionRecord()
{
    if (this->sessionRecordWritten)
        return;
    nlohmann::json session;
    session["type"] = "session";
    session["sessionIdentity"] = this->SessionIdentity();
    session["buildCompatibilityId"] = CurrentBuildCompatibilityId();
    session["width"] = this->config.renderRequest.width;
    session["height"] = this->config.renderRequest.height;
    session["backend"] = "saba-mmd";
    if (!AppendDurable(this->ManifestPath(), session.dump()))
    {
        this->Fail("cannot durably write session record");
        return;
    }
    this->sessionRecordWritten = true;
}

void OfflineFrameSequence::AppendFrameRecord(const FrameRecord& record)
{
    nlohmann::json entry;
    entry["type"] = "frame";
    entry["frameIndex"] = record.frame;
    entry["rgbaHash"] = record.rgbaHash;
    entry["checkpointWireHash"] = record.checkpointWireHash;
    entry["checkpointSlot"] = record.checkpointSlot;
    if (record.hasPng)
        entry["pngFileHash"] = record.pngHash;
    if (record.hasRaw)
        entry["rawFileHash"] = record.rawHash;
    if (!AppendDurable(this->ManifestPath(), entry.dump()))
    {
        this->Fail("cannot durably append frame record");
    }
}

std::optional<OfflineFrameSequence::FrameRecord>
OfflineFrameSequence::FindFrameRecord(MotionFrameIndex frame)
{
    if (!TruncateJsonlTail(this->ManifestPath()))
    {
        this->Fail("cannot truncate manifest crash tail");
        return std::nullopt;
    }
    std::ifstream stream(this->ManifestPath());
    if (!stream)
        return std::nullopt;
    std::string line;
    std::optional<FrameRecord> match;
    while (std::getline(stream, line))
    {
        if (line.empty())
            continue;
        nlohmann::json entry;
        try
        {
            entry = nlohmann::json::parse(line);
        }
        catch (const nlohmann::json::exception&)
        {
            this->Fail("manifest contains a corrupted complete line");
            return std::nullopt;
        }
        if (entry.value("type", "") != "frame")
            continue;
        if (entry.value("frameIndex", 0ULL) != frame)
            continue;
        FrameRecord record;
        record.frame = frame;
        record.rgbaHash = entry.value("rgbaHash", 0ULL);
        record.checkpointWireHash = entry.value(
            "checkpointWireHash",
            0ULL
        );
        record.checkpointSlot = entry.value("checkpointSlot", "");
        record.hasPng = entry.contains("pngFileHash");
        record.pngHash = entry.value("pngFileHash", 0ULL);
        record.hasRaw = entry.contains("rawFileHash");
        record.rawHash = entry.value("rawFileHash", 0ULL);
        match = record;
    }
    return match;
}

std::optional<OfflineFrameSequence::FrameRecord>
OfflineFrameSequence::ReadLastCommittedRecord()
{
    if (!TruncateJsonlTail(this->ManifestPath()))
    {
        this->Fail("cannot truncate manifest crash tail");
        return std::nullopt;
    }
    std::ifstream stream(this->ManifestPath());
    if (!stream)
        return std::nullopt;
    std::string line;
    std::optional<FrameRecord> last;
    bool sawSession = false;
    while (std::getline(stream, line))
    {
        if (line.empty())
            continue;
        nlohmann::json entry;
        try
        {
            entry = nlohmann::json::parse(line);
        }
        catch (const nlohmann::json::exception&)
        {
            this->Fail("manifest contains a corrupted complete line");
            return std::nullopt;
        }
        const std::string type = entry.value("type", "");
        if (type == "session")
        {
            sawSession = true;
            this->sessionRecordWritten = true;
            const std::string identity = entry.value(
                "sessionIdentity",
                ""
            );
            if (identity != this->SessionIdentity())
            {
                this->Fail("session identity mismatch");
                return std::nullopt;
            }
            if (entry.value("buildCompatibilityId", 0ULL) !=
                CurrentBuildCompatibilityId())
            {
                this->Fail("build compatibility mismatch");
                return std::nullopt;
            }
        }
        else if (type == "frame")
        {
            FrameRecord record;
            record.frame = entry.value("frameIndex", 0ULL);
            record.rgbaHash = entry.value("rgbaHash", 0ULL);
            record.checkpointWireHash = entry.value(
                "checkpointWireHash",
                0ULL
            );
            record.checkpointSlot = entry.value("checkpointSlot", "");
            record.hasPng = entry.contains("pngFileHash");
            record.pngHash = entry.value("pngFileHash", 0ULL);
            record.hasRaw = entry.contains("rawFileHash");
            record.rawHash = entry.value("rawFileHash", 0ULL);
            last = record;
        }
    }
    if (!sawSession)
    {
        this->Fail("manifest has no session record");
        return std::nullopt;
    }
    return last;
}

OfflineFrameSequence::FrameRecord
OfflineFrameSequence::RenderAndPersist(MotionFrameIndex frame)
{
    // 1. Re-publish the exact runtime state (never Update(0)).
    this->modelInstance->PublishCurrentRuntimeFrame();

    // 2. Presentation sampled at exactly this frame.
    OfflineRenderRequest request = this->config.renderRequest;
    this->ApplyPresentation(frame, request);

    // 3. Render through the unified offscreen boundary.
    const Rgba8Frame rgba = RenderOffline(
        *this->scene,
        request,
        *this->renderer
    );
    const std::uint64_t rgbaHash = HashBytes(rgba.pixels);

    const std::filesystem::path pngPath =
        this->FramePath(frame, ".png");
    const std::filesystem::path rawPath =
        this->FramePath(frame, ".rgba");

    // Committed manifest record is the authority; verify its artifacts
    // before applying any policy.
    const std::optional<FrameRecord> committed =
        this->FindFrameRecord(frame);
    if (committed.has_value() &&
        this->config.overwritePolicy == SequenceOverwritePolicy::Overwrite &&
        committed->rgbaHash != rgbaHash)
    {
        this->Fail("Overwrite rgba hash mismatch for committed frame " +
            std::to_string(frame));
        return {};
    }
    bool committedArtifactsIntact = true;
    if (committed.has_value())
    {
        const bool pngIntact = !committed->hasPng ||
            (std::filesystem::is_regular_file(pngPath) &&
                this->FileHash(pngPath) == committed->pngHash);
        const bool rawIntact = !committed->hasRaw ||
            (std::filesystem::is_regular_file(rawPath) &&
                this->FileHash(rawPath) == committed->rawHash);
        committedArtifactsIntact = pngIntact && rawIntact;
    }
    if (committed.has_value() && !committedArtifactsIntact)
    {
        this->Fail("committed artifact is missing or corrupt for frame " +
            std::to_string(frame));
        return {};
    }

    if (committed.has_value())
    {
        switch (this->config.overwritePolicy)
        {
        case SequenceOverwritePolicy::Reject:
            this->Fail("frame is already committed: " +
                std::to_string(frame));
            return {};
        case SequenceOverwritePolicy::Overwrite:
            break;  // rewrite artifacts + checkpoint below, no duplicate record
        case SequenceOverwritePolicy::VerifySkip:
            if (committed->rgbaHash != rgbaHash)
            {
                this->Fail("VerifySkip rgba hash mismatch for frame " +
                    std::to_string(frame));
                return {};
            }
            // Historical frames never move the committed cursor; only new
            // manifest commits (or the manifest tail) may do that.
            return *committed;
        }
    }
    else
    {
        const bool pngExists = std::filesystem::is_regular_file(pngPath);
        const bool rawExists = this->config.writeRaw &&
            std::filesystem::is_regular_file(rawPath);
        if (pngExists || rawExists)
        {
            // Orphan artifacts: no trusted committed reference.
            if (this->config.overwritePolicy !=
                SequenceOverwritePolicy::Overwrite)
            {
                this->Fail("orphan artifact has no committed record for frame " +
                    std::to_string(frame));
                return {};
            }
        }
    }
    const bool rewriteOnly =
        committed.has_value() &&
        this->config.overwritePolicy == SequenceOverwritePolicy::Overwrite;
    bool skipCheckpointRewrite = false;

    FrameRecord record;
    record.frame = frame;
    record.rgbaHash = rgbaHash;

    if (this->config.writePng)
    {
        const std::vector<std::uint8_t> png = EncodePngRgba8(
            rgba.width,
            rgba.height,
            rgba.pixels
        );
        record.pngHash = HashBytes(png);
        record.hasPng = true;
        this->WriteArtifactAtomic(pngPath, png);
        if (this->failed)
            return record;
    }
    if (this->config.writeRaw)
    {
        record.rawHash = rgbaHash;
        record.hasRaw = true;
        this->WriteArtifactAtomic(rawPath, rgba.pixels);
        if (this->failed)
            return record;
    }

    // 4. Persist checkpoint with the alternate-slot invariant.
    FrameCheckpoint checkpoint;
    if (this->runtime->CreateCheckpoint(checkpoint) !=
        TimelineStatus::Ok)
    {
        this->Fail("checkpoint capture failed at frame " +
            std::to_string(frame));
        return record;
    }
    if (checkpoint.frame != frame)
    {
        this->Fail("checkpoint frame mismatch");
        return record;
    }
    const std::vector<std::uint8_t> wire = SerializeCheckpoint(checkpoint);
    record.checkpointWireHash = HashBytes(wire);
    if (rewriteOnly)
    {
        // Keep the slot the committed record points at; do not disturb the
        // alternate slot.
        record.checkpointSlot = committed->checkpointSlot;
        // Only the newest committed frame's checkpoint is the resume anchor;
        // rewriting an older frame's checkpoint could destroy the slot a
        // newer committed frame depends on (A/B invariant).
        const std::optional<FrameRecord> latest =
            this->ReadLastCommittedRecord();
        skipCheckpointRewrite =
            !latest.has_value() || latest->frame != frame;
    }
    else
    {
        // Alternate from the last committed slot; never derive from parity.
        record.checkpointSlot =
            (this->lastCommittedCheckpointSlot == "A") ? "B" : "A";
    }
    if (!skipCheckpointRewrite)
    {
        this->WriteArtifactAtomic(
            this->CheckpointPath(record.checkpointSlot),
            wire
        );
        if (this->failed)
            return record;
    }

    if (rewriteOnly)
    {
        return record;
    }

    // 5. Manifest commit record is the committed-state authority.
    this->WriteSessionRecord();
    if (this->failed)
        return record;
    this->AppendFrameRecord(record);
    if (!this->failed)
    {
        this->lastCommitted = frame;
        this->lastCommittedCheckpointSlot = record.checkpointSlot;
    }
    return record;
}

void OfflineFrameSequence::CommitFrame(MotionFrameIndex frame)
{
    (void)this->RenderAndPersist(frame);
}
}  // namespace wisteria
