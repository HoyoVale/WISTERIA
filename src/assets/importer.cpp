#include "wisteria/common/pch.hpp"
#include "wisteria/assets/importer.hpp"

#include "texture_path_utils.hpp"
#include "pmx_parser.hpp"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtc/quaternion.hpp>
#include "wisteria/vendor/stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wisteria
{
namespace
{
glm::mat4 MakePmxModelTransform(
    const glm::vec3& position,
    const glm::quat& rotation
)
{
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
}

struct ImportedTextureKey
{
    std::string path;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;

    bool operator==(const ImportedTextureKey&) const noexcept = default;
};

struct ImportedTextureKeyHash
{
    std::size_t operator()(const ImportedTextureKey& key) const noexcept
    {
        const std::size_t pathHash = std::hash<std::string>{}(key.path);
        const std::size_t colorSpaceHash =
            std::hash<int>{}(static_cast<int>(key.colorSpace));
        return pathHash ^ (colorSpaceHash + 0x9e3779b9U +
            (pathHash << 6U) + (pathHash >> 2U));
    }
};

using ImportedTextureIndexMap = std::unordered_map<
    ImportedTextureKey,
    std::size_t,
    ImportedTextureKeyHash
>;

constexpr unsigned int ImportFlags =
    aiProcess_Triangulate |
    aiProcess_JoinIdenticalVertices |
    aiProcess_GenSmoothNormals |
    aiProcess_CalcTangentSpace |
    // Texture decoders keep the first image row at the top. Normalize Assimp's
    // UV output to the same top-left convention used by glTF and our uploader.
    aiProcess_FlipUVs |
    aiProcess_ImproveCacheLocality |
    aiProcess_SortByPType |
    aiProcess_FindInvalidData |
    aiProcess_ValidateDataStructure;

std::string ToString(const aiString& value)
{
    return std::string(value.C_Str(), value.length);
}

std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::filesystem::path FromUtf8(std::string_view value)
{
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char byte : value)
        utf8.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(utf8);
}

std::string PathExtensionHint(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    if (!extension.empty() && extension.front() == '.')
        extension.erase(extension.begin());
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        }
    );
    return extension;
}

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open model file: " + path.string());

    const std::streampos end = stream.tellg();
    if (end <= 0)
        throw std::runtime_error("Model file is empty: " + path.string());
    if (static_cast<std::uintmax_t>(end) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error("Model file is too large: " + path.string());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    if (!stream)
        throw std::runtime_error("Cannot read model file: " + path.string());
    return bytes;
}

std::optional<bool> HasNonOpaqueAlpha(const TextureData& texture)
{
    if (texture.IsRgba8())
    {
        for (std::size_t offset = 3U; offset < texture.data.size(); offset += 4U)
        {
            if (texture.data[offset] < 255U)
                return true;
        }
        return false;
    }

    std::vector<std::uint8_t> fileBytes;
    const std::vector<std::uint8_t>* encoded = &texture.data;
    if (texture.IsFile())
    {
        fileBytes = ReadBinaryFile(texture.filePath);
        encoded = &fileBytes;
    }
    if (encoded->empty() ||
        encoded->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(
            encoded->data(),
            static_cast<int>(encoded->size()),
            &width,
            &height,
            &channels,
            4
        ),
        stbi_image_free
    );
    if (pixels == nullptr || width <= 0 || height <= 0)
        return std::nullopt;
    if (channels != 2 && channels != 4)
        return false;

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        if (pixels.get()[pixel * 4U + 3U] < 255U)
            return true;
    }
    return false;
}

glm::mat4 ToGlm(const aiMatrix4x4& matrix)
{
    glm::mat4 result(1.0f);
    result[0][0] = matrix.a1;
    result[1][0] = matrix.a2;
    result[2][0] = matrix.a3;
    result[3][0] = matrix.a4;
    result[0][1] = matrix.b1;
    result[1][1] = matrix.b2;
    result[2][1] = matrix.b3;
    result[3][1] = matrix.b4;
    result[0][2] = matrix.c1;
    result[1][2] = matrix.c2;
    result[2][2] = matrix.c3;
    result[3][2] = matrix.c4;
    result[0][3] = matrix.d1;
    result[1][3] = matrix.d2;
    result[2][3] = matrix.d3;
    result[3][3] = matrix.d4;
    return result;
}

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

bool IsFinite(const glm::vec3& vector)
{
    return std::isfinite(vector.x) &&
           std::isfinite(vector.y) &&
           std::isfinite(vector.z);
}

glm::vec3 FallbackTangent(const glm::vec3& normal)
{
    const glm::vec3 reference = std::abs(normal.y) < 0.999f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(reference, normal));
}

std::string ResourceName(const aiString& name, const char* prefix, std::size_t index)
{
    std::string result = ToString(name);
    if (result.empty())
        result = std::string(prefix) + std::to_string(index);
    return result;
}

bool MatricesNearlyEqual(
    const glm::mat4& left,
    const glm::mat4& right,
    float epsilon = 0.0001f
)
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (std::abs(left[column][row] - right[column][row]) > epsilon)
                return false;
        }
    }
    return true;
}

float MatrixMaximumDifference(
    const glm::mat4& left,
    const glm::mat4& right
)
{
    float result = 0.0f;
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            result = std::max(
                result,
                std::abs(left[column][row] - right[column][row])
            );
        }
    }
    return result;
}

void CollectNodesByName(
    const aiNode& node,
    std::unordered_map<std::string, std::vector<const aiNode*>>& nodesByName
)
{
    nodesByName[ToString(node.mName)].push_back(&node);
    for (unsigned int index = 0; index < node.mNumChildren; ++index)
    {
        if (node.mChildren[index] == nullptr)
            throw std::runtime_error("Imported skeleton contains a null node");
        CollectNodesByName(*node.mChildren[index], nodesByName);
    }
}

void AppendSkeletonBones(
    const aiNode& node,
    BoneIndex parentIndex,
    const glm::mat4& parentGlobal,
    const std::unordered_set<const aiNode*>& includedNodes,
    const std::unordered_map<std::string, glm::mat4>& inverseBindMatrices,
    std::vector<Bone>& bones
)
{
    if (!includedNodes.contains(&node))
        return;

    if (bones.size() >= static_cast<std::size_t>(InvalidBoneIndex))
        throw std::length_error("Imported skeleton contains too many bones");

    const glm::mat4 localMatrix = ToGlm(node.mTransformation);
    const glm::mat4 globalMatrix = parentGlobal * localMatrix;
    if (!IsFinite(localMatrix) || !IsFinite(globalMatrix))
        throw std::runtime_error("Imported bone contains a non-finite matrix");

    const std::string name = ToString(node.mName);
    const auto inverseBind = inverseBindMatrices.find(name);
    const glm::mat4 inverseBindMatrix =
        inverseBind != inverseBindMatrices.end()
            ? inverseBind->second
            : glm::inverse(globalMatrix);
    if (!IsFinite(inverseBindMatrix))
        throw std::runtime_error("Imported bone has an invalid inverse bind matrix");

    const BoneIndex currentIndex = static_cast<BoneIndex>(bones.size());
    bones.push_back(Bone{
        name,
        parentIndex,
        localMatrix,
        inverseBindMatrix
    });

    for (unsigned int index = 0; index < node.mNumChildren; ++index)
    {
        AppendSkeletonBones(
            *node.mChildren[index],
            currentIndex,
            globalMatrix,
            includedNodes,
            inverseBindMatrices,
            bones
        );
    }
}

