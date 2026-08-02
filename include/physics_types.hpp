#pragma once

#include <cstdint>
#include <limits>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

enum class PhysicsMotionType : std::uint8_t
{
    Static,
    Dynamic,
    Kinematic
};

enum class PhysicsShapeKind : std::uint8_t
{
    Sphere,
    Box,
    Capsule
};

struct PhysicsShapeDesc
{
    PhysicsShapeKind kind = PhysicsShapeKind::Sphere;

    // Sphere: x = radius.
    // Box: xyz = half extents.
    // Capsule: x = radius, y = cylinder height. The capsule axis is +Y.
    glm::vec3 dimensions{0.5f, 0.0f, 0.0f};

    static PhysicsShapeDesc Sphere(float radius) noexcept;
    static PhysicsShapeDesc Box(const glm::vec3& halfExtents) noexcept;
    static PhysicsShapeDesc Capsule(
        float radius,
        float cylinderHeight
    ) noexcept;
};

struct PhysicsBodyHandle
{
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    bool IsValid() const noexcept;

    friend bool operator==(
        const PhysicsBodyHandle& left,
        const PhysicsBodyHandle& right
    ) noexcept = default;
};

struct PhysicsBodyDesc
{
    PhysicsShapeDesc shape{};
    PhysicsMotionType motionType = PhysicsMotionType::Static;

    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    float mass = 0.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    float restitution = 0.0f;
    float friction = 0.5f;

    std::uint16_t collisionGroup = 0x0001U;
    std::uint16_t collisionMask = 0xFFFFU;
    bool disableDeactivation = false;
};

struct PhysicsBodyState
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    bool active = false;
};

struct PhysicsStepSettings
{
    int maxSubSteps = 4;
    float fixedTimeStep = 1.0f / 60.0f;
    float maxDeltaTime = 0.1f;
};
