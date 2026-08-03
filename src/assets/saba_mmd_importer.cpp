#include "wisteria/assets/saba_mmd_importer.hpp"

#include "wisteria/rendering/texture.hpp"

#include <Saba/Model/MMD/PMXFile.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
bool IsFinitePmx(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

glm::vec3 ConvertPosition(const glm::vec3& value)
{
    if (!IsFinitePmx(value))
        throw std::runtime_error("PMX contains a non-finite vector");
    return glm::vec3(value.x, value.y, -value.z);
}

glm::quat ConvertEulerRotation(const glm::vec3& euler)
{
    if (!IsFinitePmx(euler))
        throw std::runtime_error("PMX contains a non-finite rotation");

    const glm::mat4 reflection(
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    );
    glm::mat4 source(1.0f);
    source = glm::rotate(source, euler.x, glm::vec3(1.0f, 0.0f, 0.0f));
    source = glm::rotate(source, euler.y, glm::vec3(0.0f, 1.0f, 0.0f));
    source = glm::rotate(source, euler.z, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::mat3 converted = glm::mat3(reflection * source * reflection);
    const glm::quat result = glm::normalize(glm::quat_cast(converted));
    const float lengthSquared = glm::dot(result, result);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
        throw std::runtime_error("PMX contains an invalid Euler rotation");
    return result;
}

std::pair<glm::vec3, glm::vec3> ConvertLinearLimits(
    const glm::vec3& lower,
    const glm::vec3& upper
)
{
    const glm::vec3 convertedLower = ConvertPosition(lower);
    const glm::vec3 convertedUpper = ConvertPosition(upper);
    return {
        glm::min(convertedLower, convertedUpper),
        glm::max(convertedLower, convertedUpper)
    };
}

std::pair<glm::vec3, glm::vec3> ConvertAngularLimits(
    const glm::vec3& lower,
    const glm::vec3& upper
)
{
    if (!IsFinitePmx(lower) || !IsFinitePmx(upper))
        throw std::runtime_error("PMX joint contains non-finite angular limits");
    const glm::vec3 convertedLower(-lower.x, -lower.y, lower.z);
    const glm::vec3 convertedUpper(-upper.x, -upper.y, upper.z);
    return {
        glm::min(convertedLower, convertedUpper),
        glm::max(convertedLower, convertedUpper)
    };
}

glm::mat4 MakeModelTransform(
    const glm::vec3& position,
    const glm::quat& rotation
)
{
    return glm::translate(glm::mat4(1.0f), position) *
        glm::mat4_cast(rotation);
}

glm::vec3 FallbackTangent(const glm::vec3& normal)
{
    const glm::vec3 candidate = std::abs(normal.y) < 0.999f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(candidate, normal));
}

std::string ToNarrowUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(u8.data()),
        u8.size()
    );
}

std::filesystem::path PathFromUtf8(std::string_view utf8)
{
    const std::u8string u8(
        reinterpret_cast<const char8_t*>(utf8.data()),
        utf8.size()
    );
    return std::filesystem::path(u8);
}

MorphCategory CategoryFromControl(std::uint8_t controlPanel)
{
    switch (controlPanel)
    {
    case 1: return MorphCategory::Eyebrow;
    case 2: return MorphCategory::Eye;
    case 3: return MorphCategory::Mouth;
    case 4: return MorphCategory::Other;
    default: return MorphCategory::System;
    }
}

MmdRigidBodyShape ShapeFromSaba(saba::PMXRigidbody::Shape shape)
{
    switch (shape)
    {
    case saba::PMXRigidbody::Shape::Sphere: return MmdRigidBodyShape::Sphere;
    case saba::PMXRigidbody::Shape::Box: return MmdRigidBodyShape::Box;
    case saba::PMXRigidbody::Shape::Capsule: return MmdRigidBodyShape::Capsule;
    }
    throw std::runtime_error("Unknown Saba PMX rigid-body shape");
}

MmdRigidBodyMode ModeFromSaba(saba::PMXRigidbody::Operation operation)
{
    switch (operation)
    {
    case saba::PMXRigidbody::Operation::Static: return MmdRigidBodyMode::FollowBone;
    case saba::PMXRigidbody::Operation::Dynamic: return MmdRigidBodyMode::Physics;
    case saba::PMXRigidbody::Operation::DynamicAndBoneMerge:
        return MmdRigidBodyMode::PhysicsWithBone;
    }
    throw std::runtime_error("Unknown Saba PMX rigid-body operation");
}

