#include "pch.hpp"
#include "entity.hpp"

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