void ApplyPmxBoneMetadata(
    std::vector<Bone>& bones,
    const PmxMetadata& metadata
)
{
    std::unordered_map<std::string, BoneIndex> skeletonIndices;
    skeletonIndices.reserve(bones.size());
    for (std::size_t index = 0; index < bones.size(); ++index)
    {
        skeletonIndices.emplace(
            bones[index].name,
            static_cast<BoneIndex>(index)
        );
    }

    std::vector<BoneIndex> pmxToSkeleton;
    pmxToSkeleton.reserve(metadata.bones.size());
    for (const PmxMetadata::BoneMetadata& source : metadata.bones)
    {
        const auto iterator = skeletonIndices.find(source.name);
        if (iterator == skeletonIndices.end())
        {
            throw std::runtime_error(
                "PMX bone metadata has no matching Skeleton bone: " +
                source.name
            );
        }
        pmxToSkeleton.push_back(iterator->second);
    }

    const auto mappedIndex = [&pmxToSkeleton](int index, const char* semantic)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= pmxToSkeleton.size())
        {
            throw std::runtime_error(
                std::string("PMX ") + semantic + " bone index is invalid"
            );
        }
        return pmxToSkeleton[static_cast<std::size_t>(index)];
    };
    const auto convertLimits = [](const glm::vec3& minimum,
                                  const glm::vec3& maximum)
    {
        // Assimp's PMX importer mirrors Z and changes quaternion X/Y signs.
        return std::pair{
            glm::vec3(-maximum.x, -maximum.y, minimum.z),
            glm::vec3(-minimum.x, -minimum.y, maximum.z)
        };
    };

    for (std::size_t index = 0; index < metadata.bones.size(); ++index)
    {
        const PmxMetadata::BoneMetadata& source = metadata.bones[index];
        Bone& destination = bones[pmxToSkeleton[index]];
        destination.deformLayer = source.deformLayer;
        destination.sourceOrder = static_cast<std::uint32_t>(index);
        destination.deformAfterPhysics =
            (source.flags & 0x1000U) != 0U;

        if ((source.flags & (0x0100U | 0x0200U)) != 0U)
        {
            destination.appendTransform = MmdAppendTransform{
                mappedIndex(source.appendSourceIndex, "append-source"),
                source.appendWeight,
                (source.flags & 0x0100U) != 0U,
                (source.flags & 0x0200U) != 0U
            };
        }
        if ((source.flags & 0x0020U) != 0U)
        {
            MmdIkConstraint ik;
            ik.targetBone = mappedIndex(source.ikTargetIndex, "IK target");
            ik.iterations = source.ikIterations;
            ik.angleLimit = source.ikAngleLimit;
            ik.links.reserve(source.ikLinks.size());
            for (const PmxMetadata::IkLink& sourceLink : source.ikLinks)
            {
                MmdIkLink link;
                link.bone = mappedIndex(sourceLink.boneIndex, "IK link");
                link.hasLimits = sourceLink.hasLimits;
                if (link.hasLimits)
                {
                    const auto [minimum, maximum] = convertLimits(
                        sourceLink.minimumAngle,
                        sourceLink.maximumAngle
                    );
                    link.minimumAngle = minimum;
                    link.maximumAngle = maximum;
                }
                ik.links.push_back(std::move(link));
            }
            destination.ikConstraint = std::move(ik);
        }
    }
}

void RemapPmxBoneMorphs(
    std::vector<MorphDefinition>& definitions,
    const PmxMetadata& metadata,
    const Skeleton& skeleton
)
{
    std::vector<BoneIndex> pmxToSkeleton;
    pmxToSkeleton.reserve(metadata.bones.size());
    for (const PmxMetadata::BoneMetadata& source : metadata.bones)
    {
        const std::optional<BoneIndex> destination =
            skeleton.FindBone(source.name);
        if (!destination.has_value())
        {
            throw std::runtime_error(
                "PMX Bone Morph has no matching Skeleton bone: " +
                source.name
            );
        }
        pmxToSkeleton.push_back(*destination);
    }

    for (MorphDefinition& definition : definitions)
    {
        if (definition.kind != MorphKind::Bone)
            continue;
        for (BoneMorphOffset& offset : definition.boneOffsets)
        {
            if (static_cast<std::size_t>(offset.boneIndex) >=
                pmxToSkeleton.size())
            {
                throw std::runtime_error(
                    "PMX Bone Morph source index is out of range"
                );
            }
            offset.boneIndex = pmxToSkeleton[offset.boneIndex];
        }
    }
}

