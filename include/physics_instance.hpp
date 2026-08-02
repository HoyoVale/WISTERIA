#pragma once

// Per-Entity bridge between scene simulation and model-specific runtime data.
// Implementations may adapt MMD rigid bodies, a glTF character controller, a
// vehicle, or any future model format without making Entity depend on that
// format's metadata types.
class PhysicsInstance
{
public:
    virtual ~PhysicsInstance() = default;

    PhysicsInstance(const PhysicsInstance&) = delete;
    PhysicsInstance& operator=(const PhysicsInstance&) = delete;
    PhysicsInstance(PhysicsInstance&&) = delete;
    PhysicsInstance& operator=(PhysicsInstance&&) = delete;

    virtual void PrepareSimulation(float deltaTime) = 0;
    virtual void FinishSimulation() = 0;
    virtual void ResetSimulation() = 0;

protected:
    PhysicsInstance() = default;
};
