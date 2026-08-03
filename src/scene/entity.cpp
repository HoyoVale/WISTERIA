#include "wisteria/common/pch.hpp"
#include "wisteria/scene/entity.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/physics/physics_world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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

bool Entity::HasPose() const noexcept
{
    return this->pose != nullptr;
}

Pose* Entity::TryGetPose() noexcept
{
    return this->pose.get();
}

const Pose* Entity::TryGetPose() const noexcept
{
    return this->pose.get();
}

Pose& Entity::GetPose()
{
    if (this->pose == nullptr)
        throw std::logic_error("Entity has no skeleton pose");
    return *this->pose;
}

const Pose& Entity::GetPose() const
{
    if (this->pose == nullptr)
        throw std::logic_error("Entity has no skeleton pose");
    return *this->pose;
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
    return this->animator != nullptr;
}

Animator* Entity::TryGetAnimator() noexcept
{
    return this->animator.get();
}

const Animator* Entity::TryGetAnimator() const noexcept
{
    return this->animator.get();
}

Animator& Entity::GetAnimator()
{
    if (this->animator == nullptr)
        throw std::logic_error("Entity has no skeleton animator");
    return *this->animator;
}

const Animator& Entity::GetAnimator() const
{
    if (this->animator == nullptr)
        throw std::logic_error("Entity has no skeleton animator");
    return *this->animator;
}

bool Entity::HasMorphState() const noexcept
{
    return this->morphState != nullptr;
}

MorphState* Entity::TryGetMorphState() noexcept
{
    return this->morphState.get();
}

const MorphState* Entity::TryGetMorphState() const noexcept
{
    return this->morphState.get();
}

MorphState& Entity::GetMorphState()
{
    if (this->morphState == nullptr)
        throw std::logic_error("Entity has no morph state");
    return *this->morphState;
}

const MorphState& Entity::GetMorphState() const
{
    if (this->morphState == nullptr)
        throw std::logic_error("Entity has no morph state");
    return *this->morphState;
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
    return this->physicsInstance != nullptr;
}

PhysicsInstance* Entity::TryGetPhysicsInstance() noexcept
{
    return this->physicsInstance.get();
}

const PhysicsInstance* Entity::TryGetPhysicsInstance() const noexcept
{
    return this->physicsInstance.get();
}

PhysicsInstance& Entity::GetPhysicsInstance()
{
    if (this->physicsInstance == nullptr)
        throw std::logic_error("Entity has no physics instance");
    return *this->physicsInstance;
}

const PhysicsInstance& Entity::GetPhysicsInstance() const
{
    if (this->physicsInstance == nullptr)
        throw std::logic_error("Entity has no physics instance");
    return *this->physicsInstance;
}

void Entity::SetPhysicsInstance(std::unique_ptr<PhysicsInstance> instance)
{
    if (instance == nullptr)
        throw std::invalid_argument("Entity physics instance must not be null");
    if (this->physicsInstance != nullptr)
        throw std::logic_error("Entity physics instance is already set");
    this->physicsInstance = std::move(instance);
}

void Entity::PrePhysicsUpdate(float deltaTime)
{
    if (this->physicsInstance == nullptr)
        return;
    if (this->physicsResetPending)
    {
        this->physicsInstance->ResetSimulation();
        this->physicsResetPending = false;
    }
    this->physicsInstance->PrepareSimulation(deltaTime);
}

void Entity::PreparePhysicsSubstep(
    float alpha,
    float fixedTimeStep
)
{
    if (this->physicsInstance != nullptr)
    {
        this->physicsInstance->PrepareSimulationSubstep(
            alpha,
            fixedTimeStep
        );
    }
}

void Entity::ObservePhysicsSubstep(float fixedTimeStep)
{
    if (this->physicsInstance != nullptr)
        this->physicsInstance->ObserveSimulationSubstep(fixedTimeStep);
}

void Entity::PostPhysicsUpdate()
{
    if (this->physicsInstance != nullptr)
        this->physicsInstance->FinishSimulation();
}

void Entity::SolveAfterPhysicsPose()
{
    if (this->animator != nullptr)
        this->animator->SolveAfterPhysics();
}

void Entity::ResetPhysicsToCurrentPose()
{
    if (this->physicsInstance != nullptr)
    {
        this->physicsInstance->ResetSimulation();
        this->physicsResetPending = false;
    }
}

void Entity::AppendPhysicsDebugLines(
    std::vector<PhysicsDebugLine>& lines
)
{
    if (this->physicsInstance != nullptr)
        this->physicsInstance->AppendDebugLines(lines);
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
    if (this->animator != nullptr)
    {
        this->animator->Update(deltaTime);
        const std::uint64_t discontinuity =
            this->animator->DiscontinuityRevision();
        if (discontinuity != this->observedAnimatorDiscontinuityRevision)
        {
            this->observedAnimatorDiscontinuityRevision = discontinuity;
            this->physicsResetPending = this->physicsInstance != nullptr;
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
