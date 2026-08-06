#include "wisteria/mmd/mmd_determinism.hpp"

#include <cmath>
#include <cstring>
#include <glm/gtc/quaternion.hpp>

namespace wisteria
{
namespace
{
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr float kCanonicalQuantization = 1.0e-5f;

void FnvByte(std::uint64_t& state, std::uint8_t byte) noexcept
{
    state ^= byte;
    state *= kFnvPrime;
}

void HashU32(
    std::uint64_t& state,
    std::uint32_t value
) noexcept
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        FnvByte(state, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashFloat(std::uint64_t& state, float value) noexcept
{
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    HashU32(state, bits);
}

float CanonicalFloat(float value) noexcept
{
    if (!std::isfinite(value))
    {
        // NaN / Infinity are normalized to a fixed sentinel so a single
        // build can never produce unstable hash output from them.
        return 0.0f;
    }
    if (value == 0.0f)
    {
        return 0.0f;  // -0.0 -> +0.0
    }
    return std::round(value / kCanonicalQuantization) * kCanonicalQuantization;
}

void HashCanonicalFloat(
    std::uint64_t& state,
    float value
) noexcept
{
    HashFloat(state, CanonicalFloat(value));
}

void HashVec3(
    std::uint64_t& state,
    const glm::vec3& value
) noexcept
{
    HashFloat(state, value.x);
    HashFloat(state, value.y);
    HashFloat(state, value.z);
}

void HashCanonicalVec3(
    std::uint64_t& state,
    const glm::vec3& value
) noexcept
{
    HashCanonicalFloat(state, value.x);
    HashCanonicalFloat(state, value.y);
    HashCanonicalFloat(state, value.z);
}

glm::quat CanonicalQuat(glm::quat value) noexcept
{
    const float lengthSquared = glm::dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
    {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    value = glm::normalize(value);
    if (value.w < 0.0f)
    {
        value = -value;
    }
    return value;
}

void HashQuat(
    std::uint64_t& state,
    const glm::quat& value
) noexcept
{
    HashFloat(state, value.w);
    HashFloat(state, value.x);
    HashFloat(state, value.y);
    HashFloat(state, value.z);
}

void HashCanonicalQuat(
    std::uint64_t& state,
    const glm::quat& value
) noexcept
{
    const glm::quat canonical = CanonicalQuat(value);
    HashCanonicalFloat(state, canonical.w);
    HashCanonicalFloat(state, canonical.x);
    HashCanonicalFloat(state, canonical.y);
    HashCanonicalFloat(state, canonical.z);
}

bool IsFinite(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsFinite(const glm::quat& value) noexcept
{
    return std::isfinite(value.w) &&
        std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsFinite(const glm::mat4& value) noexcept
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (!std::isfinite(value[column][row]))
                return false;
        }
    }
    return true;
}

void HashMatrix(
    std::uint64_t& state,
    const glm::mat4& matrix
) noexcept
{
    // glm is column-major; iterate columns explicitly.
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            HashFloat(state, matrix[column][row]);
        }
    }
}

void HashCanonicalMatrix(
    std::uint64_t& state,
    const glm::mat4& matrix
) noexcept
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            HashCanonicalFloat(state, matrix[column][row]);
        }
    }
}

std::uint64_t SeedExact() noexcept
{
    std::uint64_t state = kFnvOffsetBasis;
    HashU32(state, 1U);  // schema version 1
    return state;
}

std::uint64_t SeedCanonical() noexcept
{
    std::uint64_t state = kFnvOffsetBasis;
    HashU32(state, 1U);
    return state;
}
}  // namespace

std::uint64_t Fnv1a64(
    const std::uint8_t* data,
    std::size_t size
) noexcept
{
    std::uint64_t state = kFnvOffsetBasis;
    for (std::size_t index = 0U; index < size; ++index)
    {
        FnvByte(state, data[index]);
    }
    return state;
}

DeterminismHashes HashPose(const PoseSnapshot& pose) noexcept
{
    DeterminismHashes hashes;
    // local/global/skinning must all describe the same bone count; a
    // structurally inconsistent Pose is invalid input, not a hashable state.
    if (pose.localTransforms.size() != pose.globalTransforms.size() ||
        pose.localTransforms.size() != pose.skinningTransforms.size())
    {
        hashes.valid = false;
        return hashes;
    }
    std::uint64_t exact = SeedExact();
    std::uint64_t canonical = SeedCanonical();
    // Channel counts and markers are hashed explicitly so structurally
    // different snapshots cannot collide by concatenation alone.
    HashU32(exact, static_cast<std::uint32_t>(pose.localTransforms.size()));
    HashU32(canonical, static_cast<std::uint32_t>(pose.localTransforms.size()));
    HashU32(exact, static_cast<std::uint32_t>(pose.globalTransforms.size()));
    HashU32(canonical, static_cast<std::uint32_t>(pose.globalTransforms.size()));
    HashU32(exact, static_cast<std::uint32_t>(pose.skinningTransforms.size()));
    HashU32(canonical, static_cast<std::uint32_t>(pose.skinningTransforms.size()));
    HashU32(exact, 0x504C4F43U);  // "Pose" channel marker
    HashU32(canonical, 0x504C4F43U);
    for (const glm::mat4& matrix : pose.localTransforms)
    {
        if (!IsFinite(matrix))
        {
            hashes.valid = false;
            return hashes;
        }
        HashMatrix(exact, matrix);
        HashCanonicalMatrix(canonical, matrix);
    }
    for (const glm::mat4& matrix : pose.globalTransforms)
    {
        if (!IsFinite(matrix))
        {
            hashes.valid = false;
            return hashes;
        }
        HashMatrix(exact, matrix);
        HashCanonicalMatrix(canonical, matrix);
    }
    for (const glm::mat4& matrix : pose.skinningTransforms)
    {
        if (!IsFinite(matrix))
        {
            hashes.valid = false;
            return hashes;
        }
        HashMatrix(exact, matrix);
        HashCanonicalMatrix(canonical, matrix);
    }
    hashes.exactHash = exact;
    hashes.canonicalHash = canonical;
    return hashes;
}