MmdPhysicsAsset BuildPmxPhysicsAsset(
    const PmxMetadata& metadata,
    const std::optional<Skeleton>& skeleton
)
{
    std::vector<MmdRigidBodyDefinition> rigidBodies;
    rigidBodies.reserve(metadata.rigidBodies.size());
    for (const PmxMetadata::RigidBodyMetadata& source : metadata.rigidBodies)
    {
        MmdRigidBodyDefinition body;
        body.name = source.name;
        body.collisionGroup = source.collisionGroup;
        body.nonCollisionMask = source.nonCollisionMask;
        body.shape = source.shape;
        body.size = source.size;
        body.position = source.position;
        body.rotation = source.rotation;
        body.mass = source.mass;
        body.linearDamping = source.linearDamping;
        body.angularDamping = source.angularDamping;
        body.restitution = source.restitution;
        body.friction = source.friction;
        body.mode = source.mode;
        body.modelBindTransform = MakePmxModelTransform(
            body.position,
            body.rotation
        );

        if (source.boneIndex >= 0)
        {
            if (!skeleton.has_value() ||
                static_cast<std::size_t>(source.boneIndex) >=
                    metadata.bones.size())
            {
                throw std::runtime_error(
                    "PMX rigid body requires an imported Skeleton"
                );
            }
            const std::string& boneName = metadata.bones[
                static_cast<std::size_t>(source.boneIndex)
            ].name;
            const std::optional<BoneIndex> mapped = skeleton->FindBone(boneName);
            if (!mapped.has_value())
            {
                throw std::runtime_error(
                    "PMX rigid body has no matching Skeleton bone: " +
                    boneName
                );
            }
            body.bone = *mapped;
            // Assimp hierarchy globals live in skeleton-root space, while
            // PMX rigid bodies and rendered vertices live in model/mesh space.
            // Convert the bind bone into model space before deriving the
            // persistent MMD bone/body offsets.
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
    joints.reserve(metadata.joints.size());
    for (const PmxMetadata::JointMetadata& source : metadata.joints)
    {
        MmdJointDefinition joint;
        joint.name = source.name;
        joint.type = source.type;
        joint.bodyA = source.bodyA < 0
            ? InvalidRigidBodyIndex
            : static_cast<RigidBodyIndex>(source.bodyA);
        joint.bodyB = source.bodyB < 0
            ? InvalidRigidBodyIndex
            : static_cast<RigidBodyIndex>(source.bodyB);
        joint.position = source.position;
        joint.rotation = source.rotation;
        joint.linearLower = source.linearLower;
        joint.linearUpper = source.linearUpper;
        joint.angularLower = source.angularLower;
        joint.angularUpper = source.angularUpper;
        joint.linearSpring = source.linearSpring;
        joint.angularSpring = source.angularSpring;
        joint.modelBindTransform = MakePmxModelTransform(
            joint.position,
            joint.rotation
        );
        joints.push_back(std::move(joint));
    }

    return MmdPhysicsAsset(std::move(rigidBodies), std::move(joints));
}

std::optional<Skeleton> ImportSkeleton(
    const aiScene& scene,
    const PmxMetadata* pmxMetadata
)
{
    std::unordered_map<std::string, glm::mat4> inverseBindMatrices;
    for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex)
    {
        const aiMesh* mesh = scene.mMeshes[meshIndex];
        if (mesh == nullptr)
            throw std::runtime_error("Imported scene contains a null mesh");

        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
        {
            const aiBone* bone = mesh->mBones[boneIndex];
            if (bone == nullptr)
                throw std::runtime_error("Imported mesh contains a null bone");
            const std::string name = ToString(bone->mName);
            if (name.empty())
                throw std::runtime_error("Imported bone name must not be empty");

            const glm::mat4 inverseBindMatrix = ToGlm(bone->mOffsetMatrix);
            if (!IsFinite(inverseBindMatrix))
                throw std::runtime_error("Imported inverse bind matrix is not finite");

            const auto [iterator, inserted] = inverseBindMatrices.emplace(
                name,
                inverseBindMatrix
            );
            if (!inserted &&
                !MatricesNearlyEqual(iterator->second, inverseBindMatrix))
            {
                throw std::runtime_error(
                    "One bone has inconsistent inverse bind matrices across meshes"
                );
            }
        }
    }

    if (inverseBindMatrices.empty())
        return std::nullopt;

    std::unordered_map<std::string, std::vector<const aiNode*>> nodesByName;
    CollectNodesByName(*scene.mRootNode, nodesByName);

    std::unordered_set<const aiNode*> includedNodes;
    for (const auto& entry : inverseBindMatrices)
    {
        const std::string& name = entry.first;
        const auto nodes = nodesByName.find(name);
        if (nodes == nodesByName.end() || nodes->second.empty())
            throw std::runtime_error("Imported bone has no matching hierarchy node: " + name);
        if (nodes->second.size() != 1)
            throw std::runtime_error("Imported bone name is ambiguous in hierarchy: " + name);

        for (const aiNode* node = nodes->second.front();
             node != nullptr;
             node = node->mParent)
        {
            includedNodes.insert(node);
        }
    }

    std::vector<Bone> bones;
    bones.reserve(includedNodes.size());
    AppendSkeletonBones(
        *scene.mRootNode,
        InvalidBoneIndex,
        glm::mat4(1.0f),
        includedNodes,
        inverseBindMatrices,
        bones
    );

    if (pmxMetadata != nullptr)
        ApplyPmxBoneMetadata(bones, *pmxMetadata);

    const Skeleton preliminarySkeleton(bones);
    constexpr std::size_t MaximumExactlyRepresentableFloatIndex = 1U << 24U;
    if (preliminarySkeleton.BoneCount() >
        MaximumExactlyRepresentableFloatIndex)
    {
        throw std::length_error(
            "Skeleton bone indices cannot be represented by the vertex format"
        );
    }
    std::optional<glm::mat4> bindSpaceMatrix;
    for (const auto& [name, inverseBindMatrix] : inverseBindMatrices)
    {
        const std::optional<BoneIndex> boneIndex =
            preliminarySkeleton.FindBone(name);
        if (!boneIndex.has_value())
            throw std::runtime_error("Imported inverse bind has no Skeleton bone");
        const glm::mat4 candidate =
            preliminarySkeleton.BindGlobalMatrices()[*boneIndex] *
            inverseBindMatrix;
        if (!IsFinite(candidate))
            throw std::runtime_error("Imported skeleton bind space is invalid");
        if (!bindSpaceMatrix.has_value())
            bindSpaceMatrix = candidate;
        else if (!MatricesNearlyEqual(*bindSpaceMatrix, candidate, 0.01f))
        {
            throw std::runtime_error(
                "Imported bones do not share one mesh bind space; bone=" +
                name + ", difference=" + std::to_string(
                    MatrixMaximumDifference(*bindSpaceMatrix, candidate)
                )
            );
        }
    }
    if (!bindSpaceMatrix.has_value())
        throw std::runtime_error("Imported skeleton has no weighted bones");

    for (std::size_t index = 0; index < bones.size(); ++index)
    {
        if (!inverseBindMatrices.contains(bones[index].name))
        {
            bones[index].inverseBindMatrix =
                glm::inverse(
                    preliminarySkeleton.BindGlobalMatrices()[index]
                ) * *bindSpaceMatrix;
        }
    }
    const glm::mat4 inverseBindSpace = glm::inverse(*bindSpaceMatrix);
    if (!IsFinite(inverseBindSpace))
        throw std::runtime_error("Imported skeleton bind space is singular");
    return Skeleton(std::move(bones), inverseBindSpace);
}

float AnimationTimeSeconds(double ticks, double ticksPerSecond)
{
    const double seconds = ticks / ticksPerSecond;
    if (!std::isfinite(seconds) || seconds < 0.0 ||
        seconds > static_cast<double>(std::numeric_limits<float>::max()))
    {
        throw std::runtime_error("Imported animation contains an invalid key time");
    }
    return static_cast<float>(seconds);
}

std::vector<VectorKeyframe> ImportVectorKeys(
    const aiVectorKey* source,
    unsigned int count,
    double ticksPerSecond
)
{
    std::vector<VectorKeyframe> result;
    result.reserve(count);
    for (unsigned int index = 0; index < count; ++index)
    {
        const aiVector3D& value = source[index].mValue;
        if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
            !std::isfinite(value.z))
        {
            throw std::runtime_error(
                "Imported animation contains a non-finite vector key"
            );
        }
        result.push_back(VectorKeyframe{
            AnimationTimeSeconds(source[index].mTime, ticksPerSecond),
            glm::vec3(value.x, value.y, value.z)
        });
    }

    std::stable_sort(
        result.begin(),
        result.end(),
        [](const VectorKeyframe& left, const VectorKeyframe& right)
        {
            return left.time < right.time;
        }
    );
    std::vector<VectorKeyframe> unique;
    unique.reserve(result.size());
    for (const VectorKeyframe& key : result)
    {
        if (!unique.empty() &&
            std::abs(unique.back().time - key.time) <= 0.000001f)
        {
            unique.back() = key;
        }
        else
        {
            unique.push_back(key);
        }
    }
    return unique;
}

std::vector<QuaternionKeyframe> ImportQuaternionKeys(
    const aiQuatKey* source,
    unsigned int count,
    double ticksPerSecond
)
{
    std::vector<QuaternionKeyframe> result;
    result.reserve(count);
    for (unsigned int index = 0; index < count; ++index)
    {
        const aiQuaternion& value = source[index].mValue;
        if (!std::isfinite(value.w) || !std::isfinite(value.x) ||
            !std::isfinite(value.y) || !std::isfinite(value.z))
        {
            throw std::runtime_error(
                "Imported animation contains a non-finite rotation key"
            );
        }
        result.push_back(QuaternionKeyframe{
            AnimationTimeSeconds(source[index].mTime, ticksPerSecond),
            glm::quat(value.w, value.x, value.y, value.z)
        });
    }

    std::stable_sort(
        result.begin(),
        result.end(),
        [](const QuaternionKeyframe& left, const QuaternionKeyframe& right)
        {
            return left.time < right.time;
        }
    );
    std::vector<QuaternionKeyframe> unique;
    unique.reserve(result.size());
    for (const QuaternionKeyframe& key : result)
    {
        if (!unique.empty() &&
            std::abs(unique.back().time - key.time) <= 0.000001f)
        {
            unique.back() = key;
        }
        else
        {
            unique.push_back(key);
        }
    }
    return unique;
}

