#include "wisteria/common/pch.hpp"

#include "pmx_parser.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
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

}  // namespace (parser helpers)

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
