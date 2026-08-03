#pragma once

#include "wisteria/mmd/physics_compat/mmd_compat_runtime.hpp"
#include "wisteria/mmd/physics_compat/mmd_compat_settings.hpp"
#include "wisteria/mmd/physics/mmd_physics_types.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/physics/physics_types.hpp"

#include <cstddef>

class PhysicsWorld;
class Pose;
class Transform;
class MmdPhysicsAsset;

// Thin PhysicsInstance adapter that lets the Scene drive MmdCompatRuntime
// through the existing Entity lifecycle.
class MmdCompatPhysicsInstance final : public PhysicsInstance
{
public:
    MmdCompatPhysicsInstance(
        PhysicsWorld& world,
        const MmdPhysicsAsset& asset,
        Pose& pose,
        Transform& transform,
        const MmdCompatSettings& settings = {}
    );
    ~MmdCompatPhysicsInstance() override;

    MmdCompatPhysicsInstance(const MmdCompatPhysicsInstance&) = delete;
    MmdCompatPhysicsInstance& operator=(const MmdCompatPhysicsInstance&) = delete;

    void PrepareSimulation(float deltaTime) override;
    void PrepareSimulationSubstep(float alpha, float fixedTimeStep) override;
    void FinishSimulation() override;
    void ResetSimulation() override;

    std::size_t RigidBodyCount() const noexcept;
    std::size_t JointCount() const noexcept;
    PhysicsBodyState BodyStateAt(RigidBodyIndex index) const;
    void ApplyCentralImpulse(RigidBodyIndex index, const glm::vec3& impulse);
    void ApplyTorqueImpulse(RigidBodyIndex index, const glm::vec3& impulse);

private:
    MmdCompatRuntime runtime;
};