std::vector<AnimationClip> ImportAnimations(
    const aiScene& scene,
    const std::optional<Skeleton>& skeleton
)
{
    std::vector<AnimationClip> result;
    if (scene.mNumAnimations == 0)
        return result;
    if (!skeleton.has_value())
    {
        // Node-only animation needs a scene-node hierarchy, which is outside
        // this first skeletal animation implementation.
        return result;
    }

    result.reserve(scene.mNumAnimations);
    std::unordered_set<std::string> names;
    for (unsigned int animationIndex = 0;
         animationIndex < scene.mNumAnimations;
         ++animationIndex)
    {
        const aiAnimation* sourceAnimation =
            scene.mAnimations[animationIndex];
        if (sourceAnimation == nullptr)
            throw std::runtime_error("Imported scene contains a null animation");

        const double ticksPerSecond =
            sourceAnimation->mTicksPerSecond > 0.0 &&
            std::isfinite(sourceAnimation->mTicksPerSecond)
                ? sourceAnimation->mTicksPerSecond
                : 25.0;
        float duration = AnimationTimeSeconds(
            sourceAnimation->mDuration,
            ticksPerSecond
        );
        std::vector<AnimationTrack> tracks;
        tracks.reserve(sourceAnimation->mNumChannels);
        std::unordered_set<BoneIndex> animatedBones;

        for (unsigned int channelIndex = 0;
             channelIndex < sourceAnimation->mNumChannels;
             ++channelIndex)
        {
            const aiNodeAnim* channel =
                sourceAnimation->mChannels[channelIndex];
            if (channel == nullptr)
                throw std::runtime_error("Imported animation contains a null channel");

            const std::optional<BoneIndex> boneIndex =
                skeleton->FindBone(ToString(channel->mNodeName));
            if (!boneIndex.has_value())
                continue;
            if (!animatedBones.emplace(*boneIndex).second)
            {
                throw std::runtime_error(
                    "Imported animation contains duplicate channels for one bone"
                );
            }

            std::vector<VectorKeyframe> translationKeys = ImportVectorKeys(
                channel->mPositionKeys,
                channel->mNumPositionKeys,
                ticksPerSecond
            );
            std::vector<QuaternionKeyframe> rotationKeys =
                ImportQuaternionKeys(
                    channel->mRotationKeys,
                    channel->mNumRotationKeys,
                    ticksPerSecond
                );
            std::vector<VectorKeyframe> scaleKeys = ImportVectorKeys(
                channel->mScalingKeys,
                channel->mNumScalingKeys,
                ticksPerSecond
            );
            if (translationKeys.empty() && rotationKeys.empty() &&
                scaleKeys.empty())
            {
                continue;
            }

            AnimationTrack track(
                *boneIndex,
                std::move(translationKeys),
                std::move(rotationKeys),
                std::move(scaleKeys)
            );
            duration = std::max(duration, track.EndTime());
            tracks.push_back(std::move(track));
        }

        if (tracks.empty() || duration <= 0.0f)
            continue;

        std::string name = ToString(sourceAnimation->mName);
        if (name.empty())
            name = "animation" + std::to_string(animationIndex);
        if (!names.emplace(name).second)
        {
            name += "_" + std::to_string(animationIndex);
            while (!names.emplace(name).second)
                name += "_";
        }
        result.emplace_back(name, duration, std::move(tracks));
    }
    return result;
}

struct VertexInfluences
{
    std::array<BoneIndex, 4> indices{};
    std::array<float, 4> weights{};
};

void AddVertexInfluence(
    VertexInfluences& influences,
    BoneIndex boneIndex,
    float weight
)
{
    for (std::size_t index = 0; index < influences.weights.size(); ++index)
    {
        if (influences.weights[index] > 0.0f &&
            influences.indices[index] == boneIndex)
        {
            influences.weights[index] += weight;
            return;
        }
    }

    const auto minimum = std::min_element(
        influences.weights.begin(),
        influences.weights.end()
    );
    if (weight <= *minimum)
        return;
    const std::size_t slot = static_cast<std::size_t>(
        std::distance(influences.weights.begin(), minimum)
    );
    influences.indices[slot] = boneIndex;
    influences.weights[slot] = weight;
}

std::vector<VertexInfluences> ImportVertexInfluences(
    const aiMesh& mesh,
    const Skeleton& skeleton,
    std::size_t& requiredBoneCount
)
{
    std::vector<VertexInfluences> result(mesh.mNumVertices);
    BoneIndex fallbackRoot = InvalidBoneIndex;
    for (std::size_t index = 0; index < skeleton.BoneCount(); ++index)
    {
        if (skeleton.BoneAt(static_cast<BoneIndex>(index)).parentIndex ==
            InvalidBoneIndex)
        {
            fallbackRoot = static_cast<BoneIndex>(index);
            break;
        }
    }
    if (fallbackRoot == InvalidBoneIndex)
        throw std::runtime_error("Imported skeleton has no root bone");

    for (unsigned int sourceBoneIndex = 0;
         sourceBoneIndex < mesh.mNumBones;
         ++sourceBoneIndex)
    {
        const aiBone* sourceBone = mesh.mBones[sourceBoneIndex];
        if (sourceBone == nullptr)
            throw std::runtime_error("Imported mesh contains a null bone");
        const std::optional<BoneIndex> boneIndex =
            skeleton.FindBone(ToString(sourceBone->mName));
        if (!boneIndex.has_value())
            throw std::runtime_error("Mesh bone is missing from imported Skeleton");

        requiredBoneCount = std::max(
            requiredBoneCount,
            static_cast<std::size_t>(*boneIndex) + 1U
        );
        for (unsigned int weightIndex = 0;
             weightIndex < sourceBone->mNumWeights;
             ++weightIndex)
        {
            const aiVertexWeight& sourceWeight =
                sourceBone->mWeights[weightIndex];
            if (sourceWeight.mVertexId >= mesh.mNumVertices)
                throw std::runtime_error("Bone weight references an invalid vertex");
            if (!std::isfinite(sourceWeight.mWeight) ||
                sourceWeight.mWeight < 0.0f)
            {
                throw std::runtime_error("Imported bone weight is invalid");
            }
            if (sourceWeight.mWeight > 0.0f)
            {
                AddVertexInfluence(
                    result[sourceWeight.mVertexId],
                    *boneIndex,
                    sourceWeight.mWeight
                );
            }
        }
    }

    for (VertexInfluences& influences : result)
    {
        float totalWeight = 0.0f;
        for (float weight : influences.weights)
            totalWeight += weight;
        if (totalWeight <= 0.000001f)
        {
            influences.indices[0] = fallbackRoot;
            influences.weights[0] = 1.0f;
            requiredBoneCount = std::max(
                requiredBoneCount,
                static_cast<std::size_t>(fallbackRoot) + 1U
            );
            continue;
        }
        for (float& weight : influences.weights)
            weight /= totalWeight;
    }
    return result;
}