MmdJointType JointTypeFromSaba(saba::PMXJoint::JointType type)
{
    switch (type)
    {
    case saba::PMXJoint::JointType::SpringDOF6: return MmdJointType::Spring6Dof;
    case saba::PMXJoint::JointType::DOF6: return MmdJointType::SixDof;
    case saba::PMXJoint::JointType::P2P: return MmdJointType::PointToPoint;
    case saba::PMXJoint::JointType::ConeTwist: return MmdJointType::ConeTwist;
    case saba::PMXJoint::JointType::Slider: return MmdJointType::Slider;
    case saba::PMXJoint::JointType::Hinge: return MmdJointType::Hinge;
    }
    throw std::runtime_error("Unknown Saba PMX joint type");
}

std::optional<Skeleton> BuildSkeleton(const saba::PMXFile& pmx)
{
    if (pmx.m_bones.empty())
        return std::nullopt;

    std::vector<Bone> bones;
    bones.reserve(pmx.m_bones.size());
    for (std::size_t index = 0U; index < pmx.m_bones.size(); ++index)
    {
        const saba::PMXBone& source = pmx.m_bones[index];
        Bone bone;
        bone.name = source.m_name;
        bone.parentIndex = source.m_parentBoneIndex >= 0
            ? static_cast<BoneIndex>(source.m_parentBoneIndex)
            : InvalidBoneIndex;

        // Saba computes the local translation relative to the parent bone
        // and mirrors Z for its coordinate space.
        glm::vec3 localPosition = source.m_position;
        if (bone.parentIndex != InvalidBoneIndex &&
            static_cast<std::size_t>(bone.parentIndex) < pmx.m_bones.size())
        {
            localPosition -=
                pmx.m_bones[bone.parentIndex].m_position;
        }
        localPosition.z *= -1.0f;
        bone.bindLocalMatrix = glm::translate(glm::mat4(1.0f), localPosition);

        const glm::vec3 globalPosition =
            source.m_position * glm::vec3(1.0f, 1.0f, -1.0f);
        bone.inverseBindMatrix = glm::inverse(
            glm::translate(glm::mat4(1.0f), globalPosition)
        );

        bone.deformLayer = source.m_deformDepth;
        bone.sourceOrder = static_cast<std::uint32_t>(index);
        const std::uint16_t flags = static_cast<std::uint16_t>(
            source.m_boneFlag
        );
        bone.deformAfterPhysics = (flags & 0x1000U) != 0U;

        if ((flags & (0x0100U | 0x0200U)) != 0U &&
            source.m_appendBoneIndex >= 0)
        {
            bone.appendTransform = MmdAppendTransform{
                static_cast<BoneIndex>(source.m_appendBoneIndex),
                source.m_appendWeight,
                (flags & 0x0100U) != 0U,
                (flags & 0x0200U) != 0U
            };
        }
        if ((flags & 0x0020U) != 0U &&
            source.m_ikTargetBoneIndex >= 0)
        {
            MmdIkConstraint ik;
            ik.targetBone = static_cast<BoneIndex>(
                source.m_ikTargetBoneIndex
            );
            ik.iterations = static_cast<std::uint32_t>(
                source.m_ikIterationCount
            );
            ik.angleLimit = source.m_ikLimit;
            ik.links.reserve(source.m_ikLinks.size());
            for (const saba::PMXIKLink& sourceLink : source.m_ikLinks)
            {
                MmdIkLink link;
                link.bone = static_cast<BoneIndex>(
                    sourceLink.m_ikBoneIndex
                );
                link.hasLimits = sourceLink.m_enableLimit != 0U;
                if (link.hasLimits)
                {
                    link.minimumAngle = glm::vec3(
                        -sourceLink.m_limitMax.x,
                        -sourceLink.m_limitMax.y,
                        sourceLink.m_limitMin.z
                    );
                    link.maximumAngle = glm::vec3(
                        -sourceLink.m_limitMin.x,
                        -sourceLink.m_limitMin.y,
                        sourceLink.m_limitMax.z
                    );
                }
                ik.links.push_back(std::move(link));
            }
            bone.ikConstraint = std::move(ik);
        }
        bones.push_back(std::move(bone));
    }
    return Skeleton(std::move(bones));
}

