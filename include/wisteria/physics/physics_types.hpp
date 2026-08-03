#pragma once

#include <cstddef>
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

struct PhysicsConstraintRuntimeState
{
    float appliedImpulse = 0.0f;
    float appliedAngularImpulse = 0.0f;
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

    // Bullet 2.75-compatible legacy spring path. Community MMD runtimes
    // (three.js, Saba, babylon-mmd, MikuMikuPhysics) all use
    // btGeneric6DofSpringConstraint instead of btGeneric6DofSpring2Constraint.
    bool useLegacySpringConstraint = false;
    // babylon-mmd's disableOffsetForConstraintFrame: Bullet 2.76+ defaults the
    // constraint-frame offset to true, while MMD's Bullet 2.75 had no offset.
    bool disableOffsetForConstraintFrame = false;
    float constraintStopErp = 0.475f;
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
    // Bullet 2.75 compatibility for the classic 6DOF path: disable the
    // 2.76+ constraint-frame offset and pin the stop ERP like babylon-mmd.
    bool bullet275Mode = false;
    float constraintStopErp = 0.475f;
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

    // Bullet spheres and capsules use their radius as the convex margin.
    // Boxes support an independent contact margin; a negative value selects
    // WISTERIA's size-derived margin policy.
    float collisionMargin = -1.0f;

    // CCD is opt-in per body. The MMD adapter enables it only for dynamic
    // bodies whose size/aspect ratio makes discrete tunnelling plausible.
    bool enableCcd = false;
    float ccdMotionThreshold = 0.0f;
    float ccdSweptSphereRadius = 0.0f;
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

struct PhysicsBodyRuntimeSettings
{
    float collisionMargin = 0.0f;
    bool ccdEnabled = false;
    float ccdMotionThreshold = 0.0f;
    float ccdSweptSphereRadius = 0.0f;
    std::uint16_t collisionGroup = 0x0001U;
    std::uint16_t collisionMask = 0xFFFFU;
    bool gravityOverride = false;
    glm::vec3 gravity{0.0f, -9.8f, 0.0f};
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
};

struct PhysicsContactPair
{
    PhysicsBodyHandle bodyA{};
    PhysicsBodyHandle bodyB{};
    std::size_t contactPointCount = 0U;
    float maximumPenetrationDepth = 0.0f;
    float totalAppliedImpulse = 0.0f;
    float maximumAppliedImpulse = 0.0f;
    glm::vec3 deepestPointOnB{0.0f};
    glm::vec3 deepestNormalOnB{0.0f, 1.0f, 0.0f};
};

struct PhysicsStepSettings
{
    int maxSubSteps = 4;
    float fixedTimeStep = 1.0f / 60.0f;
    float maxDeltaTime = 0.1f;

    // Explicitly pin the Bullet solver policy instead of depending on bundled
    // library defaults that may change between Bullet revisions.
    int solverIterations = 15;
    bool splitImpulse = true;
    float splitImpulsePenetrationThreshold = -0.02f;
    float splitImpulseTurnErp = 0.1f;
    float solverErp = 0.2f;
    float solverErp2 = 0.1f;
    // Caps penetration correction velocity so dense MMD collision proxies do
    // not explosively push one another apart after a deep contact.
    float maximumErrorReduction = 4.0f;
    float restitutionVelocityThreshold = 0.5f;
};

struct PhysicsWorldStatistics
{
    std::size_t bodyCount = 0U;
    std::size_t staticBodyCount = 0U;
    std::size_t dynamicBodyCount = 0U;
    std::size_t kinematicBodyCount = 0U;
    std::size_t activeBodyCount = 0U;
    std::size_t sleepingBodyCount = 0U;
    std::size_t constraintCount = 0U;
    std::size_t contactManifoldCount = 0U;
    std::size_t contactPointCount = 0U;
    std::size_t contactPairCount = 0U;
    std::size_t ccdBodyCount = 0U;
    float minimumBoxCollisionMargin = 0.0f;
    float maximumBoxCollisionMargin = 0.0f;
    int solverIterations = 0;
    bool splitImpulse = false;
    float splitImpulsePenetrationThreshold = 0.0f;
    float maximumErrorReduction = 0.0f;
    float restitutionVelocityThreshold = 0.0f;
    bool finite = true;
};

struct PhysicsFrameStatistics
{
    float frameDeltaTime = 0.0f;
    float simulatedDeltaTime = 0.0f;
    float fixedTimeStep = 1.0f / 60.0f;
    float accumulatorTime = 0.0f;
    float droppedTime = 0.0f;
    double physicsCpuMilliseconds = 0.0;
    std::size_t substepCount = 0U;
    std::size_t stabilizationSubstepCount = 0U;
    bool catchUpLimited = false;
    PhysicsWorldStatistics world{};
};