ImportedMeshData ImportMesh(
    const aiMesh& mesh,
    std::size_t index,
    const std::vector<float>* mmdVertexEdgeScales,
    const std::vector<std::uint32_t>* mmdSourceVertexIndices,
    const std::vector<PmxMetadata::VertexMorphMetadata>* mmdVertexMorphs,
    const std::vector<PmxMetadata::UvMorphMetadata>* mmdUvMorphs,
    const Skeleton* skeleton
)
{
    if (mesh.mNumVertices == 0)
        throw std::runtime_error("Imported mesh has no vertices");
    if (!mesh.HasNormals())
        throw std::runtime_error("Assimp did not provide mesh normals");

    ImportedMeshData result;
    result.name = ResourceName(mesh.mName, "mesh", index);
    result.materialIndex = mesh.mMaterialIndex;
    result.morphMaterialIndex = mesh.mMaterialIndex;
    result.data.layout = {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT},
        {"normal", 3, FLOAT},
        {"tangent", 4, FLOAT}
    };
    const bool isMmdMesh = mmdVertexEdgeScales != nullptr;
    const bool hasAdditionalTexCoord =
        isMmdMesh && mesh.HasTextureCoords(1);
    if (isMmdMesh)
    {
        result.data.layout.push_back(
            {"additionalTexCoord", 2, FLOAT, false, false, 5U}
        );
        result.data.layout.push_back(
            {"edgeScale", 1, FLOAT, false, false, 6U}
        );
        if (mmdVertexEdgeScales->size() != mesh.mNumVertices)
        {
            throw std::runtime_error(
                "PMX edge-scale data no longer matches imported vertices"
            );
        }
        if (mmdSourceVertexIndices == nullptr ||
            mmdVertexMorphs == nullptr || mmdUvMorphs == nullptr ||
            mmdSourceVertexIndices->size() != mesh.mNumVertices)
        {
            throw std::runtime_error(
                "PMX source-vertex mapping no longer matches imported vertices"
            );
        }
    }
    const bool isSkinned = mesh.HasBones();
    if (isSkinned && skeleton == nullptr)
        throw std::runtime_error("Skinned mesh has no imported Skeleton");

    std::vector<VertexInfluences> vertexInfluences;
    if (isSkinned)
    {
        result.data.layout.push_back(
            {"boneIndices", 4, FLOAT, false, false, 7U}
        );
        result.data.layout.push_back(
            {"boneWeights", 4, FLOAT, false, false, 8U}
        );
        vertexInfluences = ImportVertexInfluences(
            mesh,
            *skeleton,
            result.requiredBoneCount
        );
    }
    const std::size_t vertexStride = 15U +
        (isMmdMesh ? 3U : 0U) + (isSkinned ? 8U : 0U);
    result.data.vertices.reserve(
        static_cast<std::size_t>(mesh.mNumVertices) * vertexStride
    );

    for (unsigned int vertexIndex = 0;
         vertexIndex < mesh.mNumVertices;
         ++vertexIndex)
    {
        const aiVector3D& position = mesh.mVertices[vertexIndex];
        const aiVector3D& normal = mesh.mNormals[vertexIndex];
        const aiColor4D color = mesh.HasVertexColors(0)
            ? mesh.mColors[0][vertexIndex]
            : aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
        const aiVector3D texCoord = mesh.HasTextureCoords(0)
            ? mesh.mTextureCoords[0][vertexIndex]
            : aiVector3D(0.0f, 0.0f, 0.0f);

        const glm::vec3 normalizedNormal = glm::normalize(glm::vec3(
            normal.x,
            normal.y,
            normal.z
        ));
        if (!IsFinite(normalizedNormal))
            throw std::runtime_error("Imported mesh contains an invalid normal");

        glm::vec3 tangent = FallbackTangent(normalizedNormal);
        float tangentHandedness = 1.0f;
        if (mesh.HasTangentsAndBitangents())
        {
            const aiVector3D& sourceTangent = mesh.mTangents[vertexIndex];
            const aiVector3D& sourceBitangent = mesh.mBitangents[vertexIndex];
            glm::vec3 candidate(
                sourceTangent.x,
                sourceTangent.y,
                sourceTangent.z
            );
            candidate -= normalizedNormal *
                glm::dot(normalizedNormal, candidate);
            const float candidateLengthSquared = glm::dot(candidate, candidate);
            if (IsFinite(candidate) && candidateLengthSquared > 0.000001f)
            {
                tangent = candidate / std::sqrt(candidateLengthSquared);
                const glm::vec3 bitangent(
                    sourceBitangent.x,
                    sourceBitangent.y,
                    sourceBitangent.z
                );
                if (IsFinite(bitangent))
                {
                    tangentHandedness =
                        glm::dot(
                            glm::cross(normalizedNormal, tangent),
                            bitangent
                        ) < 0.0f
                            ? -1.0f
                            : 1.0f;
                }
            }
        }

        const float values[] = {
            position.x, position.y, position.z,
            color.r, color.g, color.b,
            texCoord.x, texCoord.y,
            normal.x, normal.y, normal.z,
            tangent.x, tangent.y, tangent.z, tangentHandedness
        };
        result.data.vertices.insert(
            result.data.vertices.end(),
            std::begin(values),
            std::end(values)
        );
        if (isMmdMesh)
        {
            result.data.vertices.push_back(hasAdditionalTexCoord
                ? mesh.mTextureCoords[1][vertexIndex].x
                : 0.0f);
            result.data.vertices.push_back(hasAdditionalTexCoord
                ? mesh.mTextureCoords[1][vertexIndex].y
                : 0.0f);
            result.data.vertices.push_back(
                (*mmdVertexEdgeScales)[vertexIndex]
            );
        }
        if (isSkinned)
        {
            const VertexInfluences& influences =
                vertexInfluences[vertexIndex];
            for (BoneIndex boneIndex : influences.indices)
                result.data.vertices.push_back(static_cast<float>(boneIndex));
            result.data.vertices.insert(
                result.data.vertices.end(),
                influences.weights.begin(),
                influences.weights.end()
            );
        }
    }

    for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
    {
        const aiFace& face = mesh.mFaces[faceIndex];
        if (face.mNumIndices != 3)
            throw std::runtime_error("Imported mesh contains a non-triangle face");
        for (unsigned int corner = 0; corner < face.mNumIndices; ++corner)
            result.data.indices.push_back(face.mIndices[corner]);
    }

    if (result.data.indices.empty())
        throw std::runtime_error("Imported mesh has no triangle indices");

    if (isMmdMesh)
    {
        for (std::size_t morphIndex = 0U;
             morphIndex < mmdVertexMorphs->size();
             ++morphIndex)
        {
            const PmxMetadata::VertexMorphMetadata& sourceMorph =
                (*mmdVertexMorphs)[morphIndex];
            std::unordered_map<std::uint32_t, glm::vec3> offsets;
            offsets.reserve(sourceMorph.offsets.size());
            for (const auto& [vertexIndex, offset] : sourceMorph.offsets)
                offsets.emplace(vertexIndex, offset);

            MeshMorphTarget target;
            target.morphIndex = sourceMorph.morphIndex;
            for (std::size_t localVertex = 0U;
                 localVertex < mmdSourceVertexIndices->size();
                 ++localVertex)
            {
                const auto offset = offsets.find(
                    (*mmdSourceVertexIndices)[localVertex]
                );
                if (offset != offsets.end())
                {
                    target.offsets.push_back(VertexMorphOffset{
                        static_cast<std::uint32_t>(localVertex),
                        offset->second
                    });
                }
            }
            if (!target.offsets.empty())
                result.morphTargets.push_back(std::move(target));
        }

        for (const PmxMetadata::UvMorphMetadata& sourceMorph : *mmdUvMorphs)
        {
            std::unordered_map<std::uint32_t, glm::vec4> offsets;
            offsets.reserve(sourceMorph.offsets.size());
            for (const auto& [vertexIndex, offset] : sourceMorph.offsets)
                offsets.emplace(vertexIndex, offset);

            MeshMorphTarget target;
            target.morphIndex = sourceMorph.morphIndex;
            for (std::size_t localVertex = 0U;
                 localVertex < mmdSourceVertexIndices->size();
                 ++localVertex)
            {
                const auto offset = offsets.find(
                    (*mmdSourceVertexIndices)[localVertex]
                );
                if (offset != offsets.end())
                {
                    target.uvOffsets.push_back(UvMorphOffset{
                        static_cast<std::uint32_t>(localVertex),
                        sourceMorph.channel,
                        offset->second
                    });
                }
            }
            if (!target.uvOffsets.empty())
                result.morphTargets.push_back(std::move(target));
        }
    }
    return result;
}

