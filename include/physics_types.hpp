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

struct PhysicsConstraintHandle
{
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    bool IsValid() const noexcept;

    friend bool operator==(
        const PhysicsConstraintHandle& left,
        const PhysicsConstraintHandle& right
    ) noexcept = default;
};

struct PhysicsConstraintFrame
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct PhysicsSpring6DofDesc
{
    PhysicsBodyHandle bodyA{};
    PhysicsBodyHandle bodyB{};
    PhysicsConstraintFrame frameA{};
    PhysicsConstraintFrame frameB{};
    glm::vec3 linearLower{0.0f};
    glm::vec3 linearUpper{0.0f};
    glm::vec3 angularLower{0.0f};
    glm::vec3 angularUpper{0.0f};
    glm::vec3 linearStiffness{0.0f};
    glm::vec3 angularStiffness{0.0f};
    glm::vec3 linearDamping{0.5f};
    glm::vec3 angularDamping{0.5f};
    bool disableCollisionsBetweenLinkedBodies = true;
};

struct PhysicsSixDofDesc
{
    PhysicsBodyHandle bodyA{};
    PhysicsBodyHandle bodyB{};
    PhysicsConstraintFrame frameA{};
    PhysicsConstraintFrame frameB{};
    glm::vec3 linearLower{0.0f};
    glm::vec3 linearUpper{0.0f};
    glm::vec3 angularLower{0.0f};
    glm::vec3 angularUpper{0.0f};
    bool disableCollisionsBetweenLinkedBodies = true;
};

struct PhysicsPointToPointDesc
{
    PhysicsBodyHandle bodyA{};
    PhysicsBodyHandle bodyB{};
    glm::vec3 pivotA{0.0f};
    glm::vec3 pivotB{0.0f};
    bool disableCollisionsBetweenLinkedBodies = true;
};

struct PhysicsConeTwistDesc
{
    PhysicsBodyHandle bodyA{};
    PhysicsBodyHandle bodyB{};
    PhysicsConstraintFrame frameA{};
    PhysicsConstraintFrame frameB{};
    float swingSpan1 = 0.0f;
    float swingSpan2 = 0.0f;
    float twistSpan = 0.0f;
    bool disableCollisionsBetweenLinkedBodies = true;
};

struct PhysicsSliderDesc
{
    PhysicsBodyHandle bodyA{};
    PhysicsBodyHandle bodyB{};
    PhysicsConstraintFrame frameA{};
    PhysicsConstraintFrame frameB{};
    float linearLower = 0.0f;
    float linearUpper = 0.0f;
    float angularLower = 0.0f;
    float angularUpper = 0.0f;
    bool disableCollisionsBetweenLinkedBodies = true;
};

struct PhysicsHingeDesc
{
    PhysicsBodyHandle bodyA{};
    PhysicsBodyHandle bodyB{};
    PhysicsConstraintFrame frameA{};
    PhysicsConstraintFrame frameB{};
    float lowerAngle = 0.0f;
    float upperAngle = 0.0f;
    bool disableCollisionsBetweenLinkedBodies = true;
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
    glm::vec3 linearFactor{1.0f};
    glm::vec3 angularFactor{1.0f};

    std::uint16_t collisionGroup = 0x0001U;
    std::uint16_t collisionMask = 0xFFFFU;
    bool disableDeactivation = false;
};

struct PhysicsDebugLine
{
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    glm::vec3 color{1.0f};
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
