#include "wisteria/common/pch.hpp"
#include "wisteria/scene/scene.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace
{
template<typename T>
bool RemoveOwnedObject(
    std::vector<std::unique_ptr<T>>& objects,
    const T& object
)
{
    const auto iterator = std::find_if(
        objects.begin(),
        objects.end(),
        [&object](const std::unique_ptr<T>& candidate)
        {
            return candidate.get() == &object;
        }
    );

    if (iterator == objects.end())
        return false;

    objects.erase(iterator);
    return true;
}
}

Scene& Scene::operator=(Scene&& other) noexcept
{
    if (this == &other)
        return *this;

    // Entity physics instances must unregister from the old world before that
    // world is replaced. The incoming PhysicsWorld is heap-owned so moving a
    // Scene preserves the address stored by every incoming instance.
    this->Clear();
    this->activeCamera = std::move(other.activeCamera);
    this->physicsWorld = std::move(other.physicsWorld);
    this->entities = std::move(other.entities);
    this->pointLights = std::move(other.pointLights);
    this->directionalLights = std::move(other.directionalLights);
    this->spotLights = std::move(other.spotLights);
    this->environment = other.environment;
    this->physicsAccumulator = other.physicsAccumulator;
    this->physicsFrameStatistics = other.physicsFrameStatistics;
    other.environment = nullptr;
    other.physicsAccumulator = 0.0;
    other.physicsFrameStatistics = {};
    return *this;
}

Camera& Scene::ActiveCamera() noexcept
{
    return this->activeCamera;
}

const Camera& Scene::ActiveCamera() const noexcept
{
    return this->activeCamera;
}

PhysicsWorld& Scene::Physics() noexcept
{
    return *this->physicsWorld;
}

const PhysicsWorld& Scene::Physics() const noexcept
{
    return *this->physicsWorld;
}

const PhysicsFrameStatistics&
Scene::LastPhysicsFrameStatistics() const noexcept
{
    return this->physicsFrameStatistics;
}