std::size_t ImportTexture(
    const aiScene& scene,
    const aiString& texturePath,
    TextureColorSpace colorSpace,
    const std::filesystem::path& modelDirectory,
    ImportedModelData& result,
    ImportedTextureIndexMap& textureIndices
)
{
    const ImportedTextureKey key{ToString(texturePath), colorSpace};
    const auto existing = textureIndices.find(key);
    if (existing != textureIndices.end())
        return existing->second;

    ImportedTextureData imported;
    imported.name = key.path.empty()
        ? "texture" + std::to_string(result.textures.size())
        : key.path;

    if (const aiTexture* embedded = scene.GetEmbeddedTexture(texturePath.C_Str()))
    {
        if (embedded->mHeight == 0)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(embedded->pcData);
            imported.source = TextureData::FromEncoded(
                std::vector<std::uint8_t>(begin, begin + embedded->mWidth),
                colorSpace
            );
        }
        else
        {
            const std::size_t pixelCount =
                static_cast<std::size_t>(embedded->mWidth) * embedded->mHeight;
            if (pixelCount > std::numeric_limits<std::size_t>::max() / 4)
                throw std::length_error("Embedded texture dimensions overflow");

            std::vector<std::uint8_t> rgba(pixelCount * 4);
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                rgba[pixel * 4 + 0] = embedded->pcData[pixel].r;
                rgba[pixel * 4 + 1] = embedded->pcData[pixel].g;
                rgba[pixel * 4 + 2] = embedded->pcData[pixel].b;
                rgba[pixel * 4 + 3] = embedded->pcData[pixel].a;
            }
            imported.source = TextureData::FromRgba8(
                static_cast<int>(embedded->mWidth),
                static_cast<int>(embedded->mHeight),
                std::move(rgba),
                colorSpace
            );
        }
    }
    else
    {
        std::string normalizedKey = key.path;
        std::replace(normalizedKey.begin(), normalizedKey.end(), '\\', '/');
        std::filesystem::path externalPath = FromUtf8(normalizedKey);
        if (externalPath.is_relative())
            externalPath = modelDirectory / externalPath;
        externalPath = externalPath.lexically_normal();
        if (!std::filesystem::is_regular_file(externalPath))
        {
            externalPath = wisteria::ResolvePathCaseInsensitive(externalPath);
        }
        if (!std::filesystem::is_regular_file(externalPath))
        {
            throw std::runtime_error("External model texture was not found: " + externalPath.string());
        }
        imported.source = TextureData::FromFile(externalPath, colorSpace);
    }

    imported.hasNonOpaqueAlpha = HasNonOpaqueAlpha(imported.source);

    const std::size_t textureIndex = result.textures.size();
    result.textures.push_back(std::move(imported));
    textureIndices.emplace(key, textureIndex);
    return textureIndex;
}

std::size_t ImportCommonToonTexture(
    unsigned int toonIndex,
    ImportedModelData& result,
    ImportedTextureIndexMap& textureIndices
)
{
    if (toonIndex > 9U)
        throw std::runtime_error("PMX common Toon texture index is out of range");

    const std::string name =
        "__mmd_common_toon_" + std::to_string(toonIndex);
    const ImportedTextureKey key{name, TextureColorSpace::Srgb};
    const auto existing = textureIndices.find(key);
    if (existing != textureIndices.end())
        return existing->second;

    constexpr int RampHeight = 256;
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(RampHeight) * 4U
    );
    const float shadowFloor = 0.42f + 0.025f * static_cast<float>(toonIndex);
    for (int row = 0; row < RampHeight; ++row)
    {
        const float coordinate = static_cast<float>(row) /
            static_cast<float>(RampHeight - 1);
        const float band = glm::smoothstep(0.38f, 0.62f, coordinate);
        const float value = glm::mix(shadowFloor, 1.0f, band);
        const std::uint8_t encoded = static_cast<std::uint8_t>(
            glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f
        );
        const std::size_t offset = static_cast<std::size_t>(row) * 4U;
        pixels[offset] = encoded;
        pixels[offset + 1U] = encoded;
        pixels[offset + 2U] = encoded;
        pixels[offset + 3U] = 255U;
    }

    const std::size_t textureIndex = result.textures.size();
    result.textures.push_back(ImportedTextureData{
        name,
        TextureData::FromRgba8(
            1,
            RampHeight,
            std::move(pixels),
            TextureColorSpace::Srgb
        ),
        std::optional<bool>(false)
    });
    textureIndices.emplace(key, textureIndex);
    return textureIndex;
}