MmdPhysicsAsset BuildPhysics(
    const saba::PMXFile& pmx,
    const std::optional<Skeleton>& skeleton
)
{
    std::vector<MmdRigidBodyDefinition> rigidBodies;
    rigidBodies.reserve(pmx.m_rigidbodies.size());
    for (const saba::PMXRigidbody& source : pmx.m_rigidbodies)
    {
        MmdRigidBodyDefinition body;
        body.name = source.m_name;
        body.collisionGroup = source.m_group;
        body.nonCollisionMask = source.m_collisionGroup;
        body.shape = ShapeFromSaba(source.m_shape);
        body.size = source.m_shapeSize;
        body.position = ConvertPosition(source.m_translate);
        body.rotation = ConvertEulerRotation(source.m_rotate);
        body.mass = source.m_mass;
        body.linearDamping = source.m_translateDimmer;
        body.angularDamping = source.m_rotateDimmer;
        body.restitution = source.m_repulsion;
        body.friction = source.m_friction;
        body.mode = ModeFromSaba(source.m_op);
        body.modelBindTransform = MakeModelTransform(
            body.position,
            body.rotation
        );

        if (source.m_boneIndex >= 0)
        {
            if (!skeleton.has_value() ||
                static_cast<std::size_t>(source.m_boneIndex) >=
                    pmx.m_bones.size())
            {
                throw std::runtime_error(
                    "PMX rigid body requires an imported Skeleton"
                );
            }
            const std::string boneName =
                pmx.m_bones[source.m_boneIndex].m_name;
            const std::optional<BoneIndex> mapped =
                skeleton->FindBone(boneName);
            if (!mapped.has_value())
            {
                throw std::runtime_error(
                    "PMX rigid body has no matching Skeleton bone: " +
                    boneName
                );
            }
            body.bone = *mapped;
            const glm::mat4 boneModelBind =
                skeleton->InverseRootMatrix() *
                skeleton->BindGlobalMatrices()[body.bone];
            body.boneToBody = glm::inverse(boneModelBind) *
                body.modelBindTransform;
            body.bodyToBone = glm::inverse(body.modelBindTransform) *
                boneModelBind;
        }
        rigidBodies.push_back(std::move(body));
    }

    std::vector<MmdJointDefinition> joints;
    joints.reserve(pmx.m_joints.size());
    for (const saba::PMXJoint& source : pmx.m_joints)
    {
        MmdJointDefinition joint;
        joint.name = source.m_name;
        joint.type = JointTypeFromSaba(source.m_type);
        joint.bodyA = source.m_rigidbodyAIndex < 0
            ? InvalidRigidBodyIndex
            : static_cast<RigidBodyIndex>(source.m_rigidbodyAIndex);
        joint.bodyB = source.m_rigidbodyBIndex < 0
            ? InvalidRigidBodyIndex
            : static_cast<RigidBodyIndex>(source.m_rigidbodyBIndex);
        joint.position = ConvertPosition(source.m_translate);
        joint.rotation = ConvertEulerRotation(source.m_rotate);
        const auto [linearLower, linearUpper] = ConvertLinearLimits(
            source.m_translateLowerLimit,
            source.m_translateUpperLimit
        );
        joint.linearLower = linearLower;
        joint.linearUpper = linearUpper;
        const auto [angularLower, angularUpper] = ConvertAngularLimits(
            source.m_rotateLowerLimit,
            source.m_rotateUpperLimit
        );
        joint.angularLower = angularLower;
        joint.angularUpper = angularUpper;
        joint.linearSpring = source.m_springTranslateFactor;
        joint.angularSpring = source.m_springRotateFactor;
        joint.modelBindTransform = MakeModelTransform(
            joint.position,
            joint.rotation
        );
        joints.push_back(std::move(joint));
    }

    return MmdPhysicsAsset(std::move(rigidBodies), std::move(joints));
}

struct PendingMorph
{
    std::size_t sourceIndex = 0U;
    MorphDefinition definition;
    std::vector<std::pair<int, float>> groupMembers;
    std::vector<std::pair<int, float>> flipMembers;
};

