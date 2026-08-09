#include "wisteria/common/pch.hpp"
#include "wisteria/scene/entity.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/physics/physics_world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wisteria
{
Entity::Entity(const Transform& transform)
    : transform(transform)
{
}

Entity::Entity(
    Mesh& mesh,
    Material& material,
    const Transform& transform
)
    : Entity(transform)
{
    this->AddRenderPart(mesh, material);
}

Entity::~Entity()
{
    this->ClearBehaviours();
    this->ClearRenderParts();
    this->modelInstance.reset();
    this->physicsInstance.reset();
}

Transform& Entity::GetTransform() noexcept
{
    return this->transform;
}

const Transform& Entity::GetTransform() const noexcept
{
    return this->transform;
}

Mesh& Entity::GetMesh()
{
    if (this->renderParts.empty())
        throw std::logic_error("Entity has no render parts");
    return this->renderParts.front().GetMesh();
}

const Mesh& Entity::GetMesh() const
{
    if (this->renderParts.empty())
        throw std::logic_error("Entity has no render parts");
    return this->renderParts.front().GetMesh();
}

void Entity::SetMesh(Mesh& mesh)
{
    if (this->renderParts.empty())
        throw std::logic_error("Entity has no render parts");
    this->renderParts.front().SetMesh(mesh);
}

Material& Entity::GetMaterial()
{
    if (this->renderParts.empty())
        throw std::logic_error("Entity has no render parts");
    return this->renderParts.front().GetMaterial();
}

const Material& Entity::GetMaterial() const
{
    if (this->renderParts.empty())
        throw std::logic_error("Entity has no render parts");
    return this->renderParts.front().GetMaterial();
}

void Entity::SetMaterial(Material& material)
{
    if (this->renderParts.empty())
        throw std::logic_error("Entity has no render parts");
    this->renderParts.front().SetMaterial(material);
}

RenderPart& Entity::AddRenderPart(
    Mesh& mesh,
    Material& material,
    const glm::mat4& localTransform,
    std::optional<std::uint32_t> morphMaterialIndex
)
{
    return this->renderParts.emplace_back(
        mesh,
        material,
        localTransform,
        morphMaterialIndex
    );
}

bool Entity::RemoveRenderPart(const RenderPart& part)
{
    const auto iterator = std::find_if(
        this->renderParts.begin(),
        this->renderParts.end(),
        [&part](const RenderPart& candidate)
        {
            return &candidate == &part;
        }
    );
    if (iterator == this->renderParts.end())
        return false;

    this->renderParts.erase(iterator);
    return true;
}

void Entity::ClearRenderParts() noexcept
{
    this->renderParts.clear();
}

std::size_t Entity::RenderPartCount() const noexcept
{
    return this->renderParts.size();
}

std::span<RenderPart> Entity::RenderParts() noexcept
{
    return this->renderParts;
}

std::span<const RenderPart> Entity::RenderParts() const noexcept
{
    return this->renderParts;
}

bool Entity::IsVisible() const noexcept
{
    return this->visible;
}

void Entity::SetVisible(bool visible) noexcept
{
    this->visible = visible;
}

bool Entity::HasModelInstance() const noexcept
{
    return this->modelInstance != nullptr;
}

ModelInstance* Entity::TryGetModelInstance() noexcept
{
    return this->modelInstance.get();
}

const ModelInstance* Entity::TryGetModelInstance() const noexcept
{
    return this->modelInstance.get();
}

ModelInstance& Entity::GetModelInstance()
{
    if (this->modelInstance == nullptr)
        throw std::logic_error("Entity has no model instance");
    return *this->modelInstance;
}

const ModelInstance& Entity::GetModelInstance() const
{
    if (this->modelInstance == nullptr)
        throw std::logic_error("Entity has no model instance");
    return *this->modelInstance;
}

void Entity::SetModelInstance(std::unique_ptr<ModelInstance> instance)
{
    if (instance == nullptr)
        throw std::invalid_argument("Entity model instance must not be null");
    if (this->modelInstance != nullptr)
        throw std::logic_error("Entity model instance is already set");
    this->modelInstance = std::move(instance);
}

std::unique_ptr<ModelInstance> Entity::TakeModelInstance()
{
    return std::move(this->modelInstance);
}

bool Entity::HasPose() const noexcept
{
    return this->TryGetPose() != nullptr;
}

Pose* Entity::TryGetPose() noexcept
{
    if (this->modelInstance != nullptr)
    {
        IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
            return runtime->TryGetPose();
    }
    return this->pose.get();
}

const Pose* Entity::TryGetPose() const noexcept
{
    if (this->modelInstance != nullptr)
    {
        const IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
            return runtime->TryGetPose();
    }
    return this->pose.get();
}

Pose& Entity::GetPose()
{
    Pose* result = this->TryGetPose();
    if (result == nullptr)
        throw std::logic_error("Entity has no skeleton pose");
    return *result;
}

const Pose& Entity::GetPose() const
{
    const Pose* result = this->TryGetPose();
    if (result == nullptr)
        throw std::logic_error("Entity has no skeleton pose");
    return *result;
}

void Entity::SetSkeleton(const Skeleton& skeleton)
{
    if (this->pose != nullptr)
        throw std::logic_error("Entity skeleton is already set");
    auto nextPose = std::make_unique<Pose>(skeleton);
    auto nextAnimator = std::make_unique<Animator>(
        *nextPose,
        this->morphState.get()
    );
    this->pose = std::move(nextPose);
    this->animator = std::move(nextAnimator);
}

bool Entity::HasAnimator() const noexcept
{
    return this->TryGetAnimator() != nullptr;
}

Animator* Entity::TryGetAnimator() noexcept
{
    if (this->modelInstance != nullptr)
    {
        IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
            return runtime->TryGetAnimator();
    }
    return this->animator.get();
}

const Animator* Entity::TryGetAnimator() const noexcept
{
    if (this->modelInstance != nullptr)
    {
        const IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
            return runtime->TryGetAnimator();
    }
    return this->animator.get();
}

Animator& Entity::GetAnimator()
{
    Animator* result = this->TryGetAnimator();
    if (result == nullptr)
        throw std::logic_error("Entity has no skeleton animator");
    return *result;
}

const Animator& Entity::GetAnimator() const
{
    const Animator* result = this->TryGetAnimator();
    if (result == nullptr)
        throw std::logic_error("Entity has no skeleton animator");
    return *result;
}

bool Entity::HasMorphState() const noexcept
{
    return this->TryGetMorphState() != nullptr;
}

MorphState* Entity::TryGetMorphState() noexcept
{
    if (this->modelInstance != nullptr)
    {
        IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
            return runtime->TryGetMorphState();
    }
    return this->morphState.get();
}

const MorphState* Entity::TryGetMorphState() const noexcept
{
    if (this->modelInstance != nullptr)
    {
        const IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
            return runtime->TryGetMorphState();
    }
    return this->morphState.get();
}

MorphState& Entity::GetMorphState()
{
    MorphState* result = this->TryGetMorphState();
    if (result == nullptr)
        throw std::logic_error("Entity has no morph state");
    return *result;
}

const MorphState& Entity::GetMorphState() const
{
    const MorphState* result = this->TryGetMorphState();
    if (result == nullptr)
        throw std::logic_error("Entity has no morph state");
    return *result;
}

void Entity::SetMorphSet(const MorphSet& morphSet)
{
    if (this->morphState != nullptr)
        throw std::logic_error("Entity morph state is already set");
    this->morphState = std::make_unique<MorphState>(morphSet);
    if (this->animator != nullptr)
        this->animator->SetMorphState(*this->morphState);
}

bool Entity::HasPhysicsInstance() const noexcept
{
    return this->TryGetPhysicsInstance() != nullptr;
}

PhysicsInstance* Entity::TryGetPhysicsInstance() noexcept
{
    if (this->modelInstance != nullptr)
    {
        IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
        {
            if (PhysicsInstance* physics = runtime->TryGetPhysicsInstance())
                return physics;
        }
    }
    return this->physicsInstance.get();
}

const PhysicsInstance* Entity::TryGetPhysicsInstance() const noexcept
{
    if (this->modelInstance != nullptr)
    {
        const IModelRuntimeDriver* runtime =
            this->modelInstance->TryGetRuntime();
        if (runtime != nullptr)
        {
            if (const PhysicsInstance* physics =
                runtime->TryGetPhysicsInstance())
            {
                return physics;
            }
        }
    }
    return this->physicsInstance.get();
}

PhysicsInstance& Entity::GetPhysicsInstance()
{
    PhysicsInstance* result = this->TryGetPhysicsInstance();
    if (result == nullptr)
        throw std::logic_error("Entity has no physics instance");
    return *result;
}

const PhysicsInstance& Entity::GetPhysicsInstance() const
{
    const PhysicsInstance* result = this->TryGetPhysicsInstance();
    if (result == nullptr)
        throw std::logic_error("Entity has no physics instance");
    return *result;
}

void Entity::SetPhysicsInstance(std::unique_ptr<PhysicsInstance> instance)
{
    if (instance == nullptr)
        throw std::invalid_argument("Entity physics instance must not be null");
    if (this->HasPhysicsInstance())
        throw std::logic_error("Entity physics instance is already set");
    this->physicsInstance = std::move(instance);
}

void Entity::PrePhysicsUpdate(float deltaTime)
{
    PhysicsInstance* physics = this->TryGetPhysicsInstance();
    if (physics == nullptr)
        return;
    if (this->physicsResetPending)
    {
        physics->ResetSimulation();
        this->physicsResetPending = false;
    }
    physics->PrepareSimulation(deltaTime);
}

void Entity::PreparePhysicsSubstep(
    float alpha,
    float fixedTimeStep
)
{
    if (PhysicsInstance* physics = this->TryGetPhysicsInstance())
        physics->PrepareSimulationSubstep(alpha, fixedTimeStep);
}

void Entity::ObservePhysicsSubstep(float fixedTimeStep)
{
    if (PhysicsInstance* physics = this->TryGetPhysicsInstance())
        physics->ObserveSimulationSubstep(fixedTimeStep);
}

void Entity::PostPhysicsUpdate()
{
    if (PhysicsInstance* physics = this->TryGetPhysicsInstance())
        physics->FinishSimulation();
}

void Entity::SolveAfterPhysicsPose()
{
    // R1.5 Phase 0D: route through TryGetAnimator so a runtime-owned Generic
    // Animator participates in the after-physics pass; standalone Entities
    // still reach their legacy Animator, Saba keeps returning nullptr.
    if (Animator* animator = this->TryGetAnimator())
        animator->SolveAfterPhysics();
}

void Entity::ResetPhysicsToCurrentPose()
{
    if (PhysicsInstance* physics = this->TryGetPhysicsInstance())
    {
        physics->ResetSimulation();
        this->physicsResetPending = false;
    }
}

void Entity::AppendPhysicsDebugLines(
    std::vector<PhysicsDebugLine>& lines
)
{
    if (const PhysicsInstance* physics = this->TryGetPhysicsInstance())
        physics->AppendDebugLines(lines);
}

bool Entity::RemoveBehaviour(Behaviour& behaviour)
{
    const auto iterator = std::find_if(
        this->behaviours.begin(),
        this->behaviours.end(),
        [&behaviour](const std::unique_ptr<Behaviour>& candidate)
        {
            return candidate.get() == &behaviour;
        }
    );

    if (iterator == this->behaviours.end())
        return false;

    if (this->processingBehaviours)
    {
        behaviour.SetEnabled(false);
        if (std::find(
                this->pendingBehaviourRemovals.begin(),
                this->pendingBehaviourRemovals.end(),
                &behaviour
            ) == this->pendingBehaviourRemovals.end())
        {
            this->pendingBehaviourRemovals.push_back(&behaviour);
        }
        return true;
    }

    behaviour.OnDetach(*this);
    this->behaviours.erase(iterator);
    return true;
}

void Entity::ClearBehaviours() noexcept
{
    if (this->processingBehaviours)
    {
        this->clearBehavioursRequested = true;
        for (const std::unique_ptr<Behaviour>& behaviour : this->behaviours)
            behaviour->SetEnabled(false);
        return;
    }

    for (const std::unique_ptr<Behaviour>& behaviour : this->behaviours)
        behaviour->OnDetach(*this);
    this->behaviours.clear();
    this->pendingBehaviourRemovals.clear();
    this->clearBehavioursRequested = false;
}

std::size_t Entity::BehaviourCount() const noexcept
{
    return this->behaviours.size();
}

void Entity::Update(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        throw std::invalid_argument("Entity delta time must be finite and non-negative");
    if (this->modelInstance != nullptr &&
        this->modelInstance->HasRuntime())
    {
        const RootMotionDelta rootMotion =
            this->modelInstance->Update(deltaTime);
        if (!rootMotion.IsIdentity())
            this->transform.ApplyLocalMotion(rootMotion);
    }
    else if (this->animator != nullptr)
    {
        this->animator->Update(deltaTime);
        const std::uint64_t discontinuity =
            this->animator->DiscontinuityRevision();
        if (discontinuity != this->observedAnimatorDiscontinuityRevision)
        {
            this->observedAnimatorDiscontinuityRevision = discontinuity;
            this->physicsResetPending = this->HasPhysicsInstance();
        }
        const RootMotionDelta rootMotion =
            this->animator->ConsumeRootMotion();
        if (!rootMotion.IsIdentity())
            this->transform.ApplyLocalMotion(rootMotion);
    }
    this->UpdateBehaviours(deltaTime);
}

void Entity::UpdateBehaviours(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        throw std::invalid_argument("Entity delta time must be finite and non-negative");

    if (this->processingBehaviours)
        throw std::logic_error("Entity behaviour processing cannot be nested");

    this->processingBehaviours = true;
    const std::size_t count = this->behaviours.size();
    try
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            Behaviour& behaviour = *this->behaviours[index];
            if (behaviour.IsEnabled())
                behaviour.Update(*this, deltaTime);
        }
    }
    catch (...)
    {
        this->processingBehaviours = false;
        this->FlushPendingBehaviourRemovals();
        throw;
    }

    this->processingBehaviours = false;
    this->FlushPendingBehaviourRemovals();
}

