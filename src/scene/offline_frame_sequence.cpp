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
#include <sstream>
#include <stdexcept>

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
    std::uint64_t state = HashString(
        this->config.outputDirectory.string()
    );
    state ^= CurrentBuildCompatibilityId();
    state *= 1099511628211ULL;
    state ^= this->config.renderRequest.width;
    state *= 1099511628211ULL;
    state ^= this->config.renderRequest.height;
    state *= 1099511628211ULL;
    return std::to_string(state);
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
    if (this->failed)
        this->Fail("sequence is already failed");
    if (start > end)
    {
        this->Fail("start frame exceeds end frame");
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

    this->StepTo(start);
    for (MotionFrameIndex frame = start; frame <= end; ++frame)
    {
        this->CommitFrame(frame);
        if (frame != end)
        {
            auto* stepper =
                dynamic_cast<IDeterministicFrameStepper*>(this->runtime);
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

void OfflineFrameSequence::Resume(MotionFrameIndex end)
{
    if (this->failed)
        this->Fail("sequence is already failed");
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
    if (record->frame >= end)
        return;

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

    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(this->runtime);
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

void OfflineFrameSequence::ApplyPresentation(
    MotionFrameIndex frame,
    OfflineRenderRequest& request
) const
{
    ApplyMmdCameraFrame(
        *this->runtime,
        static_cast<float>(frame),
        request.camera,
        this->config.renderRequest.camera.GetParam()
    );
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
    {
        std::ofstream stream(temporary, std::ios::binary);
        if (!stream)
        {
            this->Fail("cannot open temporary artifact: " +
                temporary.string());
            return;
        }
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        stream.flush();
        if (!stream)
        {
            this->Fail("cannot write temporary artifact: " +
                temporary.string());
            return;
        }
    }
    std::error_code ignored;
    std::filesystem::remove(finalPath, ignored);
    std::filesystem::rename(temporary, finalPath, ignored);
    if (ignored)
    {
        this->Fail("cannot commit artifact: " + finalPath.string() +
            " (" + ignored.message() + ")");
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
    std::ofstream stream(this->ManifestPath(), std::ios::app);
    if (!stream)
    {
        this->Fail("cannot open manifest for session record");
        return;
    }
    stream << session.dump() << '\n';
    stream.flush();
    if (!stream)
    {
        this->Fail("cannot write session record");
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
    std::ofstream stream(this->ManifestPath(), std::ios::app);
    if (!stream)
    {
        this->Fail("cannot open manifest for frame record");
        return;
    }
    stream << entry.dump() << '\n';
    stream.flush();
    if (!stream)
    {
        this->Fail("cannot append frame record");
    }
}

std::optional<OfflineFrameSequence::FrameRecord>
OfflineFrameSequence::FindFrameRecord(MotionFrameIndex frame) const
{
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
            break;
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
            // Partial trailing record after a crash: ignore the remainder.
            break;
        }
        const std::string type = entry.value("type", "");
        if (type == "session")
        {
            sawSession = true;
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

    const bool pngExists = std::filesystem::is_regular_file(pngPath);
    const bool rawExists = this->config.writeRaw &&
        std::filesystem::is_regular_file(rawPath);
    if (pngExists || rawExists)
    {
        switch (this->config.overwritePolicy)
        {
        case SequenceOverwritePolicy::Reject:
            this->Fail("output artifact already exists for frame " +
                std::to_string(frame));
            break;
        case SequenceOverwritePolicy::Overwrite:
            break;
        case SequenceOverwritePolicy::VerifySkip:
        {
            const std::optional<FrameRecord> existing =
                this->FindFrameRecord(frame);
            if (!existing.has_value() || existing->frame != frame ||
                existing->rgbaHash != rgbaHash)
            {
                this->Fail("VerifySkip has no matching committed record");
            }
            // Artifacts and record already committed with the same RGBA:
            // skip encode/write entirely.
            return *existing;
        }
        }
    }

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
    record.checkpointSlot = (frame % 2U == 0U) ? "A" : "B";
    this->WriteArtifactAtomic(
        this->CheckpointPath(record.checkpointSlot),
        wire
    );
    if (this->failed)
        return record;

    // 5. Manifest commit record is the committed-state authority.
    this->WriteSessionRecord();
    if (this->failed)
        return record;
    this->AppendFrameRecord(record);
    if (!this->failed)
        this->lastCommitted = frame;
    return record;
}

void OfflineFrameSequence::CommitFrame(MotionFrameIndex frame)
{
    (void)this->RenderAndPersist(frame);
}
}  // namespace wisteria
