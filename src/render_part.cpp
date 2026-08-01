#include "pch.hpp"
#include "render_part.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include <cmath>
#include <stdexcept>

namespace
{
bool IsFinite(const glm::mat4& matrix)
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrix[column][row]))
                return false;
        }
    }
    return true;
}
}

RenderPart::RenderPart(
    Mesh& mesh,
    Material& material,
    const glm::mat4& localTransform,
    std::optional<std::uint32_t> morphMaterialIndex
)
    : mesh(&mesh),
      material(&material),
      morphMaterialIndex(morphMaterialIndex)
{
    this->SetLocalTransform(localTransform);
}

Mesh& RenderPart::GetMesh() noexcept
{
    return *this->mesh;
}

Mesh& RenderPart::GetMesh() const noexcept
{
    return *this->mesh;
}

void RenderPart::SetMesh(Mesh& mesh) noexcept
{
    this->mesh = &mesh;
}

Material& RenderPart::GetMaterial() noexcept
{
    return *this->material;
}

Material& RenderPart::GetMaterial() const noexcept
{
    return *this->material;
}

void RenderPart::SetMaterial(Material& material) noexcept
{
    this->material = &material;
}

const glm::mat4& RenderPart::LocalTransform() const noexcept
{
    return this->localTransform;
}

void RenderPart::SetLocalTransform(const glm::mat4& localTransform)
{
    if (!IsFinite(localTransform))
        throw std::invalid_argument("RenderPart transform must contain finite values");

    this->localTransform = localTransform;
}

std::optional<std::uint32_t> RenderPart::MorphMaterialIndex() const noexcept
{
    return this->morphMaterialIndex;
}
