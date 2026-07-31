#include "pch.hpp"
#include "model_asset.hpp"
#include <stdexcept>
#include <utility>

ModelAsset::ModelAsset(std::string name)
    : name(std::move(name))
{
    if (this->name.empty())
        throw std::invalid_argument("ModelAsset name must not be empty");
}

const std::string& ModelAsset::Name() const noexcept
{
    return this->name;
}

std::size_t ModelAsset::PartCount() const noexcept
{
    return this->parts.size();
}

std::span<const RenderPart> ModelAsset::Parts() const noexcept
{
    return this->parts;
}

RenderPart& ModelAsset::AddPart(
    Mesh& mesh,
    Material& material,
    const glm::mat4& localTransform
)
{
    return this->parts.emplace_back(mesh, material, localTransform);
}
