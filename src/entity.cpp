#include "pch.hpp"
#include "entity.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

Entity::Entity(
    Mesh& mesh,
    Material& material,
    const Transform& transform
)
    : transform(transform),
      mesh(&mesh),
      material(&material)
{
}

Entity::~Entity()
{
    this->ClearBehaviours();
}

Transform& Entity::GetTransform() noexcept
{
    return this->transform;
}

const Transform& Entity::GetTransform() const noexcept
{
    return this->transform;
}

Mesh& Entity::GetMesh() noexcept
{
    return *this->mesh;
}

const Mesh& Entity::GetMesh() const noexcept
{
    return *this->mesh;
}

void Entity::SetMesh(Mesh& mesh) noexcept
{
    this->mesh = &mesh;
}

Material& Entity::GetMaterial() noexcept
{
    return *this->material;
}

const Material& Entity::GetMaterial() const noexcept
{
    return *this->material;
}

void Entity::SetMaterial(Material& material) noexcept
{
    this->material = &material;
}

bool Entity::IsVisible() const noexcept
{
    return this->visible;
}

void Entity::SetVisible(bool visible) noexcept
{
    this->visible = visible;
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
