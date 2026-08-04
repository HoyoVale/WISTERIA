#include "wisteria/common/pch.hpp"
#include "wisteria/assets/importer.hpp"

#include "texture_path_utils.hpp"

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

namespace
{
struct PmxMaterialMetadata
{
    std::string name;
    glm::vec4 diffuse{1.0f};
    glm::vec3 specular{0.0f};
    float specularPower = 1.0f;
    glm::vec3 ambient{0.0f};
    bool doubleSided = false;
    bool edgeEnabled = false;
    glm::vec4 edgeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float edgeSize = 0.0f;
    std::optional<std::string> diffuseTexture;
    std::optional<std::string> sphereTexture;
    MmdSphereMapMode sphereMode = MmdSphereMapMode::Disabled;
    std::optional<std::string> toonTexture;
    std::optional<unsigned int> commonToonIndex;
};

struct PmxMetadata
{
    std::vector<PmxMaterialMetadata> materials;
    std::vector<std::vector<float>> materialVertexEdgeScales;
    std::vector<std::vector<std::uint32_t>> materialSourceVertexIndices;
    struct IkLink
    {
        int boneIndex = -1;
        bool hasLimits = false;
        glm::vec3 minimumAngle{0.0f};
        glm::vec3 maximumAngle{0.0f};
    };

    struct BoneMetadata
    {
        std::string name;
        std::int32_t deformLayer = 0;
        std::uint16_t flags = 0U;
        int appendSourceIndex = -1;
        float appendWeight = 0.0f;
        int ikTargetIndex = -1;
        std::uint32_t ikIterations = 0U;
        float ikAngleLimit = 0.0f;
        std::vector<IkLink> ikLinks;
    };

    struct VertexMorphMetadata
    {
        MorphIndex morphIndex = InvalidMorphIndex;
        std::string name;
        MorphCategory category = MorphCategory::Other;
        std::vector<std::pair<std::uint32_t, glm::vec3>> offsets;
    };

    struct UvMorphMetadata
    {
        MorphIndex morphIndex = InvalidMorphIndex;
        std::uint8_t channel = 0U;
        std::vector<std::pair<std::uint32_t, glm::vec4>> offsets;
    };

    struct RigidBodyMetadata
    {
        std::string name;
        int boneIndex = -1;
        std::uint8_t collisionGroup = 0U;
        std::uint16_t nonCollisionMask = 0U;
        MmdRigidBodyShape shape = MmdRigidBodyShape::Sphere;
        glm::vec3 size{0.0f};
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        float mass = 0.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;
        float restitution = 0.0f;
        float friction = 0.0f;
        MmdRigidBodyMode mode = MmdRigidBodyMode::FollowBone;
    };

    struct JointMetadata
    {
        std::string name;
        MmdJointType type = MmdJointType::Spring6Dof;
        int bodyA = -1;
        int bodyB = -1;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 linearLower{0.0f};
        glm::vec3 linearUpper{0.0f};
        glm::vec3 angularLower{0.0f};
        glm::vec3 angularUpper{0.0f};
        glm::vec3 linearSpring{0.0f};
        glm::vec3 angularSpring{0.0f};
    };

