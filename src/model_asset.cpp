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

bool ModelAsset::HasSkeleton() const noexcept
{
    return this->skeleton.has_value();
}

const Skeleton* ModelAsset::TryGetSkeleton() const noexcept
{
    return this->skeleton.has_value() ? &*this->skeleton : nullptr;
}

const Skeleton& ModelAsset::GetSkeleton() const
{
    if (!this->skeleton.has_value())
        throw std::logic_error("ModelAsset has no skeleton");
    return *this->skeleton;
}

void ModelAsset::SetSkeleton(Skeleton skeleton)
{
    if (this->skeleton.has_value())
        throw std::logic_error("ModelAsset skeleton is already set");
    this->skeleton.emplace(std::move(skeleton));
}

RenderPart& ModelAsset::AddPart(
    Mesh& mesh,
    Material& material,
    const glm::mat4& localTransform
)
{
    return this->parts.emplace_back(mesh, material, localTransform);
}
