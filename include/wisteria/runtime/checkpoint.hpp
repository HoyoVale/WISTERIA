#pragma once

#include "wisteria/runtime/determinism.hpp"
#include "wisteria/runtime/frame_snapshot.hpp"
#include "wisteria/mmd/mmd_determinism.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wisteria
{
// R1.2C orchestration types. Single source of truth:
// docs/architecture/R1_2C_FRAME_CHECKPOINT_CONTRACT.md (v2, frozen).

// Stable-sorted user overrides. Sorting (UTF-8 name lexicographic) is part
// of the serialization/hash contract; duplicates are invalid.
struct UserOverrideState
{
    std::vector<std::pair<std::string, float>> morphOverrides;
    std::vector<std::pair<std::string, bool>> ikOverrides;
    bool physicsEnabled = true;
    bool loopMotion = false;
};

// Immutable asset + execution identity. The two R1.2B physics fingerprints
// are reused directly, not redefined.
struct AssetIdentity
{
    std::uint64_t pmxFileHash = 0;
    std::uint64_t vmdFileHash = 0;
    bool hasMotion = false;  // distinguishes "no VMD" from vmdFileHash == 0
    std::uint64_t layoutFingerprint = 0;
    std::uint64_t physicsConfigurationFingerprint = 0;
};

// Three-layer determinism fingerprint (asset / execution / captured state).
struct DeterminismFingerprint
{
    std::uint32_t schemaVersion = 1;
    MotionFrameIndex frame = 0;
    AssetIdentity asset;
    ReplayConfig config;
    UserOverrideState overrides;
    FrameStateHashes state;
};

struct FrameCheckpoint
{
    MotionFrameIndex frame = 0;
    PhysicsSnapshot physics;
    UserOverrideState overrides;
    ReplayConfig config;
    DeterminismFingerprint fingerprint;
};
}  // namespace wisteria
