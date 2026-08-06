#pragma once

#include "wisteria/runtime/determinism.hpp"
#include "wisteria/runtime/frame_snapshot.hpp"

#include <cstddef>
#include <cstdint>

namespace wisteria
{
// State hashes only hash the corresponding state; input configuration and
// target frame are deliberately excluded (DeterminismFingerprint, added in
// R1.2C, combines them for checkpoint compatibility).
struct DeterminismHashes
{
    std::uint64_t exactHash = 0U;      // raw float bits, same-build strict
    std::uint64_t canonicalHash = 0U;  // normalized/quantized diagnostic
    // False when the input state was structurally invalid (mismatched array
    // counts, non-finite components, duplicate/out-of-order body indices).
    // Consumers must not treat an invalid hash as a determinism proof.
    bool valid = true;
};

struct FrameStateHashes
{
    DeterminismHashes pose;
    DeterminismHashes vertex;
    DeterminismHashes physics;
};

// FNV-1a 64 over a byte range. Internal building block; callers normally use
// the typed hashers below.
std::uint64_t Fnv1a64(
    const std::uint8_t* data,
    std::size_t size
) noexcept;

DeterminismHashes HashPose(const PoseSnapshot& pose) noexcept;
DeterminismHashes HashVertices(
    const DeformedVertexSnapshot& vertices
) noexcept;
DeterminismHashes HashPhysics(
    const PhysicsSnapshot& physics
) noexcept;

FrameStateHashes ComputeFrameStateHashes(
    const ModelFrameSnapshot& frame,
    const PhysicsSnapshot& physics
) noexcept;
}  // namespace wisteria
