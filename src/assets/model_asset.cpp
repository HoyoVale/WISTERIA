#include "wisteria/common/pch.hpp"
#include "wisteria/assets/model_asset.hpp"
#include "wisteria/core/deterministic_fingerprint.hpp"
#include "wisteria/rendering/mesh.hpp"
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

void ModelAsset::SetSourceDescriptor(ModelSourceDescriptor descriptor)
{
    if (descriptor.sourcePath.empty())
        throw std::invalid_argument("ModelAsset source path must not be empty");
    if (this->sourceDescriptor.has_value())
        throw std::logic_error("ModelAsset source descriptor is already set");
    descriptor.sourcePath = std::filesystem::weakly_canonical(
        std::filesystem::absolute(descriptor.sourcePath)
    );
    this->sourceDescriptor.emplace(std::move(descriptor));
}

bool ModelAsset::HasSourceDescriptor() const noexcept
{
    return this->sourceDescriptor.has_value();
}

const ModelSourceDescriptor* ModelAsset::TryGetSourceDescriptor() const noexcept
{
    return this->sourceDescriptor.has_value() ? &*this->sourceDescriptor : nullptr;
}

const ModelSourceDescriptor& ModelAsset::GetSourceDescriptor() const
{
    if (!this->sourceDescriptor.has_value())
        throw std::logic_error("ModelAsset has no source descriptor");
    return *this->sourceDescriptor;
}

void ModelAsset::SetBackendKind(ModelBackendKind kind)
{
    if (kind > ModelBackendKind::WisteriaGeneric)
    {
        throw std::invalid_argument(
            "ModelAsset backend kind is out of range"
        );
    }
    if (this->backendKind.has_value())
        throw std::logic_error("ModelAsset backend kind is already set");
    this->backendKind.emplace(kind);
}

bool ModelAsset::HasExplicitBackendKind() const noexcept
{
    return this->backendKind.has_value();
}