void Scene::Update(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
    {
        throw std::invalid_argument(
            "Scene delta time must be finite and non-negative"
        );
    }

    for (const std::unique_ptr<Entity>& entity : this->entities)
        entity->Update(deltaTime);

    const auto ownsSimulationStep = [](const Entity& entity)
    {
        const PhysicsInstance* instance = entity.TryGetPhysicsInstance();
        return instance != nullptr && instance->OwnsSimulationStep();
    };
    bool hasSharedPhysics = this->physicsWorld->BodyCount() > 0U ||
        this->physicsWorld->ConstraintCount() > 0U;
    for (const std::unique_ptr<Entity>& entity : this->entities)
    {
        const PhysicsInstance* instance = entity->TryGetPhysicsInstance();
        if (instance != nullptr && !instance->OwnsSimulationStep())
            hasSharedPhysics = true;
    }

    const auto physicsBegin = std::chrono::steady_clock::now();
    for (const std::unique_ptr<Entity>& entity : this->entities)
    {
        if (ownsSimulationStep(*entity))
            continue;
        entity->PrePhysicsUpdate(deltaTime);
    }

    struct PendingStabilization
    {
        PhysicsInstance* instance = nullptr;
        PhysicsStabilizationRequest request{};
    };
    std::vector<PendingStabilization> pending;
    pending.reserve(this->entities.size());
    std::size_t maximumStabilizationSteps = 0U;
    float stabilizationStep = 1.0f / 60.0f;
    bool hasStabilizationStep = false;
    for (const std::unique_ptr<Entity>& entity : this->entities)
    {
        PhysicsInstance* instance = entity->TryGetPhysicsInstance();
        if (instance == nullptr || instance->OwnsSimulationStep())
            continue;
        const PhysicsStabilizationRequest request =
            instance->StabilizationRequest();
        if (request.steps == 0U)
            continue;
        if (!std::isfinite(request.fixedTimeStep) ||
            request.fixedTimeStep <= 0.0f)
        {
            throw std::invalid_argument(
                "Physics stabilization time step must be finite and positive"
            );
        }
        maximumStabilizationSteps = std::max(
            maximumStabilizationSteps,
            request.steps
        );
        stabilizationStep = hasStabilizationStep
            ? std::min(stabilizationStep, request.fixedTimeStep)
            : request.fixedTimeStep;
        hasStabilizationStep = true;
        pending.push_back(PendingStabilization{instance, request});
    }

    // Hidden settling remains separate from the real-time accumulator. It is
    // deterministic setup work and must not consume or create render-frame
    // catch-up debt.
    for (std::size_t step = 0U;
         step < maximumStabilizationSteps;
         ++step)
    {
        for (const PendingStabilization& item : pending)
        {
            if (step < item.request.steps)
                item.instance->PrepareStabilizationStep(stabilizationStep);
        }
        if (hasSharedPhysics)
            this->physicsWorld->StepFixed(stabilizationStep);
        for (const PendingStabilization& item : pending)
        {
            if (step < item.request.steps)
                item.instance->ObserveStabilizationStep(step + 1U);
        }
    }
    for (const PendingStabilization& item : pending)
        item.instance->CompleteStabilization();

    const PhysicsStepSettings& settings =
        this->physicsWorld->StepSettings();
    const double fixedTimeStep =
        static_cast<double>(settings.fixedTimeStep);
    const double maximumFrameDelta =
        static_cast<double>(settings.maxDeltaTime);
    const double inputDelta = static_cast<double>(deltaTime);
    const double safeFrameDelta = std::min(inputDelta, maximumFrameDelta);
    double droppedTime = std::max(0.0, inputDelta - safeFrameDelta);

    const double accumulatorAtFrameStart = this->physicsAccumulator;
    this->physicsAccumulator += safeFrameDelta;
    const double maximumAccumulatedTime = fixedTimeStep *
        static_cast<double>(settings.maxSubSteps);
    if (this->physicsAccumulator > maximumAccumulatedTime)
    {
        droppedTime += this->physicsAccumulator - maximumAccumulatedTime;
        this->physicsAccumulator = maximumAccumulatedTime;
    }

    const double stepTolerance = fixedTimeStep * 0.000001;
    std::size_t substepCount = static_cast<std::size_t>(std::floor(
        (this->physicsAccumulator + stepTolerance) / fixedTimeStep
    ));
    substepCount = std::min(
        substepCount,
        static_cast<std::size_t>(settings.maxSubSteps)
    );

    for (std::size_t step = 0U; step < substepCount; ++step)
    {
        float alpha = 1.0f;
        if (droppedTime > 0.0 || safeFrameDelta <= stepTolerance)
        {
            // A clamped catch-up frame intentionally spans the complete
            // render-frame target over the bounded number of fixed ticks.
            // This avoids both a one-tick teleport and leaving kinematic
            // bodies permanently behind an animation timeline we dropped.
            alpha = static_cast<float>(step + 1U) /
                static_cast<float>(substepCount);
        }
        else
        {
            // The accumulator may already contain a fractional fixed tick.
            // Sample the animation at the exact tick time inside this render
            // interval instead of distributing ticks uniformly. At 144 FPS,
            // for example, the first 60 Hz tick lands partway through the
            // third render frame rather than at that frame's endpoint.
            const double timeInsideFrame =
                fixedTimeStep * static_cast<double>(step + 1U) -
                accumulatorAtFrameStart;
            alpha = static_cast<float>(std::clamp(
                timeInsideFrame / safeFrameDelta,
                0.0,
                1.0
            ));
        }
        for (const std::unique_ptr<Entity>& entity : this->entities)
        {
            if (ownsSimulationStep(*entity))
                continue;
            entity->PreparePhysicsSubstep(
                alpha,
                settings.fixedTimeStep
            );
        }
        if (hasSharedPhysics)
            this->physicsWorld->StepFixed(settings.fixedTimeStep);
        for (const std::unique_ptr<Entity>& entity : this->entities)
        {
            if (ownsSimulationStep(*entity))
                continue;
            entity->ObservePhysicsSubstep(settings.fixedTimeStep);
        }
        this->physicsAccumulator -= fixedTimeStep;
    }
    if (this->physicsAccumulator < stepTolerance)
        this->physicsAccumulator = 0.0;

    for (const std::unique_ptr<Entity>& entity : this->entities)
    {
        if (ownsSimulationStep(*entity))
            continue;
        entity->PostPhysicsUpdate();
    }
    for (const std::unique_ptr<Entity>& entity : this->entities)
    {
        if (ownsSimulationStep(*entity))
            continue;
        entity->SolveAfterPhysicsPose();
    }

    const auto physicsEnd = std::chrono::steady_clock::now();
    this->physicsFrameStatistics.frameDeltaTime = deltaTime;
    this->physicsFrameStatistics.simulatedDeltaTime = static_cast<float>(
        fixedTimeStep * static_cast<double>(substepCount)
    );
    this->physicsFrameStatistics.fixedTimeStep = settings.fixedTimeStep;
    this->physicsFrameStatistics.accumulatorTime = static_cast<float>(
        this->physicsAccumulator
    );
    this->physicsFrameStatistics.droppedTime =
        static_cast<float>(droppedTime);
    this->physicsFrameStatistics.physicsCpuMilliseconds =
        std::chrono::duration<double, std::milli>(
            physicsEnd - physicsBegin
        ).count();
    this->physicsFrameStatistics.substepCount = substepCount;
    this->physicsFrameStatistics.stabilizationSubstepCount =
        maximumStabilizationSteps;
    this->physicsFrameStatistics.catchUpLimited = droppedTime > 0.0;
    this->physicsFrameStatistics.world =
        this->physicsWorld->Statistics();
}

