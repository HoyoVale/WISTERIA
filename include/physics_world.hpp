#pragma once

#include "physics_types.hpp"
#include <cstddef>
#include <memory>

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

    void ApplyCentralImpulse(
        PhysicsBodyHandle body,
        const glm::vec3& impulse
    );
    void ApplyTorqueImpulse(
        PhysicsBodyHandle body,
        const glm::vec3& impulse
    );
    void Activate(PhysicsBodyHandle body);

    void Step(float deltaTime);

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