std::optional<aiString> BaseColorTexturePath(const aiMaterial& material)
{
    aiString path;
    if (material.GetTextureCount(aiTextureType_BASE_COLOR) > 0 &&
        material.GetTexture(aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS)
    {
        return path;
    }
    if (material.GetTextureCount(aiTextureType_DIFFUSE) > 0 &&
        material.GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
    {
        return path;
    }
    return std::nullopt;
}

std::optional<aiString> NormalTexturePath(const aiMaterial& material)
{
    aiString path;
    if (material.GetTextureCount(aiTextureType_NORMALS) > 0 &&
        material.GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS)
    {
        return path;
    }
    return std::nullopt;
}

std::optional<aiString> TexturePath(
    const aiMaterial& material,
    aiTextureType type
)
{
    aiString path;
    if (material.GetTextureCount(type) > 0 &&
        material.GetTexture(type, 0, &path) == AI_SUCCESS)
    {
        return path;
    }
    return std::nullopt;
}

std::optional<aiString> MetallicRoughnessTexturePath(
    const aiMaterial& material
)
{
    if (const std::optional<aiString> path =
            TexturePath(material, aiTextureType_METALNESS))
    {
        return path;
    }
    return TexturePath(material, aiTextureType_DIFFUSE_ROUGHNESS);
}

ImportedMaterialData ImportMaterial(
    const aiScene& scene,
    const aiMaterial& material,
    std::size_t index,
    const std::filesystem::path& modelDirectory,
    ImportedModelData& result,
    ImportedTextureIndexMap& textureIndices,
    const PmxMaterialMetadata* mmdMaterial
)
{
    ImportedMaterialData imported;
    aiString name;
    if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
        imported.name = ResourceName(name, "material", index);
    else
        imported.name = "material" + std::to_string(index);

    aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
    if (material.Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
        material.Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
    imported.baseColorFactor = glm::clamp(
        glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a),
        glm::vec4(0.0f),
        glm::vec4(1.0f)
    );

    float opacity = 1.0f;
    if (material.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS &&
        std::isfinite(opacity))
    {
        // glTF importers may expose baseColorFactor.a through both keys.
        // Only use the legacy opacity key when alpha was not already supplied.
        if (imported.baseColorFactor.a >= 1.0f)
            imported.baseColorFactor.a = glm::clamp(opacity, 0.0f, 1.0f);
    }

    aiColor3D specular(1.0f, 1.0f, 1.0f);
    if (material.Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS)
    {
        imported.specularColor = glm::max(
            glm::vec3(specular.r, specular.g, specular.b),
            glm::vec3(0.0f)
        );
    }
    float shininess = imported.shininess;
    if (material.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS &&
        std::isfinite(shininess))
    {
        imported.shininess = std::max(shininess, 1.0f);
    }

    float metallicFactor = imported.metallicFactor;
    if (material.Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS &&
        std::isfinite(metallicFactor))
    {
        imported.metallicFactor = glm::clamp(metallicFactor, 0.0f, 1.0f);
    }

    float roughnessFactor = imported.roughnessFactor;
    if (material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == AI_SUCCESS &&
        std::isfinite(roughnessFactor))
    {
        imported.roughnessFactor = glm::clamp(roughnessFactor, 0.0f, 1.0f);
    }
    else
    {
        imported.roughnessFactor = glm::clamp(
            std::sqrt(2.0f / (imported.shininess + 2.0f)),
            0.0f,
            1.0f
        );
    }

    aiColor3D emissive(0.0f, 0.0f, 0.0f);
    if (material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
    {
        const glm::vec3 value(emissive.r, emissive.g, emissive.b);
        if (IsFinite(value))
            imported.emissiveFactor = glm::max(value, glm::vec3(0.0f));
    }

    aiString alphaMode;
    if (material.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
    {
        const std::string mode = ToString(alphaMode);
        if (mode == "MASK")
            imported.alphaMode = MaterialAlphaMode::Mask;
        else if (mode == "BLEND")
            imported.alphaMode = MaterialAlphaMode::Blend;
    }
    else if (imported.baseColorFactor.a < 1.0f)
    {
        imported.alphaMode = MaterialAlphaMode::Blend;
    }
    else if (material.GetTextureCount(aiTextureType_OPACITY) > 0)
    {
        imported.alphaMode = MaterialAlphaMode::Blend;
    }

    float alphaCutoff = imported.alphaCutoff;
    if (material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) == AI_SUCCESS &&
        std::isfinite(alphaCutoff))
    {
        imported.alphaCutoff = glm::clamp(alphaCutoff, 0.0f, 1.0f);
    }

    int doubleSided = 0;
    if (material.Get(AI_MATKEY_TWOSIDED, doubleSided) == AI_SUCCESS)
        imported.doubleSided = doubleSided != 0;

    if (mmdMaterial != nullptr)
    {
        imported.shadingModel = MaterialShadingModel::MmdToon;
        if (!mmdMaterial->name.empty())
            imported.name = mmdMaterial->name;
        imported.baseColorFactor = glm::clamp(
            mmdMaterial->diffuse,
            glm::vec4(0.0f),
            glm::vec4(1.0f)
        );
        imported.specularColor = glm::max(
            mmdMaterial->specular,
            glm::vec3(0.0f)
        );
        imported.shininess = std::max(mmdMaterial->specularPower, 1.0f);
        imported.ambientColor = glm::max(
            mmdMaterial->ambient,
            glm::vec3(0.0f)
        );
        imported.doubleSided = mmdMaterial->doubleSided;
        imported.groundShadow = mmdMaterial->groundShadow;
        imported.castSelfShadow = mmdMaterial->castSelfShadow;
        imported.receiveSelfShadow = mmdMaterial->receiveSelfShadow;
        imported.edgeEnabled = mmdMaterial->edgeEnabled;
        imported.edgeColor = glm::clamp(
            mmdMaterial->edgeColor,
            glm::vec4(0.0f),
            glm::vec4(1.0f)
        );
        imported.edgeSize = std::max(mmdMaterial->edgeSize, 0.0f);
        imported.sphereMapMode = mmdMaterial->sphereTexture.has_value()
            ? mmdMaterial->sphereMode
            : MmdSphereMapMode::Disabled;
        imported.alphaMode = imported.baseColorFactor.a < 0.999f
            ? MaterialAlphaMode::Blend
            : MaterialAlphaMode::Opaque;
    }

    std::optional<aiString> baseColorPath;
    const std::optional<aiString> opacityPath =
        TexturePath(material, aiTextureType_OPACITY);
    if (mmdMaterial != nullptr && mmdMaterial->diffuseTexture.has_value())
        baseColorPath = aiString(mmdMaterial->diffuseTexture->c_str());
    else
        baseColorPath = BaseColorTexturePath(material);
    if (baseColorPath.has_value())
    {
        imported.baseColorTexture = ImportTexture(
            scene,
            *baseColorPath,
            TextureColorSpace::Srgb,
            modelDirectory,
            result,
            textureIndices
        );

        const std::optional<bool> hasTextureAlpha = result.textures[
            *imported.baseColorTexture
        ].hasNonOpaqueAlpha;
        const bool baseTextureDefinesOpacity =
            !opacityPath.has_value() ||
            ToString(*opacityPath) == ToString(*baseColorPath);
        if (mmdMaterial != nullptr && hasTextureAlpha == true &&
            imported.alphaMode == MaterialAlphaMode::Opaque)
        {
            // PMX has no explicit OPAQUE/MASK/BLEND enum. Its diffuse texture
            // alpha usually represents cutout coverage such as hair strands.
            // Keep depth writes for those pixels; only a translucent material
            // factor should enter the true Blend path.
            imported.alphaMode = MaterialAlphaMode::Mask;
        }
        else if (baseTextureDefinesOpacity &&
            hasTextureAlpha == false &&
            imported.alphaMode == MaterialAlphaMode::Blend &&
            imported.baseColorFactor.a >= 0.999f)
        {
            // Some MMD conversion tools emit BLEND/map_d for every material,
            // even when its base texture contains no transparent pixels.
            imported.alphaMode = MaterialAlphaMode::Opaque;
        }
    }
    if (const std::optional<aiString> path = NormalTexturePath(material))
    {
        imported.normalTexture = ImportTexture(
            scene,
            *path,
            TextureColorSpace::Linear,
            modelDirectory,
            result,
            textureIndices
        );

        float normalScale = imported.normalScale;
        if (material.Get(
                AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0),
                normalScale
            ) == AI_SUCCESS && std::isfinite(normalScale))
        {
            imported.normalScale = std::max(normalScale, 0.0f);
        }
    }
    if (const std::optional<aiString> path =
            MetallicRoughnessTexturePath(material))
    {
        imported.metallicRoughnessTexture = ImportTexture(
            scene,
            *path,
            TextureColorSpace::Linear,
            modelDirectory,
            result,
            textureIndices
        );
    }
    if (const std::optional<aiString> path =
            TexturePath(material, aiTextureType_EMISSIVE))
    {
        imported.emissiveTexture = ImportTexture(
            scene,
            *path,
            TextureColorSpace::Srgb,
            modelDirectory,
            result,
            textureIndices
        );
    }
    if (const std::optional<aiString> path =
            TexturePath(material, aiTextureType_LIGHTMAP))
    {
        imported.occlusionTexture = ImportTexture(
            scene,
            *path,
            TextureColorSpace::Linear,
            modelDirectory,
            result,
            textureIndices
        );

        float occlusionStrength = imported.occlusionStrength;
        if (material.Get(
                AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0),
                occlusionStrength
            ) == AI_SUCCESS && std::isfinite(occlusionStrength))
        {
            imported.occlusionStrength = glm::clamp(
                occlusionStrength,
                0.0f,
                1.0f
            );
        }
    }
    if (mmdMaterial != nullptr && mmdMaterial->sphereTexture.has_value())
    {
        imported.sphereTexture = ImportTexture(
            scene,
            aiString(mmdMaterial->sphereTexture->c_str()),
            TextureColorSpace::Srgb,
            modelDirectory,
            result,
            textureIndices
        );
    }
    if (mmdMaterial != nullptr)
    {
        if (mmdMaterial->toonTexture.has_value())
        {
            imported.toonTexture = ImportTexture(
                scene,
                aiString(mmdMaterial->toonTexture->c_str()),
                TextureColorSpace::Srgb,
                modelDirectory,
                result,
                textureIndices
            );
        }
        else if (mmdMaterial->commonToonIndex.has_value())
        {
            imported.toonTexture = ImportCommonToonTexture(
                *mmdMaterial->commonToonIndex,
                result,
                textureIndices
            );
        }
    }
    return imported;
}

void ImportNodeParts(
    const aiScene& scene,
    const aiNode& node,
    const glm::mat4& parentTransform,
    ImportedModelData& result
)
{
    const glm::mat4 transform = parentTransform * ToGlm(node.mTransformation);
    if (!IsFinite(transform))
        throw std::runtime_error("Imported node transform contains non-finite values");

    for (unsigned int index = 0; index < node.mNumMeshes; ++index)
    {
        const std::size_t meshIndex = node.mMeshes[index];
        if (meshIndex >= result.meshes.size() || meshIndex >= scene.mNumMeshes)
            throw std::runtime_error("Imported node references an invalid mesh index");

        result.parts.push_back(ImportedPartData{
            ResourceName(node.mName, "part", result.parts.size()),
            meshIndex,
            transform
        });
    }

    for (unsigned int child = 0; child < node.mNumChildren; ++child)
    {
        if (node.mChildren[child] == nullptr)
            throw std::runtime_error("Imported node contains a null child");
        ImportNodeParts(scene, *node.mChildren[child], transform, result);
    }
}
}

