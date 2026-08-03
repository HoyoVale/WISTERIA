#pragma once

#include "wisteria/physics/physics_types.hpp"
#include <cstddef>
#include <vector>

struct PhysicsStabilizationRequest
{
    std::size_t steps = 0U;
    float fixedTimeStep = 1.0f / 60.0f;
};

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

    // Called immediately before every Scene-owned fixed substep. The alpha
    // value progresses from 0 to 1 across the substeps performed for the
    // current render frame, allowing animation-driven bodies to move smoothly
    // instead of teleporting to the final frame target before Bullet runs.
    virtual void PrepareSimulationSubstep(
        float alpha,
        float fixedTimeStep
    )
    {
        (void)alpha;
        (void)fixedTimeStep;
    }

    // Called immediately after each Scene-owned real-time fixed step. This is
    // intentionally separate from FinishSimulation(), which runs once per
    // render frame even when no physics tick occurred. Runtime health checks
    // and other time-based physics bookkeeping belong here.
    virtual void ObserveSimulationSubstep(float fixedTimeStep)
    {
        (void)fixedTimeStep;
    }

    virtual void FinishSimulation() = 0;
    virtual void ResetSimulation() = 0;

    // Optional scene-level hidden stabilization. The Scene owns world stepping;
    // model adapters only describe how many fixed steps they need and how to
    // keep their animation-driven bodies fixed during those steps. This avoids
    // a model-specific instance secretly advancing the shared PhysicsWorld.
    virtual PhysicsStabilizationRequest StabilizationRequest() const noexcept
    {
        return {};
    }

    virtual void PrepareStabilizationStep(float fixedTimeStep)
    {
        (void)fixedTimeStep;
    }

    virtual void ObserveStabilizationStep(std::size_t completedSteps)
    {
        (void)completedSteps;
    }

    virtual void CompleteStabilization()
    {
    }

    // Optional model-specific diagnostic geometry. The scene renderer consumes
    // the common PhysicsDebugLine format without knowing whether the provider
    // is MMD, glTF, a vehicle, or another future physics adapter.
    virtual void AppendDebugLines(
        std::vector<PhysicsDebugLine>& lines
    ) const
    {
        (void)lines;
    }

protected:
    PhysicsInstance() = default;
};
