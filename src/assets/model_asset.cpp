#include "wisteria/common/pch.hpp"
#include "wisteria/assets/model_asset.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace wisteria
{
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

bool ModelAsset::HasMorphs() const noexcept
{
    return this->morphSet.has_value();
}

const MorphSet* ModelAsset::TryGetMorphSet() const noexcept
{
    return this->morphSet.has_value() ? &*this->morphSet : nullptr;
}

const MorphSet& ModelAsset::GetMorphSet() const
{
    if (!this->morphSet.has_value())
        throw std::logic_error("ModelAsset has no morph definitions");
    return *this->morphSet;
}

void ModelAsset::SetMorphs(std::vector<MorphDefinition> definitions)
{
    if (this->morphSet.has_value())
        throw std::logic_error("ModelAsset morph definitions are already set");
    this->morphSet.emplace(std::move(definitions));
}

bool ModelAsset::HasMmdPhysics() const noexcept
{
    return this->mmdPhysics.has_value();
}

const MmdPhysicsAsset* ModelAsset::TryGetMmdPhysics() const noexcept
{
    return this->mmdPhysics.has_value() ? &*this->mmdPhysics : nullptr;
}

const MmdPhysicsAsset& ModelAsset::GetMmdPhysics() const
{
    if (!this->mmdPhysics.has_value())
        throw std::logic_error("ModelAsset has no MMD physics metadata");
    return *this->mmdPhysics;
}

void ModelAsset::SetMmdPhysics(MmdPhysicsAsset physics)
{
    if (this->mmdPhysics.has_value())
        throw std::logic_error("ModelAsset MMD physics metadata is already set");
    for (const MmdRigidBodyDefinition& body : physics.RigidBodies())
    {
        if (body.bone != InvalidBoneIndex &&
            (!this->skeleton.has_value() ||
                static_cast<std::size_t>(body.bone) >=
                    this->skeleton->BoneCount()))
        {
            throw std::invalid_argument(
                "ModelAsset MMD rigid body references an invalid bone"
            );
        }
    }
    this->mmdPhysics.emplace(std::move(physics));
}

std::size_t ModelAsset::MmdRigidBodyCount() const noexcept
{
    return this->mmdPhysics.has_value()
        ? this->mmdPhysics->RigidBodyCount()
        : 0U;
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
    for (const MmdIkStateTrack& track : clip.MmdIkStateTracks())
    {
        if (static_cast<std::size_t>(track.ControllerBone()) >=
            this->skeleton->BoneCount() ||
            !this->skeleton->BoneAt(track.ControllerBone())
                .ikConstraint.has_value())
        {
            throw std::invalid_argument(
                "ModelAsset animation references an invalid MMD IK controller"
            );
        }
    }
    for (const MorphWeightTrack& track : clip.MorphWeightTracks())
    {
        if (!this->morphSet.has_value() ||
            static_cast<std::size_t>(track.Morph()) >=
                this->morphSet->MorphCount())
        {
            throw std::invalid_argument(
                "ModelAsset animation references an invalid morph"
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
    const glm::mat4& localTransform,
    std::optional<std::uint32_t> morphMaterialIndex
)
{
    return this->parts.emplace_back(
        mesh,
        material,
        localTransform,
        morphMaterialIndex
    );
}
}  // namespace wisteria
