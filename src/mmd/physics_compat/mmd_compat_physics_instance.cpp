#include "wisteria/mmd/physics_compat/mmd_compat_physics_instance.hpp"

#include <stdexcept>

MmdCompatPhysicsInstance::MmdCompatPhysicsInstance(
    PhysicsWorld& world,
    const MmdPhysicsAsset& asset,
    Pose& pose,
    Transform& transform,
    const MmdCompatSettings& settings
)
    : runtime(world, asset, pose, transform, settings)
{
    if (!this->runtime.Create())
    {
        throw std::runtime_error(
            "MmdCompatPhysicsInstance failed to create Bullet resources"
        );
    }
}

MmdCompatPhysicsInstance::~MmdCompatPhysicsInstance() = default;

void MmdCompatPhysicsInstance::PrepareSimulation(float deltaTime)
{
    this->runtime.UpdateFromBones();
    (void)deltaTime;
}

void MmdCompatPhysicsInstance::PrepareSimulationSubstep(
    float alpha,
    float fixedTimeStep
)
{
    // v1 keeps Saba's non-interpolated kinematic sync; Scene still advances
    // the shared world once per fixed tick.
    (void)alpha;
    (void)fixedTimeStep;
}

void MmdCompatPhysicsInstance::FinishSimulation()
{
    this->runtime.UpdateBones();
}

void MmdCompatPhysicsInstance::ResetSimulation()
{
    this->runtime.Reset();
}

std::size_t MmdCompatPhysicsInstance::RigidBodyCount() const noexcept
{
    return this->runtime.RigidBodyCount();
}

std::size_t MmdCompatPhysicsInstance::JointCount() const noexcept
{
    return this->runtime.JointCount();
}

PhysicsBodyState MmdCompatPhysicsInstance::BodyStateAt(
    RigidBodyIndex index
) const
{
    return this->runtime.BodyStateAt(index);
}

MmdCompatJointDiagnostics MmdCompatPhysicsInstance::JointDiagnostics() const
{
    return this->runtime.JointDiagnostics();
}

void MmdCompatPhysicsInstance::ApplyCentralImpulse(
    RigidBodyIndex index,
    const glm::vec3& impulse
)
{
    this->runtime.ApplyCentralImpulse(index, impulse);
}

void MmdCompatPhysicsInstance::ApplyTorqueImpulse(
    RigidBodyIndex index,
    const glm::vec3& impulse
)
{
    this->runtime.ApplyTorqueImpulse(index, impulse);
}
