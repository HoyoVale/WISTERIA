#include "pch.hpp"
#include "model_asset.hpp"
#include <algorithm>
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

std::size_t ModelAsset::AnimationClipCount() const noexcept
{
    return this->animationClips.size();
}

const AnimationClip& ModelAsset::AnimationClipAt(std::size_t index) const
{
    if (index >= this->animationClips.size())
        throw std::out_of_range("ModelAsset animation clip index is out of range");
    return *this->animationClips[index];
}

const AnimationClip* ModelAsset::FindAnimationClip(
    std::string_view name
) const noexcept
{
    const auto iterator = std::find_if(
        this->animationClips.begin(),
        this->animationClips.end(),
        [name](const std::unique_ptr<AnimationClip>& clip)
        {
            return clip->Name() == name;
        }
    );
    return iterator == this->animationClips.end() ? nullptr : iterator->get();
}

AnimationClip& ModelAsset::AddAnimationClip(AnimationClip clip)
{
    if (!this->skeleton.has_value())
    {
        throw std::logic_error(
            "ModelAsset must have a skeleton before adding animations"
        );
    }
    if (this->FindAnimationClip(clip.Name()) != nullptr)
    {
        throw std::invalid_argument(
            "ModelAsset animation clip name already exists: " + clip.Name()
        );
    }
    for (const AnimationTrack& track : clip.Tracks())
    {
        if (static_cast<std::size_t>(track.Bone()) >=
            this->skeleton->BoneCount())
        {
            throw std::invalid_argument(
                "ModelAsset animation references an invalid skeleton bone"
            );
        }
    }
    auto stored = std::make_unique<AnimationClip>(std::move(clip));
    AnimationClip& result = *stored;
    this->animationClips.emplace_back(std::move(stored));
    return result;
}

RenderPart& ModelAsset::AddPart(
    Mesh& mesh,
    Material& material,
    const glm::mat4& localTransform
)
{
    return this->parts.emplace_back(mesh, material, localTransform);
}
