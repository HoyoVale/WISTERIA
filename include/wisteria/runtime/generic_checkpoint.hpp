#pragma once

#include "wisteria/core/root_motion.hpp"
#include "wisteria/runtime/determinism.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wisteria
{
// R1.8 GenericR18 checkpoint payload (in-memory neutral state).
//
// Represents exactly the Generic Deterministic Mode v1 subset (single active
// clip, canonical 30Hz timeline, loop/non-loop, pose, animation-driven
// morph, user morph overrides, root-motion configuration + pending delta).
// Any Animator state outside this subset is rejected before capture/restore
// with TimelineStatus::UnsupportedDeterministicState.
struct GenericRuntimeCheckpoint
{
    MotionFrameIndex frame = 0U;
    // Evaluated clip time at the canonical boundary (wrapped for looping,
    // clamped otherwise). Must match the payload frame derivation.
    float canonicalTime = 0.0f;
    bool looping = true;
    bool playing = true;      // deterministic v1 is always playing
    bool clipClamped = false; // non-looping terminal state

    bool rootMotionEnabled = false;
    std::optional<std::uint32_t> rootMotionBoneIndex;
    RootMotionDelta pendingRootMotion;

    // Sorted (UTF-8 lexicographic) persistent morph overrides.
    std::vector<std::pair<std::string, float>> morphOverrides;
    std::optional<std::uint32_t> activeClipIndex;

    std::uint32_t motionFps = 30U;
    std::uint32_t physicsHz = 120U;
    std::uint32_t warmupFrames = 0U;
    std::uint64_t assetFingerprint = 0U;
};

// R1.8 backend-neutral deterministic checkpoint surface for payload kind 2
// (Generic R1.8). Saba keeps its R1.2C FrameCheckpoint path (payload kind 1);
// orchestration selects by capability + interface cast.
class IDeterministicCheckpoint
{
public:
    virtual ~IDeterministicCheckpoint() = default;

    virtual TimelineStatus CreateCheckpoint(
        GenericRuntimeCheckpoint& output
    ) const = 0;
    virtual TimelineStatus RestoreCheckpoint(
        const GenericRuntimeCheckpoint& checkpoint
    ) = 0;
    virtual TimelineStatus ReplayFromCheckpoint(
        const GenericRuntimeCheckpoint& checkpoint,
        MotionFrameIndex target
    ) = 0;
};
}  // namespace wisteria
