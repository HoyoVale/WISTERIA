#pragma once

#include "physics_types.hpp"
#include <cstddef>
#include <memory>
#include <span>

// WISTERIA-facing rigid-body world. Bullet types are intentionally hidden
// behind the implementation boundary so engine code does not depend on bt*.
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
    std::size_t ConstraintCount() const noexcept;

    bool Contains(PhysicsBodyHandle body) const noexcept;
    std::size_t BodyCount() const noexcept;

    PhysicsBodyState State(PhysicsBodyHandle body) const;
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

    void Step(float deltaTime);

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
