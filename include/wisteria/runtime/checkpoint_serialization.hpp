#pragma once

#include "wisteria/runtime/checkpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wisteria
{
// R1.4 Phase 0A: WISTERIA Checkpoint Envelope + MMD R1.2C payload v1 wire
// codec (contract §2C). Binary, little-endian, IEEE-754 raw bits; JSON is
// not a canonical checkpoint format. Portable bytes != portable
// deterministic semantics: build compatibility is verified before any
// mutation.

inline constexpr std::uint32_t CheckpointWireVersion = 1U;
inline constexpr std::uint32_t CheckpointPayloadKindMmdR12C = 1U;
inline constexpr std::uint32_t CheckpointPayloadSchemaMmdR12C = 1U;
inline constexpr std::uint32_t CheckpointBackendIdSabaMmd = 1U;
inline constexpr std::uint32_t CheckpointDeterministicProfileColdStepV1 = 1U;

inline constexpr std::uint64_t CheckpointWireHeaderSize = 48U;

// Engine-owned build/runtime compatibility identity (contract §2C).
// These revisions are frozen per deterministic release; changing any of
// them invalidates every previously serialized checkpoint. Production
// serializers write CurrentBuildCompatibilityId() and deserializers compare
// against it; callers cannot substitute their own identity.
inline constexpr std::uint64_t DeterministicCompatibilityRevision = 1U;
inline constexpr std::uint64_t SabaCompatibilityRevision = 1U;
inline constexpr std::uint64_t BulletCompatibilityRevision = 1U;
inline constexpr std::uint64_t CheckpointPayloadImplementationRevision = 1U;
// 1 = little-endian 64-bit address-space class (Windows/Linux x64 are in
// this class). Big-endian or non-64-bit toolchains must use another class.
inline constexpr std::uint64_t BuildPlatformCompatibilityClass = 1U;

inline constexpr std::uint64_t CurrentBuildCompatibilityId()
{
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t state = kOffsetBasis;
    const std::uint64_t parts[] = {
        DeterministicCompatibilityRevision,
        SabaCompatibilityRevision,
        BulletCompatibilityRevision,
        CheckpointPayloadImplementationRevision,
        BuildPlatformCompatibilityClass
    };
    for (const std::uint64_t part : parts)
    {
        for (int shift = 0; shift < 64; shift += 8)
        {
            state ^= static_cast<std::uint8_t>((part >> shift) & 0xFFU);
            state *= kPrime;
        }
    }
    return state;
}

static_assert(
    CurrentBuildCompatibilityId() != 0U,
    "build compatibility identity must be non-zero"
);

struct CheckpointSerializationOptions
{
    // Test/internal-only build identity override. Production callers must
    // leave this empty; the codec then writes and compares the engine-owned
    // CurrentBuildCompatibilityId(). An override of 0 is invalid.
    std::optional<std::uint64_t> buildCompatibilityIdOverride;

    // Untrusted-input guards (contract §2C): never allocate beyond these.
    std::uint64_t maxPayloadBytes = 256U * 1024U * 1024U;
    std::uint64_t maxMorphOverrideCount = 1U << 20;
    std::uint64_t maxIkOverrideCount = 1U << 20;
    std::uint64_t maxRigidBodyCount = 1U << 20;
    std::uint64_t maxStringBytes = 1U << 20;
};

// Serializes a checkpoint into the wire format, writing the engine-owned
// CurrentBuildCompatibilityId() (or the test-only override). A zero build
// identity is invalid and throws std::invalid_argument.
std::vector<std::uint8_t> SerializeCheckpoint(
    const FrameCheckpoint& checkpoint,
    const CheckpointSerializationOptions& options = {}
);

// Decodes untrusted bytes. Returns TimelineStatus::Ok and fills output on
// success; output is left unchanged on failure. Structural duplicate-field
// consistency is verified here; full semantic validation happens when the
// decoded checkpoint is passed to the runtime restore path.
TimelineStatus DeserializeCheckpoint(
    const std::uint8_t* bytes,
    std::size_t size,
    const CheckpointSerializationOptions& options,
    FrameCheckpoint& output
);
}  // namespace wisteria
