#pragma once

#include "wisteria/physics/physics_types.hpp"
#include <cstddef>
#include <memory>
#include <span>

// WISTERIA-facing rigid-body world. Bullet types are intentionally hidden
// behind the implementation boundary so engine code does not depend on bt*.
namespace wisteria
{
class PhysicsWorld
{
public:
    explicit PhysicsWorld(const PhysicsStepSettings& settings = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    void SetGravity(const glm::vec3& gravity);
    glm::vec3 Gravity() const noexcept;

    void SetStepSettings(const PhysicsStepSettings& settings);
    const PhysicsStepSettings& StepSettings() const noexcept;

    PhysicsBodyHandle CreateBody(const PhysicsBodyDesc& description);
    bool DestroyBody(PhysicsBodyHandle body) noexcept;
    void Clear() noexcept;

    PhysicsConstraintHandle CreateSpring6DofConstraint(
        const PhysicsSpring6DofDesc& description
    );
    PhysicsConstraintHandle CreateSixDofConstraint(
        const PhysicsSixDofDesc& description
    );
    PhysicsConstraintHandle CreatePointToPointConstraint(
        const PhysicsPointToPointDesc& description
    );
    PhysicsConstraintHandle CreateConeTwistConstraint(
        const PhysicsConeTwistDesc& description
    );
    PhysicsConstraintHandle CreateSliderConstraint(
        const PhysicsSliderDesc& description
    );
    PhysicsConstraintHandle CreateHingeConstraint(
        const PhysicsHingeDesc& description
    );
    bool DestroyConstraint(PhysicsConstraintHandle constraint) noexcept;
    bool Contains(PhysicsConstraintHandle constraint) const noexcept;
    PhysicsConstraintRuntimeState ConstraintState(
        PhysicsConstraintHandle constraint
    ) const;
    std::size_t ConstraintCount() const noexcept;

    bool Contains(PhysicsBodyHandle body) const noexcept;
    std::size_t BodyCount() const noexcept;
    PhysicsWorldStatistics Statistics() const noexcept;

    PhysicsBodyState State(PhysicsBodyHandle body) const;
    PhysicsBodyRuntimeSettings RuntimeSettings(
        PhysicsBodyHandle body
    ) const;
    void ConfigureCcd(
        PhysicsBodyHandle body,
        bool enabled,
        float motionThreshold,
        float sweptSphereRadius
    );
    void ConfigureGravity(
        PhysicsBodyHandle body,
        bool overrideWorldGravity,
        const glm::vec3& gravity
    );
    void SetDamping(
        PhysicsBodyHandle body,
        float linearDamping,
        float angularDamping
    );
    // Temporarily marks a body kinematic (Saba-style reset/warmup). Dynamic
    // bodies keep their mass/inertia but stop integrating until cleared.
    void SetKinematic(PhysicsBodyHandle body, bool kinematic);
    void SetCollisionPairIgnored(
        PhysicsBodyHandle bodyA,
        PhysicsBodyHandle bodyB,
        bool ignored = true
    );
    std::span<const PhysicsContactPair> ContactPairs() const noexcept;
    void SetTransform(
        PhysicsBodyHandle body,
        const glm::vec3& position,
        const glm::quat& rotation,
        bool clearVelocity = false
    );
    void SetLinearVelocity(
        PhysicsBodyHandle body,
        const glm::vec3& velocity
    );
    void SetAngularVelocity(
        PhysicsBodyHandle body,
        const glm::vec3& velocity
    );
    void SetLinearFactor(
        PhysicsBodyHandle body,
        const glm::vec3& factor
    );
    void SetAngularFactor(
        PhysicsBodyHandle body,
        const glm::vec3& factor
    );

    void ApplyCentralImpulse(
        PhysicsBodyHandle body,
        const glm::vec3& impulse
    );
    void ApplyTorqueImpulse(
        PhysicsBodyHandle body,
        const glm::vec3& impulse
    );
    void ClearDynamics(PhysicsBodyHandle body);
    void Activate(PhysicsBodyHandle body);

    void SetDebugDrawEnabled(bool enabled) noexcept;
    bool DebugDrawEnabled() const noexcept;
    std::span<const PhysicsDebugLine> DebugLines() const noexcept;

    // Advances Bullet by exactly one solver tick. Scene owns accumulation and
    // calls this once per WISTERIA fixed substep.
    void StepFixed(float fixedTimeStep);

    // Compatibility helper for tests and low-level callers. It advances one
    // exact tick after applying maxDeltaTime; it does not use Bullet's internal
    // accumulator.
    void Step(float deltaTime);

    // Saba-style step: forwards to Bullet's stepSimulation(timeStep,
    // maxSubSteps, fixedTimeStep), including Bullet's internal accumulator.
    // Returns the number of simulation substeps actually executed.
    int StepSimulation(
        float timeStep,
        int maxSubSteps,
        float fixedTimeStep
    );

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace wisteria