DeterminismHashes HashVertices(
    const DeformedVertexSnapshot& vertices
) noexcept
{
    DeterminismHashes hashes;
    const std::size_t positionCount = vertices.positions.size();
    const std::size_t normalCount = vertices.normals.size();
    if (positionCount != normalCount)
    {
        // Never silently truncate: mismatched vertex arrays are invalid
        // input, not a determinism signal.
        hashes.valid = false;
        return hashes;
    }
    std::uint64_t exact = SeedExact();
    std::uint64_t canonical = SeedCanonical();
    HashU32(exact, static_cast<std::uint32_t>(positionCount));
    HashU32(canonical, static_cast<std::uint32_t>(positionCount));
    HashU32(exact, static_cast<std::uint32_t>(normalCount));
    HashU32(canonical, static_cast<std::uint32_t>(normalCount));
    for (std::size_t index = 0U; index < positionCount; ++index)
    {
        if (!IsFinite(vertices.positions[index]) ||
            !IsFinite(vertices.normals[index]))
        {
            hashes.valid = false;
            return hashes;
        }
        HashVec3(exact, vertices.positions[index]);
        HashCanonicalVec3(canonical, vertices.positions[index]);
        HashVec3(exact, vertices.normals[index]);
        HashCanonicalVec3(canonical, vertices.normals[index]);
    }
    hashes.exactHash = exact;
    hashes.canonicalHash = canonical;
    return hashes;
}

DeterminismHashes HashPhysics(
    const PhysicsSnapshot& physics
) noexcept
{
    DeterminismHashes hashes;
    std::uint64_t exact = SeedExact();
    std::uint64_t canonical = SeedCanonical();
    HashU32(exact, static_cast<std::uint32_t>(physics.rigidBodies.size()));
    HashU32(canonical, static_cast<std::uint32_t>(physics.rigidBodies.size()));
    HashU32(exact, physics.jointCount);
    HashU32(canonical, physics.jointCount);
    std::uint32_t expectedIndex = 0U;
    for (const RigidBodySnapshot& body : physics.rigidBodies)
    {
        // The container order is not a serialization contract: indices must
        // be a strict 0..N-1 sequence, otherwise the hash is invalid.
        if (body.index != expectedIndex)
        {
            hashes.valid = false;
            return hashes;
        }
        ++expectedIndex;
        if (!IsFinite(body.position) ||
            !IsFinite(body.rotation) ||
            !IsFinite(body.interpolationPosition) ||
            !IsFinite(body.interpolationRotation) ||
            !IsFinite(body.linearVelocity) ||
            !IsFinite(body.angularVelocity) ||
            !IsFinite(body.interpolationLinearVelocity) ||
            !IsFinite(body.interpolationAngularVelocity) ||
            !IsFinite(body.totalForce) ||
            !IsFinite(body.totalTorque) ||
            !std::isfinite(body.deactivationTime) ||
            !std::isfinite(body.mass))
        {
            hashes.valid = false;
            return hashes;
        }
        HashU32(exact, body.index);
        HashU32(canonical, body.index);
        HashVec3(exact, body.position);
        HashCanonicalVec3(canonical, body.position);
        HashQuat(exact, body.rotation);
        HashCanonicalQuat(canonical, body.rotation);
        HashVec3(exact, body.interpolationPosition);
        HashCanonicalVec3(canonical, body.interpolationPosition);
        HashQuat(exact, body.interpolationRotation);
        HashCanonicalQuat(canonical, body.interpolationRotation);
        HashVec3(exact, body.linearVelocity);
        HashCanonicalVec3(canonical, body.linearVelocity);
        HashVec3(exact, body.angularVelocity);
        HashCanonicalVec3(canonical, body.angularVelocity);
        HashVec3(exact, body.interpolationLinearVelocity);
        HashCanonicalVec3(canonical, body.interpolationLinearVelocity);
        HashVec3(exact, body.interpolationAngularVelocity);
        HashCanonicalVec3(canonical, body.interpolationAngularVelocity);
        HashVec3(exact, body.totalForce);
        HashCanonicalVec3(canonical, body.totalForce);
        HashVec3(exact, body.totalTorque);
        HashCanonicalVec3(canonical, body.totalTorque);
        HashU32(exact, static_cast<std::uint32_t>(body.activationState));
        HashU32(canonical, static_cast<std::uint32_t>(body.activationState));
        HashFloat(exact, body.deactivationTime);
        HashCanonicalFloat(canonical, body.deactivationTime);
        HashFloat(exact, body.mass);
        HashCanonicalFloat(canonical, body.mass);
        HashU32(exact, body.kinematic ? 1U : 0U);
        HashU32(canonical, body.kinematic ? 1U : 0U);
    }
    hashes.exactHash = exact;
    hashes.canonicalHash = canonical;
    return hashes;
}

FrameStateHashes ComputeFrameStateHashes(
    const ModelFrameSnapshot& frame,
    const PhysicsSnapshot& physics
) noexcept
{
    FrameStateHashes hashes;
    hashes.pose = HashPose(frame.pose);
    hashes.vertex = HashVertices(frame.geometry);
    hashes.physics = HashPhysics(physics);
    return hashes;
}
}  // namespace wisteria