std::vector<MorphDefinition> BuildMorphs(const saba::PMXFile& pmx)
{
    std::vector<PendingMorph> pending;
    pending.reserve(pmx.m_morphs.size());
    for (std::size_t index = 0U; index < pmx.m_morphs.size(); ++index)
    {
        const saba::PMXMorph& source = pmx.m_morphs[index];
        PendingMorph morph;
        morph.sourceIndex = index;
        morph.definition.name = source.m_name;
        morph.definition.category = CategoryFromControl(source.m_controlPanel);

        switch (source.m_morphType)
        {
        case saba::PMXMorphType::Group:
            morph.definition.kind = MorphKind::Group;
            for (const saba::PMXMorph::GroupMorph& member :
                 source.m_groupMorph)
            {
                morph.groupMembers.emplace_back(
                    member.m_morphIndex,
                    member.m_weight
                );
            }
            break;
        case saba::PMXMorphType::Position:
            morph.definition.kind = MorphKind::Vertex;
            break;
        case saba::PMXMorphType::Bone:
            morph.definition.kind = MorphKind::Bone;
            for (const saba::PMXMorph::BoneMorph& offset :
                 source.m_boneMorph)
            {
                if (offset.m_boneIndex < 0)
                    continue;
                BoneMorphOffset converted;
                converted.boneIndex = static_cast<BoneIndex>(
                    offset.m_boneIndex
                );
                converted.translation = ConvertPosition(offset.m_position);
                converted.rotation = offset.m_quaternion;
                morph.definition.boneOffsets.push_back(converted);
            }
            break;
        case saba::PMXMorphType::UV:
        case saba::PMXMorphType::AddUV1:
        case saba::PMXMorphType::AddUV2:
        case saba::PMXMorphType::AddUV3:
        case saba::PMXMorphType::AddUV4:
            morph.definition.kind = MorphKind::Uv;
            break;
        case saba::PMXMorphType::Material:
            morph.definition.kind = MorphKind::Material;
            for (const saba::PMXMorph::MaterialMorph& offset :
                 source.m_materialMorph)
            {
                MaterialMorphOffset converted;
                converted.materialIndex = offset.m_materialIndex < 0
                    ? AllMaterialMorphTargets
                    : static_cast<std::uint32_t>(offset.m_materialIndex);
                converted.operation =
                    offset.m_opType ==
                        saba::PMXMorph::MaterialMorph::OpType::Mul
                    ? MaterialMorphOperation::Multiply
                    : MaterialMorphOperation::Add;
                converted.diffuse = offset.m_diffuse;
                converted.specular = offset.m_specular;
                converted.shininess = offset.m_specularPower;
                converted.ambient = offset.m_ambient;
                converted.edgeColor = offset.m_edgeColor;
                converted.edgeSize = offset.m_edgeSize;
                converted.textureFactor = offset.m_textureFactor;
                converted.sphereTextureFactor =
                    offset.m_sphereTextureFactor;
                converted.toonTextureFactor = offset.m_toonTextureFactor;
                morph.definition.materialOffsets.push_back(converted);
            }
            break;
        case saba::PMXMorphType::Flip:
            morph.definition.kind = MorphKind::Flip;
            for (const saba::PMXMorph::FlipMorph& member :
                 source.m_flipMorph)
            {
                morph.flipMembers.emplace_back(
                    member.m_morphIndex,
                    member.m_weight
                );
            }
            break;
        case saba::PMXMorphType::Impluse:
            morph.definition.kind = MorphKind::Impulse;
            for (const saba::PMXMorph::ImpulseMorph& offset :
                 source.m_impulseMorph)
            {
                ImpulseMorphOffset converted;
                converted.rigidBodyIndex = offset.m_rigidbodyIndex < 0
                    ? InvalidRigidBodyIndex
                    : static_cast<RigidBodyIndex>(
                        offset.m_rigidbodyIndex
                    );
                converted.local = offset.m_localFlag != 0U;
                converted.velocity = ConvertPosition(
                    offset.m_translateVelocity
                );
                converted.torque = ConvertPosition(
                    offset.m_rotateTorque
                );
                morph.definition.impulseOffsets.push_back(converted);
            }
            break;
        }
        pending.push_back(std::move(morph));
    }

    std::vector<MorphIndex> sourceToRuntime(
        pmx.m_morphs.size(),
        InvalidMorphIndex
    );
    for (std::size_t index = 0U; index < pending.size(); ++index)
        sourceToRuntime[pending[index].sourceIndex] =
            static_cast<MorphIndex>(index);

    for (PendingMorph& morph : pending)
    {
        if (morph.definition.kind == MorphKind::Group)
        {
            for (const auto& [member, weight] : morph.groupMembers)
            {
                if (member < 0 ||
                    static_cast<std::size_t>(member) >=
                        sourceToRuntime.size())
                {
                    throw std::runtime_error(
                        "PMX Group morph references an unavailable morph"
                    );
                }
                morph.definition.groupMembers.push_back(GroupMorphMember{
                    sourceToRuntime[static_cast<std::size_t>(member)],
                    weight
                });
            }
        }
        else if (morph.definition.kind == MorphKind::Flip)
        {
            for (const auto& [member, weight] : morph.flipMembers)
            {
                if (member < 0 ||
                    static_cast<std::size_t>(member) >=
                        sourceToRuntime.size())
                {
                    throw std::runtime_error(
                        "PMX Flip morph references an unavailable morph"
                    );
                }
                morph.definition.flipMembers.push_back(FlipMorphMember{
                    sourceToRuntime[static_cast<std::size_t>(member)],
                    weight
                });
            }
        }
    }

    std::vector<MorphDefinition> definitions;
    definitions.reserve(pending.size());
    for (PendingMorph& morph : pending)
        definitions.push_back(std::move(morph.definition));

    if (!definitions.empty())
    {
        const MorphSet validation(definitions);
        (void)validation;
    }
    return definitions;
}