void Scene::Clear() noexcept
{
    this->physicsAccumulator = 0.0;
    this->physicsFrameStatistics = {};
    this->ClearEntities();
    this->ClearPointLights();
    this->ClearDirectionalLights();
    this->ClearSpotLights();
    this->ClearEnvironment();
}

Entity& Scene::CreateEntity(const Transform& transform)
{
    auto entity = std::make_unique<Entity>(transform);
    Entity& result = *entity;
    this->entities.emplace_back(std::move(entity));
    return result;
}

Entity& Scene::CreateEntity(
    Mesh& mesh,
    Material& material,
    const Transform& transform
)
{
    auto entity = std::make_unique<Entity>(mesh, material, transform);
    Entity& result = *entity;
    this->entities.emplace_back(std::move(entity));
    return result;
}

Entity& Scene::InstantiateModel(
    const ModelAsset& model,
    const Transform& transform,
    const ModelInstantiationOptions& options
)
{
    Entity& entity = this->CreateEntity(transform);
    if (model.HasMorphs())
        entity.SetMorphSet(model.GetMorphSet());
    if (model.HasSkeleton())
    {
        entity.SetSkeleton(model.GetSkeleton());
        if (model.AnimationClipCount() > 0)
            entity.GetAnimator().Play(model.AnimationClipAt(0));
    }
    if (options.enablePhysics && model.HasMmdPhysics())
        entity.SetMmdPhysics(*this->physicsWorld, model.GetMmdPhysics());
    for (const RenderPart& part : model.Parts())
    {
        entity.AddRenderPart(
            part.GetMesh(),
            part.GetMaterial(),
            part.LocalTransform(),
            part.MorphMaterialIndex()
        );
    }
    return entity;
}

bool Scene::RemoveEntity(const Entity& entity)
{
    return RemoveOwnedObject(this->entities, entity);
}

void Scene::ClearEntities() noexcept
{
    this->entities.clear();
}

PointLight& Scene::CreatePointLight(const PointLightData& data)
{
    auto light = std::make_unique<PointLight>(data);
    PointLight& result = *light;
    this->pointLights.emplace_back(std::move(light));
    return result;
}

bool Scene::RemovePointLight(const PointLight& light)
{
    return RemoveOwnedObject(this->pointLights, light);
}

void Scene::ClearPointLights() noexcept
{
    this->pointLights.clear();
}

DirectionalLight& Scene::CreateDirectionalLight(
    const DirectionalLightData& data
)
{
    auto light = std::make_unique<DirectionalLight>(data);
    DirectionalLight& result = *light;
    this->directionalLights.emplace_back(std::move(light));
    return result;
}

bool Scene::RemoveDirectionalLight(const DirectionalLight& light)
{
    return RemoveOwnedObject(this->directionalLights, light);
}

void Scene::ClearDirectionalLights() noexcept
{
    this->directionalLights.clear();
}

SpotLight& Scene::CreateSpotLight(const SpotLightData& data)
{
    auto light = std::make_unique<SpotLight>(data);
    SpotLight& result = *light;
    this->spotLights.emplace_back(std::move(light));
    return result;
}

bool Scene::RemoveSpotLight(const SpotLight& light)
{
    return RemoveOwnedObject(this->spotLights, light);
}

void Scene::ClearSpotLights() noexcept
{
    this->spotLights.clear();
}

void Scene::SetEnvironment(EnvironmentMap* environment) noexcept
{
    this->environment = environment;
}

void Scene::ClearEnvironment() noexcept
{
    this->environment = nullptr;
}

EnvironmentMap* Scene::Environment() noexcept
{
    return this->environment;
}

const EnvironmentMap* Scene::Environment() const noexcept
{
    return this->environment;
}

std::size_t Scene::EntityCount() const noexcept
{
    return this->entities.size();
}

Entity* Scene::EntityAt(std::size_t index) noexcept
{
    return index < this->entities.size() ? this->entities[index].get() : nullptr;
}

const Entity* Scene::EntityAt(std::size_t index) const noexcept
{
    return index < this->entities.size() ? this->entities[index].get() : nullptr;
}

const std::vector<std::unique_ptr<Entity>>& Scene::Entities() const noexcept
{
    return this->entities;
}

const std::vector<std::unique_ptr<PointLight>>& Scene::PointLights() const noexcept
{
    return this->pointLights;
}

const std::vector<std::unique_ptr<DirectionalLight>>& Scene::DirectionalLights() const noexcept
{
    return this->directionalLights;
}

const std::vector<std::unique_ptr<SpotLight>>& Scene::SpotLights() const noexcept
{
    return this->spotLights;
}