void Entity::DispatchEvent(const Event& event)
{
    if (this->processingBehaviours)
        throw std::logic_error("Entity behaviour processing cannot be nested");

    this->processingBehaviours = true;
    const std::size_t count = this->behaviours.size();
    try
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            Behaviour& behaviour = *this->behaviours[index];
            if (behaviour.IsEnabled())
                behaviour.OnEvent(*this, event);
        }
    }
    catch (...)
    {
        this->processingBehaviours = false;
        this->FlushPendingBehaviourRemovals();
        throw;
    }

    this->processingBehaviours = false;
    this->FlushPendingBehaviourRemovals();
}

void Entity::FlushPendingBehaviourRemovals() noexcept
{
    if (this->clearBehavioursRequested)
    {
        this->ClearBehaviours();
        return;
    }

    for (Behaviour* pending : this->pendingBehaviourRemovals)
    {
        const auto iterator = std::find_if(
            this->behaviours.begin(),
            this->behaviours.end(),
            [pending](const std::unique_ptr<Behaviour>& candidate)
            {
                return candidate.get() == pending;
            }
        );
        if (iterator != this->behaviours.end())
        {
            (*iterator)->OnDetach(*this);
            this->behaviours.erase(iterator);
        }
    }
    this->pendingBehaviourRemovals.clear();
}
}  // namespace wisteria
