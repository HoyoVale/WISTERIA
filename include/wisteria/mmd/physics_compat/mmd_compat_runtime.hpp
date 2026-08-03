#pragma once

#include "wisteria/mmd/physics_compat/mmd_compat_settings.hpp"
#include "wisteria/mmd/physics/mmd_physics_types.hpp"
#include "wisteria/physics/physics_types.hpp"

#include <cstddef>
#include <memory>

class PhysicsWorld;
class Pose;
class Transform;
class MmdPhysicsAsset;

// Core MMD physics runtime modeled after Saba's MMDPhysics/MMDRigidBody/MMDJoint.
// It is format-agnostic about Entity/Scene: it only talks to PhysicsWorld and
// Pose, so it can be unit-tested without a Scene.
class MmdCompatRuntime
{
public:
    MmdCompatRuntime(
        PhysicsWorld& world,
        const MmdPhysicsAsset& asset,
        Pose& pose,
        const Transform& transform,
        const MmdCompatSettings& settings
    );
    ~MmdCompatRuntime();

    MmdCompatRuntime(const MmdCompatRuntime&) = delete;
    MmdCompatRuntime& operator=(const MmdCompatRuntime&) = delete;
    MmdCompatRuntime(MmdCompatRuntime&&) noexcept;
    MmdCompatRuntime& operator=(MmdCompatRuntime&&) noexcept;

    // Creates Bullet bodies/joints from the asset. Returns false on invalid
    // references or Bullet creation failures.
    bool Create();
    void Destroy() noexcept;

    // Writes the current bone pose into kinematic bodies / targets.
    void UpdateFromBones();

    // Advances Bullet by exactly one tick (Scene owns the accumulator).
    void Step(float deltaTime);

    // Writes physics results back into Pose.
    void UpdateBones();

    // Convenience: UpdateFromBones + Step + UpdateBones.
    void Update(float deltaTime);

    void Reset();
    void SetGravity(const glm::vec3& gravity);

    std::size_t RigidBodyCount() const noexcept;
    std::size_t JointCount() const noexcept;
    PhysicsBodyState BodyStateAt(RigidBodyIndex index) const;
    void ApplyCentralImpulse(RigidBodyIndex index, const glm::vec3& impulse);
    void ApplyTorqueImpulse(RigidBodyIndex index, const glm::vec3& impulse);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
