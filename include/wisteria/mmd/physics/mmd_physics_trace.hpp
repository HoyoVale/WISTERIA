#pragma once

#include "wisteria/runtime/determinism.hpp"
#include "wisteria/animation/bone.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace wisteria
{
// R1.3 Phase 0A trace schema v1 (contract §6). These are neutral, read-only
// observation records captured at Canonical Frame Boundaries. The runtime
// only fills the struct; JSONL writing and diffing live in tools/trace.

inline constexpr std::uint32_t MmdPhysicsTraceSchemaVersion = 1U;

// Ground-plane contact sentinel: contact pairs with the Saba ground proxy
// report this body index instead of a PMX rigid body index.
inline constexpr std::uint32_t MmdPhysicsTraceGroundBodyIndex =
    std::numeric_limits<std::uint32_t>::max();

struct MmdPhysicsTraceTransform
{
    glm::vec3 position{0.0f};
    // Column-major 3x3 basis:
    // [c0.x, c0.y, c0.z, c1.x, c1.y, c1.z, c2.x, c2.y, c2.z]
    std::array<float, 9> rotationBasis{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

struct MmdPhysicsTraceHash
{
    std::string hex = "0000000000000000";  // 16 lowercase hex chars
    bool valid = false;
};

struct MmdPhysicsTraceBody
{
    std::uint32_t index = 0U;
    PmxRigidBodyMode mode = PmxRigidBodyMode::FollowBone;
    MmdPhysicsTraceTransform worldTransform;               // Bullet body COM
    MmdPhysicsTraceTransform interpolationWorldTransform;  // latency interp
    MmdPhysicsTraceTransform motionStateTransform;
    bool motionStateAvailable = false;
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
};

struct MmdPhysicsTraceBone
{
    BoneIndex index = InvalidBoneIndex;
    // Column-major 4x4 matrices (16 floats).
    std::array<float, 16> localMatrix{1, 0, 0, 0, 0, 1, 0, 0,
                                      0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> globalMatrix{1, 0, 0, 0, 0, 1, 0, 0,
                                       0, 0, 1, 0, 0, 0, 0, 1};
};

struct MmdPhysicsTraceJoint
{
    std::uint32_t index = 0U;
    // R1.3 §6.3: raw = constraint-frame difference without limits;
    // violation = excess beyond allowed limits.
    float rawLinearError = 0.0f;
    float linearViolation = 0.0f;
    float rawAngularErrorDeg = 0.0f;
    float angularViolationDeg = 0.0f;
};

struct MmdPhysicsTraceContactPair
{
    std::uint32_t bodyA = 0U;
    std::uint32_t bodyB = 0U;
    int pointCount = 0;
    float maxPenetration = 0.0f;  // most negative contact distance
    float normalImpulse = 0.0f;   // summed applied impulse
};

struct MmdPhysicsTraceFrame
{
    std::uint32_t traceSchemaVersion = MmdPhysicsTraceSchemaVersion;
    std::string backendIdentity = "saba-mmd";
    std::string presetIdentity;
    std::string effectiveConfigurationHash;  // 16 lowercase hex
    std::string executionProfile = "deterministic-cold-step-v1";
    std::string modelHash;  // 16 lowercase hex
    std::string motionHash;  // 16 lowercase hex
    bool hasMotion = false;
    MotionFrameIndex frame = 0U;
    TimelineTick physicsTick = 0U;
    bool canonical = false;
    MmdPhysicsTraceHash poseHash;
    MmdPhysicsTraceHash physicsHash;
    MmdPhysicsTraceHash vertexHash;
    std::vector<MmdPhysicsTraceBody> bodies;       // sorted by index
    std::vector<MmdPhysicsTraceBone> bones;        // sorted by index
    std::vector<MmdPhysicsTraceJoint> joints;      // sorted by index
    std::vector<MmdPhysicsTraceContactPair> contactPairs;  // sorted by pair
    std::vector<std::string> events;
};

inline std::string FormatTraceHex(std::uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        output[static_cast<std::size_t>(index)] =
            digits[value & 0xFULL];
        value >>= 4U;
    }
    return output;
}
}  // namespace wisteria