    std::vector<BoneMetadata> bones;
    std::vector<std::uint8_t> assimpCompatibleBytes;
    std::vector<RigidBodyMetadata> rigidBodies;
    std::vector<JointMetadata> joints;
    std::vector<MorphDefinition> morphDefinitions;
    std::vector<VertexMorphMetadata> vertexMorphs;
    std::vector<UvMorphMetadata> uvMorphs;
};

void AppendUtf8(std::string& output, std::uint32_t codePoint)
{
    if (codePoint <= 0x7FU)
        output.push_back(static_cast<char>(codePoint));
    else if (codePoint <= 0x7FFU)
    {
        output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
    else if (codePoint <= 0xFFFFU)
    {
        output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
    else
    {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

class PmxReader
{
public:
    explicit PmxReader(const std::vector<std::uint8_t>& bytes)
        : bytes(bytes)
    {
    }

    std::size_t Position() const noexcept
    {
        return this->offset;
    }

    std::size_t Remaining() const noexcept
    {
        return this->bytes.size() - std::min(this->offset, this->bytes.size());
    }

    template<typename T>
    T Read()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        this->Require(sizeof(T));
        T value{};
        std::memcpy(&value, this->bytes.data() + this->offset, sizeof(T));
        this->offset += sizeof(T);
        return value;
    }

    void Skip(std::size_t count)
    {
        this->Require(count);
        this->offset += count;
    }

    int ReadIndex(unsigned int size)
    {
        switch (size)
        {
        case 1: return static_cast<int>(this->Read<std::int8_t>());
        case 2: return static_cast<int>(this->Read<std::int16_t>());
        case 4: return this->Read<std::int32_t>();
        default: throw std::runtime_error("PMX contains an invalid index size");
        }
    }

    std::uint32_t ReadUnsignedIndex(unsigned int size)
    {
        switch (size)
        {
        case 1: return this->Read<std::uint8_t>();
        case 2: return this->Read<std::uint16_t>();
        case 4: return this->Read<std::uint32_t>();
        default: throw std::runtime_error("PMX contains an invalid index size");
        }
    }

    std::string ReadText(unsigned int encoding)
    {
        const std::int32_t byteCount = this->Read<std::int32_t>();
        if (byteCount < 0)
            throw std::runtime_error("PMX contains a negative text length");
        this->Require(static_cast<std::size_t>(byteCount));

        const std::uint8_t* begin = this->bytes.data() + this->offset;
        this->offset += static_cast<std::size_t>(byteCount);
        if (encoding == 1)
        {
            return std::string(
                reinterpret_cast<const char*>(begin),
                static_cast<std::size_t>(byteCount)
            );
        }
        if (encoding != 0 || (byteCount % 2) != 0)
            throw std::runtime_error("PMX contains invalid text encoding");

        std::string result;
        result.reserve(static_cast<std::size_t>(byteCount));
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(byteCount);
             index += 2)
        {
            std::uint32_t codePoint = static_cast<std::uint32_t>(begin[index]) |
                (static_cast<std::uint32_t>(begin[index + 1]) << 8U);
            if (codePoint >= 0xD800U && codePoint <= 0xDBFFU)
            {
                if (index + 3 >= static_cast<std::size_t>(byteCount))
                    throw std::runtime_error("PMX contains an incomplete UTF-16 surrogate");
                const std::uint32_t low =
                    static_cast<std::uint32_t>(begin[index + 2]) |
                    (static_cast<std::uint32_t>(begin[index + 3]) << 8U);
                if (low < 0xDC00U || low > 0xDFFFU)
                    throw std::runtime_error("PMX contains an invalid UTF-16 surrogate");
                codePoint = 0x10000U +
                    ((codePoint - 0xD800U) << 10U) +
                    (low - 0xDC00U);
                index += 2;
            }
            else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU)
            {
                throw std::runtime_error("PMX contains an unmatched UTF-16 surrogate");
            }
            AppendUtf8(result, codePoint);
        }
        return result;
    }

private:
    void Require(std::size_t count) const
    {
        if (count > this->bytes.size() - std::min(this->offset, this->bytes.size()))
            throw std::runtime_error("PMX file ended unexpectedly");
    }

    const std::vector<std::uint8_t>& bytes;
    std::size_t offset = 0;
};

template<typename T>
std::vector<T> ReadPmxVector(PmxReader& reader, std::size_t count)
{
    std::vector<T> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.push_back(reader.Read<T>());
    return result;
}

glm::vec3 ReadPmxVec3(PmxReader& reader)
{
    const float x = reader.Read<float>();
    const float y = reader.Read<float>();
    const float z = reader.Read<float>();
    return glm::vec3(x, y, z);
}

glm::vec4 ReadPmxVec4(PmxReader& reader)
{
    const float x = reader.Read<float>();
    const float y = reader.Read<float>();
    const float z = reader.Read<float>();
    const float w = reader.Read<float>();
    return glm::vec4(x, y, z, w);
}

glm::quat ReadPmxQuaternion(PmxReader& reader)
{
    const float x = reader.Read<float>();
    const float y = reader.Read<float>();
    const float z = reader.Read<float>();
    const float w = reader.Read<float>();
    const glm::quat converted(w, -x, -y, z);
    const float lengthSquared = glm::dot(converted, converted);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
        throw std::runtime_error("PMX morph contains an invalid quaternion");
    return glm::normalize(converted);
}


bool IsFinitePmx(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

glm::vec3 ConvertPmxPosition(const glm::vec3& value)
{
    if (!IsFinitePmx(value))
        throw std::runtime_error("PMX contains a non-finite vector");
    return glm::vec3(value.x, value.y, -value.z);
}

glm::quat ConvertPmxEulerRotation(const glm::vec3& euler)
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

std::pair<glm::vec3, glm::vec3> ConvertPmxLinearLimits(
    const glm::vec3& lower,
    const glm::vec3& upper
)
{
    const glm::vec3 convertedLower = ConvertPmxPosition(lower);
    const glm::vec3 convertedUpper = ConvertPmxPosition(upper);
    return {
        glm::min(convertedLower, convertedUpper),
        glm::max(convertedLower, convertedUpper)
    };
}

std::pair<glm::vec3, glm::vec3> ConvertPmxAngularLimits(
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

glm::mat4 MakePmxModelTransform(
    const glm::vec3& position,
    const glm::quat& rotation
)
{
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
}

std::optional<std::string> PmxTexturePath(
    const std::vector<std::string>& textures,
    int index
)
{
    if (index < 0)
        return std::nullopt;
    if (static_cast<std::size_t>(index) >= textures.size())
        throw std::runtime_error("PMX material references an invalid texture index");
    return textures[static_cast<std::size_t>(index)];
}

PmxMetadata ParsePmxMetadata(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < 9 || std::memcmp(bytes.data(), "PMX ", 4) != 0)
        throw std::runtime_error("PMX header is invalid");

    PmxReader reader(bytes);
    reader.Skip(4);
    const float version = reader.Read<float>();
    if (!std::isfinite(version) || version < 2.0f || version >= 2.2f)
        throw std::runtime_error("Only PMX 2.0 and 2.1 are supported");

    const std::uint8_t settingCount = reader.Read<std::uint8_t>();
    if (settingCount < 8)
        throw std::runtime_error("PMX global settings are incomplete");
    std::vector<std::uint8_t> settings =
        ReadPmxVector<std::uint8_t>(reader, settingCount);
    const unsigned int encoding = settings[0];
    const unsigned int additionalUvCount = settings[1];
    const unsigned int vertexIndexSize = settings[2];
    const unsigned int textureIndexSize = settings[3];
    const unsigned int materialIndexSize = settings[4];
    const unsigned int boneIndexSize = settings[5];
    const unsigned int morphIndexSize = settings[6];
    const unsigned int rigidBodyIndexSize = settings[7];
    const auto validIndexSize = [](unsigned int size)
    {
        return size == 1U || size == 2U || size == 4U;
    };
    if (additionalUvCount > 4U ||
        !validIndexSize(vertexIndexSize) ||
        !validIndexSize(textureIndexSize) ||
        !validIndexSize(materialIndexSize) ||
        !validIndexSize(boneIndexSize) ||
        !validIndexSize(morphIndexSize) ||
        !validIndexSize(rigidBodyIndexSize))
    {
        throw std::runtime_error("PMX global settings contain invalid sizes");
    }

    for (int index = 0; index < 4; ++index)
        reader.ReadText(encoding);

    const std::int32_t vertexCount = reader.Read<std::int32_t>();
    if (vertexCount < 0)
        throw std::runtime_error("PMX contains a negative vertex count");
    std::vector<float> vertexEdgeScales;
    vertexEdgeScales.reserve(static_cast<std::size_t>(vertexCount));
    for (std::int32_t vertex = 0; vertex < vertexCount; ++vertex)
    {
        reader.Skip(
            (3U + 3U + 2U + additionalUvCount * 4U) * sizeof(float)
        );
        const std::uint8_t skinning = reader.Read<std::uint8_t>();
        switch (skinning)
        {
        case 0: // BDEF1
            reader.Skip(boneIndexSize);
            break;
        case 1: // BDEF2
            reader.Skip(2U * boneIndexSize + sizeof(float));
            break;
        case 2: // BDEF4
        case 4: // QDEF
            reader.Skip(4U * boneIndexSize + 4U * sizeof(float));
            break;
        case 3: // SDEF
            reader.Skip(2U * boneIndexSize + 10U * sizeof(float));
            break;
        default:
            throw std::runtime_error("PMX contains an unsupported skinning type");
        }
        vertexEdgeScales.push_back(reader.Read<float>());
    }

    const std::int32_t indexCount = reader.Read<std::int32_t>();
    if (indexCount < 0)
        throw std::runtime_error("PMX contains a negative surface index count");
    std::vector<std::uint32_t> surfaceIndices;
    surfaceIndices.reserve(static_cast<std::size_t>(indexCount));
    for (std::int32_t index = 0; index < indexCount; ++index)
        surfaceIndices.push_back(reader.ReadUnsignedIndex(vertexIndexSize));

    const std::int32_t textureCount = reader.Read<std::int32_t>();
    if (textureCount < 0)
        throw std::runtime_error("PMX contains a negative texture count");
    std::vector<std::string> textures;
    textures.reserve(static_cast<std::size_t>(textureCount));
    for (std::int32_t index = 0; index < textureCount; ++index)
        textures.push_back(reader.ReadText(encoding));

    const std::int32_t materialCount = reader.Read<std::int32_t>();
    if (materialCount < 0)
        throw std::runtime_error("PMX contains a negative material count");

    PmxMetadata result;
    result.materials.reserve(static_cast<std::size_t>(materialCount));
    result.materialVertexEdgeScales.reserve(
        static_cast<std::size_t>(materialCount)
    );
    result.materialSourceVertexIndices.reserve(
        static_cast<std::size_t>(materialCount)
    );
    std::size_t surfaceOffset = 0;
    for (std::int32_t index = 0; index < materialCount; ++index)
    {
        PmxMaterialMetadata material;
        const std::string localName = reader.ReadText(encoding);
        const std::string englishName = reader.ReadText(encoding);
        material.name = !localName.empty() ? localName : englishName;
        material.diffuse = ReadPmxVec4(reader);
        material.specular = ReadPmxVec3(reader);
        material.specularPower = reader.Read<float>();
        material.ambient = ReadPmxVec3(reader);
        const std::uint8_t flags = reader.Read<std::uint8_t>();
        material.doubleSided = (flags & 0x01U) != 0;
        material.edgeEnabled = (flags & 0x10U) != 0;
        material.edgeColor = ReadPmxVec4(reader);
        material.edgeSize = reader.Read<float>();
        material.diffuseTexture = PmxTexturePath(
            textures,
            reader.ReadIndex(textureIndexSize)
        );
        material.sphereTexture = PmxTexturePath(
            textures,
            reader.ReadIndex(textureIndexSize)
        );
        const std::uint8_t sphereMode = reader.Read<std::uint8_t>();
        if (sphereMode <= static_cast<std::uint8_t>(MmdSphereMapMode::SubTexture))
        {
            material.sphereMode = static_cast<MmdSphereMapMode>(sphereMode);
        }
        const std::uint8_t commonToon = reader.Read<std::uint8_t>();
        if (commonToon != 0)
        {
            material.commonToonIndex = reader.Read<std::uint8_t>();
        }
        else
        {
            material.toonTexture = PmxTexturePath(
                textures,
                reader.ReadIndex(textureIndexSize)
            );
        }
        reader.ReadText(encoding); // Material memo.
        const std::int32_t materialIndexCount = reader.Read<std::int32_t>();
        if (materialIndexCount < 0)
            throw std::runtime_error("PMX material has a negative index count");
        if (static_cast<std::size_t>(materialIndexCount) >
            surfaceIndices.size() - std::min(surfaceOffset, surfaceIndices.size()))
        {
            throw std::runtime_error("PMX material index ranges exceed the surface list");
        }
        std::vector<float> edgeScales;
        std::vector<std::uint32_t> sourceVertexIndices;
        edgeScales.reserve(static_cast<std::size_t>(materialIndexCount));
        sourceVertexIndices.reserve(static_cast<std::size_t>(materialIndexCount));
        for (std::int32_t surface = 0;
             surface < materialIndexCount;
             ++surface)
        {
            const std::uint32_t vertexIndex =
                surfaceIndices[surfaceOffset + static_cast<std::size_t>(surface)];
            if (vertexIndex >= vertexEdgeScales.size())
                throw std::runtime_error("PMX surface references an invalid vertex");
            edgeScales.push_back(vertexEdgeScales[vertexIndex]);
            sourceVertexIndices.push_back(vertexIndex);
        }
        surfaceOffset += static_cast<std::size_t>(materialIndexCount);
        result.materials.push_back(std::move(material));
        result.materialVertexEdgeScales.push_back(std::move(edgeScales));
        result.materialSourceVertexIndices.push_back(
            std::move(sourceVertexIndices)
        );
    }
    if (surfaceOffset != surfaceIndices.size())
        throw std::runtime_error("PMX materials do not cover every surface index");

    const std::int32_t boneCount = reader.Read<std::int32_t>();
    if (boneCount < 0)
        throw std::runtime_error("PMX contains a negative bone count");
    result.bones.reserve(static_cast<std::size_t>(boneCount));
    for (std::int32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
    {
        PmxMetadata::BoneMetadata bone;
        const std::string localName = reader.ReadText(encoding);
        const std::string englishName = reader.ReadText(encoding);
        bone.name = !localName.empty() ? localName : englishName;
        ReadPmxVec3(reader); // Absolute bind position; Assimp owns hierarchy.
        reader.ReadIndex(boneIndexSize); // Parent is already imported by Assimp.
        bone.deformLayer = reader.Read<std::int32_t>();
        bone.flags = reader.Read<std::uint16_t>();

        if ((bone.flags & 0x0001U) != 0U)
            reader.ReadIndex(boneIndexSize);
        else
            ReadPmxVec3(reader);

        if ((bone.flags & (0x0100U | 0x0200U)) != 0U)
        {
            bone.appendSourceIndex = reader.ReadIndex(boneIndexSize);
            bone.appendWeight = reader.Read<float>();
        }
        if ((bone.flags & 0x0400U) != 0U)
            ReadPmxVec3(reader);
        if ((bone.flags & 0x0800U) != 0U)
        {
            ReadPmxVec3(reader);
            ReadPmxVec3(reader);
        }
        if ((bone.flags & 0x2000U) != 0U)
            reader.Read<std::int32_t>();

        if ((bone.flags & 0x0020U) != 0U)
        {
            bone.ikTargetIndex = reader.ReadIndex(boneIndexSize);
            const std::int32_t iterations = reader.Read<std::int32_t>();
            if (iterations <= 0)
                throw std::runtime_error("PMX IK iteration count is invalid");
            bone.ikIterations = static_cast<std::uint32_t>(iterations);
            bone.ikAngleLimit = reader.Read<float>();
            const std::int32_t linkCount = reader.Read<std::int32_t>();
            if (linkCount <= 0)
                throw std::runtime_error("PMX IK chain is empty");
            bone.ikLinks.reserve(static_cast<std::size_t>(linkCount));
            for (std::int32_t linkIndex = 0;
                 linkIndex < linkCount;
                 ++linkIndex)
            {
                PmxMetadata::IkLink link;
                link.boneIndex = reader.ReadIndex(boneIndexSize);
                const std::uint8_t hasLimits = reader.Read<std::uint8_t>();
                if (hasLimits > 1U)
                    throw std::runtime_error("PMX IK limit flag is invalid");
                link.hasLimits = hasLimits != 0U;
                if (link.hasLimits)
                {
                    link.minimumAngle = ReadPmxVec3(reader);
                    link.maximumAngle = ReadPmxVec3(reader);
                }
                bone.ikLinks.push_back(std::move(link));
            }
        }
        result.bones.push_back(std::move(bone));
    }

    const std::size_t morphCountOffset = reader.Position();
    const std::int32_t morphCount = reader.Read<std::int32_t>();
    if (morphCount < 0)
        throw std::runtime_error("PMX contains a negative morph count");
    struct PendingMorph
    {
        std::int32_t sourceIndex = -1;
        std::string name;
        MorphCategory category = MorphCategory::Other;
        MorphKind kind = MorphKind::Vertex;
        std::uint8_t uvChannel = 0U;
        std::vector<std::pair<std::uint32_t, glm::vec3>> vertexOffsets;
        std::vector<std::pair<std::uint32_t, glm::vec4>> uvOffsets;
        std::vector<std::pair<std::int32_t, float>> groupMembers;
        std::vector<std::pair<std::int32_t, float>> flipMembers;
        std::vector<BoneMorphOffset> boneOffsets;
        std::vector<MaterialMorphOffset> materialOffsets;
        std::vector<ImpulseMorphOffset> impulseOffsets;
    };
    std::vector<PendingMorph> pendingMorphs;
    pendingMorphs.reserve(static_cast<std::size_t>(morphCount));
    std::vector<std::uint8_t> assimpMorphBytes;
    bool requiresAssimpMorphSanitization = false;
    std::unordered_set<std::string> supportedMorphNames;
    for (std::int32_t morphIndex = 0; morphIndex < morphCount; ++morphIndex)
    {
        const std::size_t recordStart = reader.Position();
        const std::string localName = reader.ReadText(encoding);
        const std::string englishName = reader.ReadText(encoding);
        const std::string name = !localName.empty() ? localName : englishName;
        const std::uint8_t panel = reader.Read<std::uint8_t>();
        const std::size_t typeOffset = reader.Position();
        const std::uint8_t type = reader.Read<std::uint8_t>();
        const std::int32_t offsetCount = reader.Read<std::int32_t>();
        if (offsetCount < 0)
            throw std::runtime_error("PMX morph has a negative offset count");
        if (panel > static_cast<std::uint8_t>(MorphCategory::Other))
            throw std::runtime_error("PMX morph category is invalid");

        if (type <= 10U)
        {
            if (type >= 9U && version < 2.1f)
            {
                throw std::runtime_error(
                    "PMX 2.0 file contains a PMX 2.1 morph type"
                );
            }
            if (name.empty() || !supportedMorphNames.emplace(name).second)
            {
                throw std::runtime_error(
                    "Supported PMX morph names must be non-empty and unique"
                );
            }

            PendingMorph morph;
            morph.sourceIndex = morphIndex;
            morph.name = name;
            morph.category = static_cast<MorphCategory>(panel);
            if (type == 0U)
                morph.kind = MorphKind::Group;
            else if (type == 1U)
                morph.kind = MorphKind::Vertex;
            else if (type == 2U)
                morph.kind = MorphKind::Bone;
            else if (type <= 7U)
            {
                morph.kind = MorphKind::Uv;
                morph.uvChannel = static_cast<std::uint8_t>(type - 3U);
            }
            else if (type == 8U)
                morph.kind = MorphKind::Material;
            else if (type == 9U)
                morph.kind = MorphKind::Flip;
            else
                morph.kind = MorphKind::Impulse;

            if (type == 0U)
            {
                morph.groupMembers.reserve(
                    static_cast<std::size_t>(offsetCount)
                );
                for (std::int32_t offset = 0; offset < offsetCount; ++offset)
                {
                    const int memberIndex = reader.ReadIndex(morphIndexSize);
                    const float weight = reader.Read<float>();
                    if (memberIndex < 0 || memberIndex >= morphCount ||
                        !std::isfinite(weight))
                    {
                        throw std::runtime_error(
                            "PMX group morph contains an invalid member"
                        );
                    }
                    morph.groupMembers.emplace_back(memberIndex, weight);
                }
            }
            else if (type == 1U)
            {
                morph.vertexOffsets.reserve(
                    static_cast<std::size_t>(offsetCount)
                );
                std::unordered_set<std::uint32_t> affectedVertices;
                for (std::int32_t offset = 0; offset < offsetCount; ++offset)
                {
                    const std::uint32_t vertexIndex =
                        reader.ReadUnsignedIndex(vertexIndexSize);
                    if (vertexIndex >= vertexEdgeScales.size() ||
                        !affectedVertices.emplace(vertexIndex).second)
                    {
                        throw std::runtime_error(
                            "PMX vertex morph references an invalid or duplicate vertex"
                        );
                    }
                    const glm::vec3 value = ReadPmxVec3(reader);
                    morph.vertexOffsets.emplace_back(
                        vertexIndex,
                        glm::vec3(value.x, value.y, -value.z)
                    );
                }
            }
            else if (type == 2U)
            {
                morph.boneOffsets.reserve(static_cast<std::size_t>(offsetCount));
                std::unordered_set<BoneIndex> affectedBones;
                for (std::int32_t offset = 0; offset < offsetCount; ++offset)
                {
                    const int sourceBoneIndex = reader.ReadIndex(boneIndexSize);
                    if (sourceBoneIndex < 0 ||
                        static_cast<std::size_t>(sourceBoneIndex) >=
                            result.bones.size())
                    {
                        throw std::runtime_error(
                            "PMX bone morph references an invalid bone"
                        );
                    }
                    const BoneIndex boneIndex =
                        static_cast<BoneIndex>(sourceBoneIndex);
                    if (!affectedBones.emplace(boneIndex).second)
                    {
                        throw std::runtime_error(
                            "PMX bone morph references a duplicate bone"
                        );
                    }
                    const glm::vec3 translation = ReadPmxVec3(reader);
                    morph.boneOffsets.push_back(BoneMorphOffset{
                        boneIndex,
                        glm::vec3(
                            translation.x,
                            translation.y,
                            -translation.z
                        ),
                        ReadPmxQuaternion(reader)
                    });
                }
            }
            else if (type <= 7U)
            {
                morph.uvOffsets.reserve(static_cast<std::size_t>(offsetCount));
                std::unordered_set<std::uint32_t> affectedVertices;
                for (std::int32_t offset = 0; offset < offsetCount; ++offset)
                {
                    const std::uint32_t vertexIndex =
                        reader.ReadUnsignedIndex(vertexIndexSize);
                    if (vertexIndex >= vertexEdgeScales.size() ||
                        !affectedVertices.emplace(vertexIndex).second)
                    {
                        throw std::runtime_error(
                            "PMX UV morph references an invalid or duplicate vertex"
                        );
                    }
                    morph.uvOffsets.emplace_back(
                        vertexIndex,
                        ReadPmxVec4(reader)
                    );
                }
            }
            else if (type == 8U)
            {
                morph.materialOffsets.reserve(
                    static_cast<std::size_t>(offsetCount)
                );
                for (std::int32_t offset = 0; offset < offsetCount; ++offset)
                {
                    const int sourceMaterialIndex =
                        reader.ReadIndex(materialIndexSize);
                    if (sourceMaterialIndex < -1 ||
                        sourceMaterialIndex >= materialCount)
                    {
                        throw std::runtime_error(
                            "PMX material morph references an invalid material"
                        );
                    }
                    const std::uint8_t operation = reader.Read<std::uint8_t>();
                    if (operation > static_cast<std::uint8_t>(
                            MaterialMorphOperation::Add
                        ))
                    {
                        throw std::runtime_error(
                            "PMX material morph operation is invalid"
                        );
                    }
                    MaterialMorphOffset materialOffset;
                    materialOffset.materialIndex = sourceMaterialIndex < 0
                        ? AllMaterialMorphTargets
                        : static_cast<std::uint32_t>(sourceMaterialIndex);
                    materialOffset.operation =
                        static_cast<MaterialMorphOperation>(operation);
                    materialOffset.diffuse = ReadPmxVec4(reader);
                    materialOffset.specular = ReadPmxVec3(reader);
                    materialOffset.shininess = reader.Read<float>();
                    materialOffset.ambient = ReadPmxVec3(reader);
                    materialOffset.edgeColor = ReadPmxVec4(reader);
                    materialOffset.edgeSize = reader.Read<float>();
                    materialOffset.textureFactor = ReadPmxVec4(reader);
                    materialOffset.sphereTextureFactor = ReadPmxVec4(reader);
                    materialOffset.toonTextureFactor = ReadPmxVec4(reader);
                    morph.materialOffsets.push_back(materialOffset);
                }
            }
            else if (type == 9U)
            {
                morph.flipMembers.reserve(static_cast<std::size_t>(offsetCount));
                for (std::int32_t offset = 0; offset < offsetCount; ++offset)
                {
                    const int memberIndex = reader.ReadIndex(morphIndexSize);
                    const float weight = reader.Read<float>();
                    if (memberIndex < 0 || memberIndex >= morphCount ||
                        !std::isfinite(weight))
                    {
                        throw std::runtime_error(
                            "PMX Flip morph contains an invalid member"
                        );
                    }
                    morph.flipMembers.emplace_back(memberIndex, weight);
                }
            }
            else
            {
                morph.impulseOffsets.reserve(
                    static_cast<std::size_t>(offsetCount)
                );
                for (std::int32_t offset = 0; offset < offsetCount; ++offset)
                {
                    const int rigidBodyIndex =
                        reader.ReadIndex(rigidBodyIndexSize);
                    const std::uint8_t local = reader.Read<std::uint8_t>();
                    const glm::vec3 velocity = ReadPmxVec3(reader);
                    const glm::vec3 torque = ReadPmxVec3(reader);
                    const auto finite = [](const glm::vec3& value)
                    {
                        return std::isfinite(value.x) &&
                            std::isfinite(value.y) &&
                            std::isfinite(value.z);
                    };
                    if (rigidBodyIndex < 0 || local > 1U ||
                        !finite(velocity) || !finite(torque))
                    {
                        throw std::runtime_error(
                            "PMX Impulse morph contains an invalid offset"
                        );
                    }
                    morph.impulseOffsets.push_back(ImpulseMorphOffset{
                        static_cast<RigidBodyIndex>(rigidBodyIndex),
                        local != 0U,
                        glm::vec3(velocity.x, velocity.y, -velocity.z),
                        // Angular impulse is an axial vector. Reflecting the
                        // PMX Z axis therefore negates X/Y, like quaternion
                        // handedness conversion, while preserving Z.
                        glm::vec3(-torque.x, -torque.y, torque.z)
                    });
                }
            }
            pendingMorphs.push_back(std::move(morph));

            const std::size_t recordEnd = reader.Position();
            if (type <= 8U)
            {
                assimpMorphBytes.insert(
                    assimpMorphBytes.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(recordStart),
                    bytes.begin() + static_cast<std::ptrdiff_t>(recordEnd)
                );
            }
            else
            {
                requiresAssimpMorphSanitization = true;
                // Assimp's MMD importer currently rejects PMX 2.1 Flip and
                // Impulse records. Preserve their source indices but replace
                // only the Assimp-facing copy with an empty Group morph.
                assimpMorphBytes.insert(
                    assimpMorphBytes.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(recordStart),
                    bytes.begin() + static_cast<std::ptrdiff_t>(typeOffset)
                );
                assimpMorphBytes.push_back(0U);
                assimpMorphBytes.insert(
                    assimpMorphBytes.end(),
                    sizeof(std::int32_t),
                    0U
                );
            }
            continue;
        }

        throw std::runtime_error("PMX contains an unsupported morph type");
    }

    const std::size_t morphSectionEnd = reader.Position();
    if (requiresAssimpMorphSanitization)
    {
        result.assimpCompatibleBytes.reserve(
            bytes.size() -
                (morphSectionEnd -
                    (morphCountOffset + sizeof(std::int32_t))) +
            assimpMorphBytes.size()
        );
        result.assimpCompatibleBytes.insert(
            result.assimpCompatibleBytes.end(),
            bytes.begin(),
            bytes.begin() + static_cast<std::ptrdiff_t>(
                morphCountOffset + sizeof(std::int32_t)
            )
        );
        result.assimpCompatibleBytes.insert(
            result.assimpCompatibleBytes.end(),
            assimpMorphBytes.begin(),
            assimpMorphBytes.end()
        );
        result.assimpCompatibleBytes.insert(
            result.assimpCompatibleBytes.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(morphSectionEnd),
            bytes.end()
        );
    }

    const std::int32_t displayFrameCount = reader.Read<std::int32_t>();
    if (displayFrameCount < 0)
        throw std::runtime_error("PMX contains a negative display-frame count");
    for (std::int32_t frame = 0; frame < displayFrameCount; ++frame)
    {
        reader.ReadText(encoding);
        reader.ReadText(encoding);
        const std::uint8_t special = reader.Read<std::uint8_t>();
        if (special > 1U)
            throw std::runtime_error("PMX display-frame flag is invalid");
        const std::int32_t elementCount = reader.Read<std::int32_t>();
        if (elementCount < 0)
            throw std::runtime_error("PMX display frame has a negative size");
        for (std::int32_t element = 0; element < elementCount; ++element)
        {
            const std::uint8_t target = reader.Read<std::uint8_t>();
            if (target == 0U)
                reader.ReadIndex(boneIndexSize);
            else if (target == 1U)
                reader.ReadIndex(morphIndexSize);
            else
                throw std::runtime_error("PMX display-frame target is invalid");
        }
    }

    const auto finiteScalar = [](float value) noexcept
    {
        return std::isfinite(value);
    };
    const auto physicsName = [](std::string local,
                                std::string english,
                                std::string_view prefix,
                                std::int32_t index)
    {
        if (!local.empty())
            return local;
        if (!english.empty())
            return english;
        return std::string(prefix) + " " + std::to_string(index);
    };

    const std::int32_t rigidBodyCount = reader.Read<std::int32_t>();
    if (rigidBodyCount < 0)
        throw std::runtime_error("PMX contains a negative rigid-body count");
    result.rigidBodies.reserve(static_cast<std::size_t>(rigidBodyCount));
    for (std::int32_t bodyIndex = 0;
         bodyIndex < rigidBodyCount;
         ++bodyIndex)
    {
        PmxMetadata::RigidBodyMetadata body;
        body.name = physicsName(
            reader.ReadText(encoding),
            reader.ReadText(encoding),
            "Rigid Body",
            bodyIndex
        );
        body.boneIndex = reader.ReadIndex(boneIndexSize);
        if (body.boneIndex < -1 || body.boneIndex >= boneCount)
        {
            throw std::runtime_error(
                "PMX rigid body references an invalid bone"
            );
        }
        body.collisionGroup = reader.Read<std::uint8_t>();
        if (body.collisionGroup >= 16U)
        {
            throw std::runtime_error(
                "PMX rigid-body collision group is out of range"
            );
        }
        body.nonCollisionMask = reader.Read<std::uint16_t>();
        const std::uint8_t shape = reader.Read<std::uint8_t>();
        if (shape > static_cast<std::uint8_t>(MmdRigidBodyShape::Capsule))
            throw std::runtime_error("PMX rigid-body shape is invalid");
        body.shape = static_cast<MmdRigidBodyShape>(shape);
        body.size = ReadPmxVec3(reader);
        if (!IsFinitePmx(body.size) ||
            body.size.x < 0.0f || body.size.y < 0.0f || body.size.z < 0.0f)
        {
            throw std::runtime_error("PMX rigid-body size is invalid");
        }
        body.position = ConvertPmxPosition(ReadPmxVec3(reader));
        body.rotation = ConvertPmxEulerRotation(ReadPmxVec3(reader));
        body.mass = reader.Read<float>();
        body.linearDamping = reader.Read<float>();
        body.angularDamping = reader.Read<float>();
        body.restitution = reader.Read<float>();
        body.friction = reader.Read<float>();
        if (!finiteScalar(body.mass) || body.mass < 0.0f ||
            !finiteScalar(body.linearDamping) || body.linearDamping < 0.0f ||
            !finiteScalar(body.angularDamping) || body.angularDamping < 0.0f ||
            !finiteScalar(body.restitution) || body.restitution < 0.0f ||
            !finiteScalar(body.friction) || body.friction < 0.0f)
        {
            throw std::runtime_error(
                "PMX rigid-body physical parameters are invalid"
            );
        }
        const std::uint8_t mode = reader.Read<std::uint8_t>();
        if (mode > static_cast<std::uint8_t>(
                MmdRigidBodyMode::PhysicsWithBone
            ))
        {
            throw std::runtime_error("PMX rigid-body mode is invalid");
        }
        body.mode = static_cast<MmdRigidBodyMode>(mode);
        result.rigidBodies.push_back(std::move(body));
    }

    for (const PendingMorph& pending : pendingMorphs)
    {
        for (const ImpulseMorphOffset& offset : pending.impulseOffsets)
        {
            if (static_cast<std::size_t>(offset.rigidBodyIndex) >=
                result.rigidBodies.size())
            {
                throw std::runtime_error(
                    "PMX Impulse morph references an invalid rigid body"
                );
            }
        }
    }

    const std::int32_t jointCount = reader.Read<std::int32_t>();
    if (jointCount < 0)
        throw std::runtime_error("PMX contains a negative joint count");
    result.joints.reserve(static_cast<std::size_t>(jointCount));
    for (std::int32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
    {
        PmxMetadata::JointMetadata joint;
        joint.name = physicsName(
            reader.ReadText(encoding),
            reader.ReadText(encoding),
            "Joint",
            jointIndex
        );
        const std::uint8_t type = reader.Read<std::uint8_t>();
        if (type > static_cast<std::uint8_t>(MmdJointType::Hinge) ||
            (version < 2.1f && type != 0U))
        {
            throw std::runtime_error("PMX joint type is invalid");
        }
        joint.type = static_cast<MmdJointType>(type);
        joint.bodyA = reader.ReadIndex(rigidBodyIndexSize);
        joint.bodyB = reader.ReadIndex(rigidBodyIndexSize);
        const auto validBody = [&result](int index) noexcept
        {
            return index == -1 ||
                (index >= 0 && static_cast<std::size_t>(index) <
                    result.rigidBodies.size());
        };
        if (!validBody(joint.bodyA) || !validBody(joint.bodyB) ||
            (joint.bodyA < 0 && joint.bodyB < 0))
        {
            throw std::runtime_error(
                "PMX joint references an invalid rigid body"
            );
        }
        joint.position = ConvertPmxPosition(ReadPmxVec3(reader));
        joint.rotation = ConvertPmxEulerRotation(ReadPmxVec3(reader));
        const auto [linearLower, linearUpper] = ConvertPmxLinearLimits(
            ReadPmxVec3(reader),
            ReadPmxVec3(reader)
        );
        joint.linearLower = linearLower;
        joint.linearUpper = linearUpper;
        const auto [angularLower, angularUpper] = ConvertPmxAngularLimits(
            ReadPmxVec3(reader),
            ReadPmxVec3(reader)
        );
        joint.angularLower = angularLower;
        joint.angularUpper = angularUpper;
        joint.linearSpring = ReadPmxVec3(reader);
        joint.angularSpring = ReadPmxVec3(reader);
        if (!IsFinitePmx(joint.linearSpring) ||
            !IsFinitePmx(joint.angularSpring))
        {
            throw std::runtime_error("PMX joint spring values are invalid");
        }
        result.joints.push_back(std::move(joint));
    }

    if (version >= 2.1f && reader.Remaining() >= sizeof(std::int32_t))
    {
        const std::int32_t softBodyCount = reader.Read<std::int32_t>();
        if (softBodyCount < 0)
            throw std::runtime_error("PMX contains a negative soft-body count");
        if (softBodyCount > 0)
        {
            throw std::runtime_error(
                "PMX 2.1 Soft Body is not supported by Physics 1"
            );
        }
    }

    std::vector<MorphIndex> sourceToRuntime(
        static_cast<std::size_t>(morphCount),
        InvalidMorphIndex
    );
    result.morphDefinitions.reserve(pendingMorphs.size());
    result.vertexMorphs.reserve(pendingMorphs.size());
    result.uvMorphs.reserve(pendingMorphs.size());
    for (PendingMorph& pending : pendingMorphs)
    {
        const MorphIndex runtimeIndex = static_cast<MorphIndex>(
            result.morphDefinitions.size()
        );
        sourceToRuntime[static_cast<std::size_t>(pending.sourceIndex)] =
            runtimeIndex;
        MorphDefinition definition;
        definition.name = pending.name;
        definition.category = pending.category;
        definition.kind = pending.kind;
        definition.boneOffsets = std::move(pending.boneOffsets);
        definition.materialOffsets = std::move(pending.materialOffsets);
        definition.impulseOffsets = std::move(pending.impulseOffsets);
        result.morphDefinitions.push_back(std::move(definition));
        if (pending.kind == MorphKind::Vertex)
        {
            result.vertexMorphs.push_back(
                PmxMetadata::VertexMorphMetadata{
                    runtimeIndex,
                    pending.name,
                    pending.category,
                    std::move(pending.vertexOffsets)
                }
            );
        }
        else if (pending.kind == MorphKind::Uv)
        {
            result.uvMorphs.push_back(PmxMetadata::UvMorphMetadata{
                runtimeIndex,
                pending.uvChannel,
                std::move(pending.uvOffsets)
            });
        }
    }
    for (const PendingMorph& pending : pendingMorphs)
    {
        const MorphIndex runtimeIndex = sourceToRuntime[
            static_cast<std::size_t>(pending.sourceIndex)
        ];
        MorphDefinition& definition = result.morphDefinitions[runtimeIndex];
        if (pending.kind == MorphKind::Group)
        {
            definition.groupMembers.reserve(pending.groupMembers.size());
            for (const auto& [sourceMemberIndex, weight] : pending.groupMembers)
            {
                const MorphIndex memberIndex = sourceToRuntime[
                    static_cast<std::size_t>(sourceMemberIndex)
                ];
                if (memberIndex == InvalidMorphIndex)
                {
                    throw std::runtime_error(
                        "PMX Group morph references an unavailable morph"
                    );
                }
                definition.groupMembers.push_back(GroupMorphMember{
                    memberIndex,
                    weight
                });
            }
        }
        else if (pending.kind == MorphKind::Flip)
        {
            definition.flipMembers.reserve(pending.flipMembers.size());
            for (const auto& [sourceMemberIndex, weight] : pending.flipMembers)
            {
                const MorphIndex memberIndex = sourceToRuntime[
                    static_cast<std::size_t>(sourceMemberIndex)
                ];
                if (memberIndex == InvalidMorphIndex)
                {
                    throw std::runtime_error(
                        "PMX Flip morph references an unavailable morph"
                    );
                }
                definition.flipMembers.push_back(FlipMorphMember{
                    memberIndex,
                    weight
                });
            }
        }
    }
    if (!result.morphDefinitions.empty())
    {
        const MorphSet validation(result.morphDefinitions);
        (void)validation;
    }
    return result;
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
