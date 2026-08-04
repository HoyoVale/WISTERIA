#include "wisteria/physics/physics_world.hpp"

#include "physics_world_impl.hpp"

namespace wisteria
{
void PhysicsWorld::StepFixed(float fixedTimeStep)
{
    if (!std::isfinite(fixedTimeStep) || fixedTimeStep <= 0.0f)
    {
        throw std::invalid_argument(
            "Physics fixed step must be finite and positive"
        );
    }

    // maxSubSteps=0 disables Bullet's internal accumulator. WISTERIA's Scene
    // owns the accumulator so model adapters can update kinematic targets
    // before every individual solver tick.
    impl->lastFixedTimeStep = fixedTimeStep;
    impl->world->stepSimulation(fixedTimeStep, 0, fixedTimeStep);
    impl->CaptureContactPairs();
    if (impl->debugDrawEnabled)
    {
        impl->debugCollector.Clear();
        impl->world->debugDrawWorld();
    }
}

void PhysicsWorld::Step(float deltaTime)
{
    if (!std::isfinite(deltaTime))
        throw std::invalid_argument("Physics deltaTime is non-finite");
    if (deltaTime <= 0.0f)
        return;
    this->StepFixed(std::min(deltaTime, impl->settings.maxDeltaTime));
}

int PhysicsWorld::StepSimulation(
    float timeStep,
    int maxSubSteps,
    float fixedTimeStep
)
{
    if (!std::isfinite(timeStep) || timeStep <= 0.0f)
    {
        throw std::invalid_argument(
            "Physics stepSimulation timeStep must be finite and positive"
        );
    }
    if (maxSubSteps <= 0)
    {
        throw std::invalid_argument(
            "Physics stepSimulation maxSubSteps must be positive"
        );
    }
    if (!std::isfinite(fixedTimeStep) || fixedTimeStep <= 0.0f)
    {
        throw std::invalid_argument(
            "Physics stepSimulation fixedTimeStep must be finite and positive"
        );
    }

    impl->lastFixedTimeStep = fixedTimeStep;
    const int substeps = impl->world->stepSimulation(
        timeStep,
        maxSubSteps,
        fixedTimeStep
    );
    impl->CaptureContactPairs();
    if (impl->debugDrawEnabled)
    {
        impl->debugCollector.Clear();
        impl->world->debugDrawWorld();
    }
    return substeps;
}

}  // namespace wisteria
