#pragma once

#include "wisteria/rendering/offline_render.hpp"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/runtime/model_instance.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace wisteria
{
class Renderer;
class Scene;

// R1.6 Phase 0E: overwrite behavior for a frame's output artifacts.
// VerifySkip re-renders the frame, compares the canonical RGBA hash against
// the committed record, and skips only the encode/write when equal.
enum class SequenceOverwritePolicy
{
    Reject,
    Overwrite,
    VerifySkip
};

struct OfflineFrameSequenceConfig
{
    std::filesystem::path outputDirectory;
    OfflineRenderRequest renderRequest;
    SequenceOverwritePolicy overwritePolicy =
        SequenceOverwritePolicy::Reject;
    bool writePng = true;
    bool writeRaw = false;  // optional persisted .rgba
    // Host-provided identity for the scene/presentation composition that the
    // deterministic runtime cannot prove (lights, environment, entity set).
    std::string scenePresentationIdentity;
};

// Deterministic batch frame-sequence orchestration (R1.6 Phase 0E).
// v1 scope: exactly one deterministic MMD driver (Saba + R1.2C checkpoint).
// The session borrows Scene / Renderer / runtime / ModelInstance; it owns
// the output directory, JSONL manifest, checkpoint persistence and cursor.
// Any frame-transaction failure is fail-stop: the session refuses further
// work and must be reopened (resume from last committed frame).
class OfflineFrameSequence
{
public:
    OfflineFrameSequence(
        Scene& scene,
        Renderer& renderer,
        MmdRuntimeModel& runtime,
        ModelInstance& modelInstance,
        OfflineFrameSequenceConfig config
    );
    ~OfflineFrameSequence() = default;

    OfflineFrameSequence(const OfflineFrameSequence&) = delete;
    OfflineFrameSequence& operator=(const OfflineFrameSequence&) = delete;

    // From-start deterministic sequence [start, end] inclusive.
    void RenderRange(MotionFrameIndex start, MotionFrameIndex end);

    // Resume from the last committed frame in the manifest up to `end`.
    void Resume(MotionFrameIndex end);

    std::optional<MotionFrameIndex> LastCommittedFrame() const noexcept;
    bool Failed() const noexcept;

private:
    struct FrameRecord
    {
        MotionFrameIndex frame = 0U;
        std::uint64_t rgbaHash = 0U;
        std::uint64_t pngHash = 0U;
        std::uint64_t rawHash = 0U;
        std::uint64_t checkpointWireHash = 0U;
        std::string checkpointSlot;
        bool hasPng = false;
        bool hasRaw = false;
    };

    std::filesystem::path ManifestPath() const;
    std::filesystem::path FramePath(
        MotionFrameIndex frame,
        std::string_view extension
    ) const;
    std::filesystem::path CheckpointPath(std::string_view slot) const;
    std::string SessionIdentity() const;

    void Fail(const std::string& message);
    void StepTo(MotionFrameIndex target);
    void CommitFrame(MotionFrameIndex frame);
    FrameRecord RenderAndPersist(MotionFrameIndex frame);
    void ApplyPresentation(
        MotionFrameIndex frame,
        OfflineRenderRequest& request
    ) const;
    void WriteArtifactAtomic(
        const std::filesystem::path& finalPath,
        std::span<const std::uint8_t> bytes
    );
    std::uint64_t FileHash(const std::filesystem::path& path) const;

    void WriteSessionRecord();
    std::optional<FrameRecord> FindFrameRecord(
        MotionFrameIndex frame
    );
    std::optional<FrameRecord> ReadLastCommittedRecord();
    void AppendFrameRecord(const FrameRecord& record);

    Scene* scene = nullptr;
    Renderer* renderer = nullptr;
    MmdRuntimeModel* runtime = nullptr;
    ModelInstance* modelInstance = nullptr;
    OfflineFrameSequenceConfig config;
    std::optional<MotionFrameIndex> lastCommitted;
    std::string lastCommittedCheckpointSlot;
    bool failed = false;
    bool sessionRecordWritten = false;
};
}  // namespace wisteria