ModelBackendKind ModelAsset::BackendKind() const noexcept
{
    // R1.5 Phase 0D: backend identity is exclusively the explicit
    // backendKind. ModelSourceDescriptor describes the source only and has
    // no authority over runtime selection.
    return this->backendKind.has_value()
        ? *this->backendKind
        : ModelBackendKind::Static;
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

bool ModelAsset::HasVrmMetadata() const noexcept
{
    return this->vrmMetadata.has_value();
}

const VrmMetadata* ModelAsset::TryGetVrmMetadata() const noexcept
{
    return this->vrmMetadata.has_value()
        ? &*this->vrmMetadata
        : nullptr;
}

const VrmMetadata& ModelAsset::GetVrmMetadata() const
{
    if (!this->vrmMetadata.has_value())
        throw std::logic_error("ModelAsset has no VRM metadata");
    return *this->vrmMetadata;
}

void ModelAsset::SetVrmMetadata(VrmMetadata metadata)
{
    if (this->vrmMetadata.has_value())
        throw std::logic_error("ModelAsset VRM metadata is already set");

    const auto validateBone = [this](BoneIndex bone)
    {
        if (bone == InvalidBoneIndex)
            return;
        if (!this->skeleton.has_value() ||
            static_cast<std::size_t>(bone) >= this->skeleton->BoneCount())
        {
            throw std::invalid_argument(
                "ModelAsset VRM metadata references an invalid bone"
            );
        }
    };

    for (const VrmHumanoidBoneBinding& binding : metadata.humanoidBones)
        validateBone(binding.bone);
    if (metadata.firstPerson.has_value())
        validateBone(metadata.firstPerson->bone);

    this->vrmMetadata.emplace(std::move(metadata));
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

std::uint64_t ModelAsset::DeterministicFingerprint() const noexcept
{
    FingerprintBuilder fp;
    fp.String("wisteria-model-asset/deterministic/v5");
    fp.U32(static_cast<std::uint32_t>(this->BackendKind()));

    // Parts: ordering, local transforms, mesh topology and mesh morph data.
    fp.U32(static_cast<std::uint32_t>(this->parts.size()));
    for (const RenderPart& part : this->parts)
    {
        fp.Mat4(part.LocalTransform());
        fp.U32(
            part.MorphMaterialIndex().value_or(0xFFFFFFFFU)
        );
        const Mesh& mesh = part.GetMesh();
        const DefaultModelData& data = mesh.Data();
        fp.U64(data.vertices.size());
        fp.Bytes(
            data.vertices.data(),
            data.vertices.size() * sizeof(float)
        );
        fp.U64(data.indices.size());
        for (std::uint32_t index : data.indices)
            fp.U32(index);
        fp.U32(static_cast<std::uint32_t>(data.layout.size()));
        for (const Layout& layout : data.layout)
        {
            fp.String(layout.name);
            fp.U32(layout.size);
            fp.U32(static_cast<std::uint32_t>(layout.format));
            fp.Bool(layout.normalized);
            fp.Bool(layout.integer);
            fp.U32(layout.location);
        }
        const std::span<const std::uint32_t> sourceIndices =
            mesh.SourceVertexIndices();
        fp.U64(sourceIndices.size());
        for (std::uint32_t index : sourceIndices)
            fp.U32(index);
        const std::span<const MeshMorphTarget> morphTargets =
            mesh.MorphTargets();
        fp.U64(morphTargets.size());
        for (const MeshMorphTarget& target : morphTargets)
        {
            fp.U32(target.morphIndex);
            fp.U64(target.offsets.size());
            for (const VertexMorphOffset& offset : target.offsets)
            {
                fp.U32(offset.vertexIndex);
                fp.Vec3(offset.offset);
            }
            fp.U64(target.uvOffsets.size());
            for (const UvMorphOffset& offset : target.uvOffsets)
            {
                fp.U32(offset.vertexIndex);
                fp.U8(offset.channel);
                fp.Vec4(offset.offset);
            }
        }
    }

    // Skeleton topology and bind data.
    const Skeleton* skeleton = this->TryGetSkeleton();
    if (skeleton != nullptr)
    {
        fp.U32(static_cast<std::uint32_t>(skeleton->BoneCount()));
        for (std::size_t index = 0U; index < skeleton->BoneCount(); ++index)
        {
            const Bone& bone = skeleton->BoneAt(
                static_cast<BoneIndex>(index)
            );
            fp.String(bone.name);
            fp.U32(bone.parentIndex);
            fp.Mat4(bone.bindLocalMatrix);
        }
    }
    else
    {
        fp.U32(0U);
    }

    // Morph definitions (identity + full member/offset data).
    const MorphSet* morphSet = this->TryGetMorphSet();
    if (morphSet != nullptr)
    {
        fp.U32(static_cast<std::uint32_t>(morphSet->MorphCount()));
        for (const MorphDefinition& definition : morphSet->Definitions())
        {
            fp.String(definition.name);
            fp.U32(static_cast<std::uint32_t>(definition.category));
            fp.U32(static_cast<std::uint32_t>(definition.kind));
            fp.U32(static_cast<std::uint32_t>(
                definition.groupMembers.size()
            ));
            for (const GroupMorphMember& member : definition.groupMembers)
            {
                fp.U32(member.morphIndex);
                fp.F32(member.weight);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.flipMembers.size()
            ));
            for (const FlipMorphMember& member : definition.flipMembers)
            {
                fp.U32(member.morphIndex);
                fp.F32(member.weight);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.boneOffsets.size()
            ));
            for (const BoneMorphOffset& offset : definition.boneOffsets)
            {
                fp.U32(offset.boneIndex);
                fp.Vec3(offset.translation);
                fp.Quat(offset.rotation);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.materialOffsets.size()
            ));
            for (const MaterialMorphOffset& offset :
                 definition.materialOffsets)
            {
                fp.U32(offset.materialIndex);
                fp.U32(static_cast<std::uint32_t>(offset.operation));
                fp.Vec4(offset.diffuse);
                fp.Vec3(offset.specular);
                fp.F32(offset.shininess);
                fp.Vec3(offset.ambient);
                fp.Vec4(offset.edgeColor);
                fp.F32(offset.edgeSize);
                fp.Vec4(offset.textureFactor);
                fp.Vec4(offset.sphereTextureFactor);
                fp.Vec4(offset.toonTextureFactor);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.impulseOffsets.size()
            ));
            for (const ImpulseMorphOffset& offset :
                 definition.impulseOffsets)
            {
                fp.U32(offset.rigidBodyIndex);
                fp.Bool(offset.local);
                fp.Vec3(offset.velocity);
                fp.Vec3(offset.torque);
            }
        }
    }
    else
    {
        fp.U32(0U);
    }

    // VRM semantic layer: model info, humanoid bindings, expressions
    // and first-person look-at configuration.
    const VrmMetadata* vrm = this->TryGetVrmMetadata();
    fp.Bool(vrm != nullptr);
    if (vrm != nullptr)
    {
        fp.String(vrm->specVersion);
        fp.String(vrm->model.name);
        fp.String(vrm->model.title);
        fp.String(vrm->model.version);
        fp.String(vrm->model.author);
        fp.U32(static_cast<std::uint32_t>(vrm->model.authors.size()));
        for (const std::string& author : vrm->model.authors)
            fp.String(author);
        fp.String(vrm->model.copyrightInformation);
        fp.String(vrm->model.contactInformation);
        fp.String(vrm->model.reference);
        fp.U32(static_cast<std::uint32_t>(vrm->model.licenseName));
        fp.String(vrm->model.licenseUrl);
        fp.String(vrm->model.otherLicenseUrl);

        fp.U32(static_cast<std::uint32_t>(vrm->humanoidBones.size()));
        for (const VrmHumanoidBoneBinding& binding : vrm->humanoidBones)
        {
            fp.U32(static_cast<std::uint32_t>(binding.kind));
            fp.U32(binding.bone);
            fp.U32(binding.sourceNode);
            fp.String(binding.sourceNodeName);
            fp.Bool(binding.useDefaultValues);
            fp.Vec3(binding.minimum);
            fp.Vec3(binding.maximum);
            fp.Vec3(binding.center);
            fp.F32(binding.axisLength);
        }

        fp.U32(static_cast<std::uint32_t>(vrm->expressions.size()));
        for (const VrmExpressionDefinition& expression : vrm->expressions)
        {
            fp.String(expression.name);
            fp.U32(static_cast<std::uint32_t>(expression.preset));
            fp.Bool(expression.isBinary);
            fp.U32(static_cast<std::uint32_t>(
                expression.morphBinds.size()
            ));
            for (const VrmExpressionMorphBind& bind :
                 expression.morphBinds)
            {
                fp.U32(bind.sourceMesh);
                fp.U32(bind.sourceNode);
                fp.U32(bind.morphIndex);
                fp.F32(bind.weight);
                fp.U32(static_cast<std::uint32_t>(
                    bind.resolvedMorph
                ));
            }
        }

        fp.Bool(vrm->firstPerson.has_value());
        if (vrm->firstPerson.has_value())
        {
            fp.U32(vrm->firstPerson->bone);
            fp.U32(vrm->firstPerson->sourceNode);
            fp.String(vrm->firstPerson->sourceNodeName);
            fp.U32(static_cast<std::uint32_t>(
                vrm->firstPerson->lookAtType
            ));
        }
    }

    // Animation clips: names, duration, tracks, keys and interpolation.
    fp.U32(static_cast<std::uint32_t>(this->animationClips.size()));
    for (const std::unique_ptr<AnimationClip>& clipPtr :
         this->animationClips)
    {
        const AnimationClip& clip = *clipPtr;
        fp.String(clip.Name());
        fp.F32(clip.Duration());
        fp.U32(static_cast<std::uint32_t>(clip.TrackCount()));
        for (const AnimationTrack& track : clip.Tracks())
        {
            fp.U32(track.Bone());
            fp.U32(static_cast<std::uint32_t>(
                track.TranslationKeys().size()
            ));
            for (const VectorKeyframe& key : track.TranslationKeys())
            {
                fp.F32(key.time);
                fp.Vec3(key.value);
                for (const KeyframeInterpolation& interpolation :
                     key.interpolation)
                {
                    fp.U32(static_cast<std::uint32_t>(interpolation.mode));
                    fp.Vec2(interpolation.controlPoint1);
                    fp.Vec2(interpolation.controlPoint2);
                }
            }
            fp.U32(static_cast<std::uint32_t>(
                track.RotationKeys().size()
            ));
            for (const QuaternionKeyframe& key : track.RotationKeys())
            {
                fp.F32(key.time);
                fp.Quat(key.value);
                fp.U32(static_cast<std::uint32_t>(key.interpolation.mode));
                fp.Vec2(key.interpolation.controlPoint1);
                fp.Vec2(key.interpolation.controlPoint2);
            }
            fp.U32(static_cast<std::uint32_t>(
                track.ScaleKeys().size()
            ));
            for (const VectorKeyframe& key : track.ScaleKeys())
            {
                fp.F32(key.time);
                fp.Vec3(key.value);
                for (const KeyframeInterpolation& interpolation :
                     key.interpolation)
                {
                    fp.U32(static_cast<std::uint32_t>(interpolation.mode));
                    fp.Vec2(interpolation.controlPoint1);
                    fp.Vec2(interpolation.controlPoint2);
                }
            }
        }
        fp.U32(static_cast<std::uint32_t>(
            clip.MorphWeightTrackCount()
        ));
        for (const MorphWeightTrack& track : clip.MorphWeightTracks())
        {
            fp.U32(track.Morph());
            fp.U32(static_cast<std::uint32_t>(track.Keys().size()));
            for (const FloatKeyframe& key : track.Keys())
            {
                fp.F32(key.time);
                fp.F32(key.value);
            }
        }
        fp.U32(static_cast<std::uint32_t>(
            clip.MmdIkStateTrackCount()
        ));
        for (const MmdIkStateTrack& track : clip.MmdIkStateTracks())
        {
            fp.U32(track.ControllerBone());
            fp.U32(static_cast<std::uint32_t>(track.Keys().size()));
            for (const BoolKeyframe& key : track.Keys())
            {
                fp.F32(key.time);
                fp.Bool(key.value);
            }
        }
    }
    return fp.Result();
}
}  // namespace wisteria