ImportedModelData ModelImporter::Import(
    const std::filesystem::path& filePath
) const
{
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(filePath).lexically_normal();
    if (!std::filesystem::is_regular_file(absolutePath))
        throw std::invalid_argument("Model file does not exist: " + filePath.string());

    const std::string extensionHint = PathExtensionHint(absolutePath);
    Assimp::Importer importer;
    const aiScene* scene = nullptr;
    std::vector<std::uint8_t> fileBytes;
    std::optional<PmxMetadata> pmxMetadata;
    if (extensionHint == "pmx")
    {
        // PMX geometry is self-contained. Read from memory so Unicode model
        // paths work on Windows, then resolve its external textures ourselves.
        fileBytes = ReadBinaryFile(absolutePath);
        pmxMetadata = ParsePmxMetadata(fileBytes);
        // Assimp's MMD importer expands one vertex per surface index. Keep
        // that mapping so PMX per-vertex edge scales remain aligned. The MMD
        // importer already flips V internally; applying our normal FlipUVs
        // post-process a second time restores the original MMD UV convention
        // used by the unflipped stb_image upload path.
        const unsigned int pmxImportFlags = ImportFlags &
            ~static_cast<unsigned int>(aiProcess_JoinIdenticalVertices) &
            ~static_cast<unsigned int>(aiProcess_ImproveCacheLocality);
        const std::vector<std::uint8_t>& assimpBytes =
            pmxMetadata->assimpCompatibleBytes.empty()
                ? fileBytes
                : pmxMetadata->assimpCompatibleBytes;
        scene = importer.ReadFileFromMemory(
            assimpBytes.data(),
            assimpBytes.size(),
            pmxImportFlags,
            extensionHint.c_str()
        );
    }
    else if (extensionHint == "glb")
    {
        // A GLB is self-contained. Reading it through std::filesystem keeps
        // Chinese Windows paths independent of the active system code page.
        fileBytes = ReadBinaryFile(absolutePath);
        scene = importer.ReadFileFromMemory(
            fileBytes.data(),
            fileBytes.size(),
            ImportFlags,
            extensionHint.c_str()
        );
    }
    else
    {
        // OBJ and textual glTF can reference MTL, buffers and images beside
        // the model, so Assimp must receive the real UTF-8 file path.
        const std::string utf8Path = ToUtf8(absolutePath);
        scene = importer.ReadFile(utf8Path.c_str(), ImportFlags);
    }
    if (scene == nullptr)
    {
        throw std::runtime_error(
            "Assimp cannot import model " + filePath.string() + ": " +
            importer.GetErrorString()
        );
    }
    if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mRootNode == nullptr)
    {
        throw std::runtime_error("Assimp returned an incomplete model scene");
    }

    ImportedModelData result;
    result.skeleton = ImportSkeleton(
        *scene,
        pmxMetadata.has_value() ? &*pmxMetadata : nullptr
    );
    if (pmxMetadata.has_value())
    {
        if (!pmxMetadata->rigidBodies.empty() || !pmxMetadata->joints.empty())
        {
            result.mmdPhysics.emplace(
                BuildPmxPhysicsAsset(*pmxMetadata, result.skeleton)
            );
        }
        result.morphs = pmxMetadata->morphDefinitions;
        const bool hasBoneMorph = std::any_of(
            result.morphs.begin(),
            result.morphs.end(),
            [](const MorphDefinition& definition)
            {
                return definition.kind == MorphKind::Bone;
            }
        );
        if (hasBoneMorph && !result.skeleton.has_value())
        {
            throw std::runtime_error(
                "PMX Bone Morph requires an imported Skeleton"
            );
        }
        if (hasBoneMorph)
        {
            RemapPmxBoneMorphs(
                result.morphs,
                *pmxMetadata,
                *result.skeleton
            );
        }
    }
    result.animations = ImportAnimations(*scene, result.skeleton);
    ImportedTextureIndexMap textureIndices;
    std::unordered_map<unsigned int, std::size_t> materialIndices;
    result.meshes.reserve(scene->mNumMeshes);
    for (unsigned int index = 0; index < scene->mNumMeshes; ++index)
    {
        if (scene->mMeshes[index] == nullptr)
            throw std::runtime_error("Imported scene contains a null mesh");

        const unsigned int sourceMaterialIndex =
            scene->mMeshes[index]->mMaterialIndex;
        const std::vector<float>* mmdEdgeScales = nullptr;
        const std::vector<std::uint32_t>* mmdSourceVertexIndices = nullptr;
        const std::vector<PmxMetadata::VertexMorphMetadata>*
            mmdVertexMorphs = nullptr;
        const std::vector<PmxMetadata::UvMorphMetadata>* mmdUvMorphs = nullptr;
        if (pmxMetadata.has_value())
        {
            if (sourceMaterialIndex >=
                pmxMetadata->materialVertexEdgeScales.size())
            {
                throw std::runtime_error("PMX mesh has no matching edge-scale range");
            }
            mmdEdgeScales =
                &pmxMetadata->materialVertexEdgeScales[sourceMaterialIndex];
            mmdSourceVertexIndices =
                &pmxMetadata->materialSourceVertexIndices[sourceMaterialIndex];
            mmdVertexMorphs = &pmxMetadata->vertexMorphs;
            mmdUvMorphs = &pmxMetadata->uvMorphs;
        }
        ImportedMeshData mesh = ImportMesh(
            *scene->mMeshes[index],
            index,
            mmdEdgeScales,
            mmdSourceVertexIndices,
            mmdVertexMorphs,
            mmdUvMorphs,
            result.skeleton.has_value() ? &*result.skeleton : nullptr
        );
        if (sourceMaterialIndex >= scene->mNumMaterials ||
            scene->mMaterials[sourceMaterialIndex] == nullptr)
        {
            throw std::runtime_error("Imported mesh references an invalid material index");
        }

        auto materialEntry = materialIndices.find(sourceMaterialIndex);
        if (materialEntry == materialIndices.end())
        {
            const std::size_t importedMaterialIndex = result.materials.size();
            result.materials.push_back(ImportMaterial(
                *scene,
                *scene->mMaterials[sourceMaterialIndex],
                sourceMaterialIndex,
                absolutePath.parent_path(),
                result,
                textureIndices,
                pmxMetadata.has_value() &&
                    sourceMaterialIndex < pmxMetadata->materials.size()
                    ? &pmxMetadata->materials[sourceMaterialIndex]
                    : nullptr
            ));
            materialEntry = materialIndices.emplace(
                sourceMaterialIndex,
                importedMaterialIndex
            ).first;
        }
        mesh.materialIndex = materialEntry->second;
        result.meshes.push_back(std::move(mesh));
    }

    ImportNodeParts(*scene, *scene->mRootNode, glm::mat4(1.0f), result);
    if (result.meshes.empty() || result.parts.empty())
        throw std::runtime_error("Imported model contains no drawable mesh parts");
    return result;
}
}  // namespace wisteria