std::optional<std::size_t> TryLoadTexture(
    const std::filesystem::path& path,
    const std::string& name,
    ImportedModelData& result,
    std::unordered_map<std::string, std::size_t>& cache
)
{
    if (path.empty())
        return std::nullopt;
    const auto existing = cache.find(name);
    if (existing != cache.end())
        return existing->second;
    if (!std::filesystem::is_regular_file(path))
        return std::nullopt;
    try
    {
        ImportedTextureData imported;
        imported.name = name;
        imported.source = TextureData::FromFile(
            path,
            TextureColorSpace::Srgb
        );
        const std::size_t index = result.textures.size();
        result.textures.push_back(std::move(imported));
        cache.emplace(name, index);
        return index;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

void BuildMeshesAndMaterials(
    const saba::PMXFile& pmx,
    const std::filesystem::path& modelDirectory,
    ImportedModelData& result
)
{
    std::unordered_map<std::string, std::size_t> textureCache;
    const std::filesystem::path commonToonDirectory =
        std::filesystem::current_path() /
        "third-party" / "saba" / "viewer" / "Saba" / "Viewer" /
        "resource" / "mmd";

    result.materials.reserve(pmx.m_materials.size());
    for (const saba::PMXMaterial& source : pmx.m_materials)
    {
        ImportedMaterialData material;
        material.name = source.m_name;
        material.shadingModel = MaterialShadingModel::MmdToon;
        material.baseColorFactor = glm::clamp(
            source.m_diffuse,
            glm::vec4(0.0f),
            glm::vec4(1.0f)
        );
        material.specularColor = glm::max(
            source.m_specular,
            glm::vec3(0.0f)
        );
        material.shininess = std::max(source.m_specularPower, 1.0f);
        material.ambientColor = glm::max(
            source.m_ambient,
            glm::vec3(0.0f)
        );
        const std::uint8_t drawMode = static_cast<std::uint8_t>(
            source.m_drawMode
        );
        material.doubleSided = (drawMode & 0x01U) != 0U;
        material.edgeEnabled = (drawMode & 0x10U) != 0U;
        material.edgeColor = glm::clamp(
            source.m_edgeColor,
            glm::vec4(0.0f),
            glm::vec4(1.0f)
        );
        material.edgeSize = std::max(source.m_edgeSize, 0.0f);
        material.sphereMapMode =
            source.m_sphereTextureIndex >= 0
            ? static_cast<MmdSphereMapMode>(source.m_sphereMode)
            : MmdSphereMapMode::Disabled;
        material.alphaMode = material.baseColorFactor.a < 0.999f
            ? MaterialAlphaMode::Blend
            : MaterialAlphaMode::Opaque;

        if (source.m_textureIndex >= 0 &&
            static_cast<std::size_t>(source.m_textureIndex) <
                pmx.m_textures.size())
        {
            const std::string textureName =
                pmx.m_textures[source.m_textureIndex].m_textureName;
            const std::filesystem::path texturePath =
                modelDirectory / PathFromUtf8(textureName);
            material.baseColorTexture = TryLoadTexture(
                texturePath,
                textureName,
                result,
                textureCache
            );
        }
        if (source.m_sphereTextureIndex >= 0 &&
            static_cast<std::size_t>(source.m_sphereTextureIndex) <
                pmx.m_textures.size())
        {
            const std::string textureName =
                pmx.m_textures[source.m_sphereTextureIndex].m_textureName;
            const std::filesystem::path texturePath =
                modelDirectory / PathFromUtf8(textureName);
            material.sphereTexture = TryLoadTexture(
                texturePath,
                textureName,
                result,
                textureCache
            );
        }
        if (source.m_toonMode == saba::PMXToonMode::Separate &&
            source.m_toonTextureIndex >= 0 &&
            static_cast<std::size_t>(source.m_toonTextureIndex) <
                pmx.m_textures.size())
        {
            const std::string textureName =
                pmx.m_textures[source.m_toonTextureIndex].m_textureName;
            const std::filesystem::path texturePath =
                modelDirectory / PathFromUtf8(textureName);
            material.toonTexture = TryLoadTexture(
                texturePath,
                textureName,
                result,
                textureCache
            );
        }
        else if (source.m_toonMode == saba::PMXToonMode::Common &&
                 source.m_toonTextureIndex >= 0 &&
                 source.m_toonTextureIndex <= 9)
        {
            const std::string textureName =
                "toon0" + std::to_string(source.m_toonTextureIndex) + ".bmp";
            const std::filesystem::path texturePath =
                commonToonDirectory / textureName;
            material.toonTexture = TryLoadTexture(
                texturePath,
                textureName,
                result,
                textureCache
            );
        }
        result.materials.push_back(std::move(material));
    }

    const std::size_t vertexCount = pmx.m_vertices.size();
    constexpr std::size_t VertexStride = 26U;
    std::size_t faceCursor = 0U;
    for (std::size_t materialIndex = 0U;
         materialIndex < pmx.m_materials.size();
         ++materialIndex)
    {
        const saba::PMXMaterial& source =
            pmx.m_materials[materialIndex];
        const std::int32_t faceVertexCount = source.m_numFaceVertices;
        if (faceVertexCount < 0 ||
            faceVertexCount % 3 != 0 ||
            faceCursor + static_cast<std::size_t>(faceVertexCount) / 3U >
                pmx.m_faces.size())
        {
            throw std::runtime_error(
                "PMX material face range is invalid"
            );
        }

        ImportedMeshData mesh;
        mesh.name = "sabaMesh" + std::to_string(materialIndex);
        mesh.materialIndex = materialIndex;
        mesh.morphMaterialIndex = static_cast<std::uint32_t>(
            materialIndex
        );
        mesh.data.layout = {
            {"position", 3, FLOAT},
            {"color", 3, FLOAT},
            {"texCoord", 2, FLOAT},
            {"normal", 3, FLOAT},
            {"tangent", 4, FLOAT},
            {"additionalTexCoord", 2, FLOAT, false, false, 5U},
            {"edgeScale", 1, FLOAT, false, false, 6U},
            {"boneIndices", 4, FLOAT, false, false, 7U},
            {"boneWeights", 4, FLOAT, false, false, 8U}
        };
        mesh.data.vertices.resize(vertexCount * VertexStride);
        for (std::size_t vertexIndex = 0U;
             vertexIndex < vertexCount;
             ++vertexIndex)
        {
            const saba::PMXVertex& vertex =
                pmx.m_vertices[vertexIndex];
            const glm::vec3 position = ConvertPosition(
                vertex.m_position
            );
            const glm::vec3 normal = glm::normalize(
                ConvertPosition(vertex.m_normal)
            );
            const glm::vec3 tangent = FallbackTangent(normal);
            float* output = mesh.data.vertices.data() +
                vertexIndex * VertexStride;
            std::size_t offset = 0U;
            output[offset++] = position.x;
            output[offset++] = position.y;
            output[offset++] = position.z;
            output[offset++] = 1.0f;
            output[offset++] = 1.0f;
            output[offset++] = 1.0f;
            output[offset++] = vertex.m_uv.x;
            output[offset++] = vertex.m_uv.y;
            output[offset++] = normal.x;
            output[offset++] = normal.y;
            output[offset++] = normal.z;
            output[offset++] = tangent.x;
            output[offset++] = tangent.y;
            output[offset++] = tangent.z;
            output[offset++] = 1.0f;
            output[offset++] = vertex.m_addUV[0].x;
            output[offset++] = vertex.m_addUV[0].y;
            output[offset++] = vertex.m_edgeMag;

            float weights[4] = {};
            float indices[4] = {};
            switch (vertex.m_weightType)
            {
            case saba::PMXVertexWeight::BDEF1:
                indices[0] = static_cast<float>(vertex.m_boneIndices[0]);
                weights[0] = 1.0f;
                break;
            case saba::PMXVertexWeight::BDEF2:
            case saba::PMXVertexWeight::SDEF:
                indices[0] = static_cast<float>(vertex.m_boneIndices[0]);
                indices[1] = static_cast<float>(vertex.m_boneIndices[1]);
                weights[0] = vertex.m_boneWeights[0];
                weights[1] = 1.0f - weights[0];
                break;
            case saba::PMXVertexWeight::BDEF4:
            case saba::PMXVertexWeight::QDEF:
                for (int slot = 0; slot < 4; ++slot)
                {
                    indices[slot] = static_cast<float>(
                        vertex.m_boneIndices[slot]
                    );
                    weights[slot] = vertex.m_boneWeights[slot];
                }
                break;
            }
            for (int slot = 0; slot < 4; ++slot)
            {
                output[offset++] = indices[slot];
            }
            for (int slot = 0; slot < 4; ++slot)
            {
                output[offset++] = weights[slot];
            }
        }

        const std::size_t faceCount =
            static_cast<std::size_t>(faceVertexCount) / 3U;
        mesh.data.indices.reserve(faceCount * 3U);
        for (std::size_t face = 0U; face < faceCount; ++face)
        {
            const saba::PMXFace& sourceFace =
                pmx.m_faces[faceCursor + face];
            for (int corner = 0; corner < 3; ++corner)
            {
                const std::uint32_t vertexIndex =
                    sourceFace.m_vertices[corner];
                if (vertexIndex >= vertexCount)
                {
                    throw std::runtime_error(
                        "PMX face references an invalid vertex"
                    );
                }
                mesh.data.indices.push_back(vertexIndex);
            }
        }
        faceCursor += faceCount;

        result.meshes.push_back(std::move(mesh));
        result.parts.push_back(ImportedPartData{
            "part" + std::to_string(materialIndex),
            materialIndex,
            glm::mat4(1.0f)
        });
    }
}
}

ImportedModelData SabaMmdImporter::Import(
    const std::filesystem::path& filePath
) const
{
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(filePath).lexically_normal();
    if (!std::filesystem::is_regular_file(absolutePath))
    {
        throw std::invalid_argument(
            "Model file does not exist: " + ToNarrowUtf8(filePath)
        );
    }

    const std::string extension = absolutePath.extension().string();
    if (extension != ".pmx")
    {
        throw std::runtime_error(
            "SabaMmdImporter only supports PMX in phase 1"
        );
    }

    saba::PMXFile pmx;
    const std::string utf8Path = ToNarrowUtf8(absolutePath);
    if (!saba::ReadPMXFile(&pmx, utf8Path.c_str()))
    {
        throw std::runtime_error(
            "Saba cannot parse PMX model " + ToNarrowUtf8(filePath)
        );
    }

    ImportedModelData result;
    result.skeleton = BuildSkeleton(pmx);
    if (!pmx.m_rigidbodies.empty() || !pmx.m_joints.empty())
    {
        result.mmdPhysics.emplace(BuildPhysics(pmx, result.skeleton));
    }
    result.morphs = BuildMorphs(pmx);
    BuildMeshesAndMaterials(
        pmx,
        absolutePath.parent_path(),
        result
    );
    if (result.meshes.empty() || result.parts.empty())
    {
        throw std::runtime_error(
            "Saba import produced no drawable mesh parts"
        );
    }
    return result;
}
