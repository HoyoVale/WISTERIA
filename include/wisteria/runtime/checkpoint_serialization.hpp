#pragma once

#include "wisteria/runtime/checkpoint.hpp"

#include <cstddef>
#include <cstdint>
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

struct CheckpointSerializationOptions
{
    // Caller-provided build/runtime compatibility identity. Must match on
    // deserialize or the checkpoint is rejected before any allocation or
    // mutation.
    std::uint64_t buildCompatibilityId = 1U;

    // Untrusted-input guards (contract §2C): never allocate beyond these.
    std::uint64_t maxPayloadBytes = 256U * 1024U * 1024U;
    std::uint64_t maxMorphOverrideCount = 1U << 20;
    std::uint64_t maxIkOverrideCount = 1U << 20;
    std::uint64_t maxRigidBodyCount = 1U << 20;
    std::uint64_t maxStringBytes = 1U << 20;
};

// Serializes a checkpoint into the wire format. buildCompatibilityId must
// be non-zero; invalid options throw std::invalid_argument.
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
