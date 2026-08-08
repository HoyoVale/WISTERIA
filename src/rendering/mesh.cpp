#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/rendering/vao.hpp"
#include "wisteria/animation/pose.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace wisteria
{
namespace
{
std::size_t DataTypeSize(DataType type)
{
    switch (type)
    {
    case FLOAT: return sizeof(float);
    case INT: return sizeof(int);
    case UINT: return sizeof(unsigned int);
    case UCHAR: return sizeof(unsigned char);
    }
    throw std::invalid_argument("Mesh layout contains an unsupported data type");
}

glm::vec3 CalculateBoundsCenter(const DefaultModelData& data)
{
    if (data.vertices.empty() || data.layout.empty())
        return glm::vec3(0.0f);

    std::size_t strideBytes = 0;
    std::size_t positionOffsetBytes = 0;
    bool foundPosition = false;
    for (const Layout& attribute : data.layout)
    {
        if (!foundPosition && attribute.name == "position")
        {
            if (attribute.type != FLOAT || attribute.size < 3)
            {
                throw std::invalid_argument(
                    "Mesh position must contain at least three FLOAT components"
                );
            }
            positionOffsetBytes = strideBytes;
            foundPosition = true;
        }
        strideBytes += attribute.size * DataTypeSize(attribute.type);
    }
    if (!foundPosition || strideBytes == 0 ||
        data.VertexBytes() % strideBytes != 0)
    {
        throw std::invalid_argument("Mesh vertex data does not match its layout");
    }

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(
        data.vertices.data()
    );
    const std::size_t vertexCount = data.VertexBytes() / strideBytes;
    for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
    {
        float positionComponents[3]{};
        std::memcpy(
            positionComponents,
            bytes + vertex * strideBytes + positionOffsetBytes,
            3 * sizeof(float)
        );
        const glm::vec3 position(
            positionComponents[0],
            positionComponents[1],
            positionComponents[2]
        );
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    return (minimum + maximum) * 0.5f;
}

std::size_t CalculateVertexCount(const DefaultModelData& data)
{
    if (data.vertices.empty())
        return 0U;
    std::size_t strideBytes = 0U;
    for (const Layout& attribute : data.layout)
        strideBytes += attribute.size * DataTypeSize(attribute.type);
    if (strideBytes == 0U || data.VertexBytes() % strideBytes != 0U)
        throw std::invalid_argument("Mesh vertex data does not match its layout");
    return data.VertexBytes() / strideBytes;
}

}

Mesh::Mesh(
    DefaultModelData data,
    std::size_t requiredBoneCount,
    std::vector<MeshMorphTarget> morphTargets,
    std::vector<std::uint32_t> sourceVertexIndices,
    GraphicsDevice* device
)
    : device(device),
      data(std::move(data)),
      morphTargets(std::move(morphTargets)),
      requiredBoneCount(requiredBoneCount),
      sourceVertexIndices(std::move(sourceVertexIndices))
{
    const auto hasAttribute = [this](const char* name)
    {
        return std::any_of(
            this->data.layout.begin(),
            this->data.layout.end(),
            [name](const Layout& attribute)
            {
                return attribute.name == name;
            }
        );
    };
    const bool hasBoneIndices = hasAttribute("boneIndices");
    const bool hasBoneWeights = hasAttribute("boneWeights");
    if (hasBoneIndices != hasBoneWeights ||
        (this->requiredBoneCount > 0 && !hasBoneIndices) ||
        (this->requiredBoneCount == 0 && hasBoneIndices))
    {
        throw std::invalid_argument(
            "Mesh skinning metadata does not match its vertex layout"
        );
    }
    this->vertexCount = CalculateVertexCount(this->data);
    this->localBoundsCenter = CalculateBoundsCenter(this->data);

    if (this->requiredBoneCount > 0U)
    {
        std::size_t strideBytes = 0U;
        std::size_t positionOffset = 0U;
        std::size_t boneIndexOffset = 0U;
        std::size_t boneWeightOffset = 0U;
        bool foundPosition = false;
        bool foundBoneIndices = false;
        bool foundBoneWeights = false;
        for (const Layout& attribute : this->data.layout)
        {
            if (attribute.name == "position")
            {
                if (attribute.type != FLOAT || attribute.size < 3U)
                    throw std::invalid_argument("Skinned Mesh position layout is invalid");
                positionOffset = strideBytes;
                foundPosition = true;
            }
            else if (attribute.name == "boneIndices")
            {
                if (attribute.type != FLOAT || attribute.size != 4U)
                    throw std::invalid_argument("Skinned Mesh bone index layout is invalid");
                boneIndexOffset = strideBytes;
                foundBoneIndices = true;
            }
            else if (attribute.name == "boneWeights")
            {
                if (attribute.type != FLOAT || attribute.size != 4U)
                    throw std::invalid_argument("Skinned Mesh bone weight layout is invalid");
                boneWeightOffset = strideBytes;
                foundBoneWeights = true;
            }
            strideBytes += attribute.size * DataTypeSize(attribute.type);
        }
        if (!foundPosition || !foundBoneIndices || !foundBoneWeights ||
            strideBytes == 0U)
        {
            throw std::invalid_argument(
                "Skinned Mesh is missing debug-readable skinning attributes"
            );
        }

        this->skinningDebugVertices.resize(this->vertexCount);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(
            this->data.vertices.data()
        );
        for (std::size_t vertexIndex = 0U;
             vertexIndex < this->vertexCount;
             ++vertexIndex)
        {
            const std::uint8_t* vertex = bytes + vertexIndex * strideBytes;
            float position[3]{};
            float indices[4]{};
            float weights[4]{};
            std::memcpy(position, vertex + positionOffset, sizeof(position));
            std::memcpy(indices, vertex + boneIndexOffset, sizeof(indices));
            std::memcpy(weights, vertex + boneWeightOffset, sizeof(weights));

            SkinningDebugVertex& debugVertex =
                this->skinningDebugVertices[vertexIndex];
            debugVertex.position = glm::vec3(
                position[0], position[1], position[2]
            );
            debugVertex.boneWeights = glm::vec4(
                weights[0], weights[1], weights[2], weights[3]
            );
            for (std::size_t influence = 0U; influence < 4U; ++influence)
            {
                const float rounded = std::floor(indices[influence] + 0.5f);
                if (!std::isfinite(rounded) || rounded < 0.0f ||
                    rounded >= static_cast<float>(this->requiredBoneCount))
                {
                    throw std::invalid_argument(
                        "Skinned Mesh contains an invalid bone index"
                    );
                }
                debugVertex.boneIndices[influence] =
                    static_cast<BoneIndex>(rounded);
            }
        }
    }

    std::unordered_set<MorphIndex> targetIndices;
    for (const MeshMorphTarget& target : this->morphTargets)
    {
        if (target.morphIndex == InvalidMorphIndex ||
            (target.offsets.empty() && target.uvOffsets.empty()) ||
            !targetIndices.emplace(target.morphIndex).second)
        {
            throw std::invalid_argument("Mesh morph target metadata is invalid");
        }
        std::unordered_set<std::uint32_t> affectedVertices;
        for (const VertexMorphOffset& offset : target.offsets)
        {
            if (offset.vertexIndex >= this->vertexCount ||
                !std::isfinite(offset.offset.x) ||
                !std::isfinite(offset.offset.y) ||
                !std::isfinite(offset.offset.z) ||
                !affectedVertices.emplace(offset.vertexIndex).second)
            {
                throw std::invalid_argument(
                    "Mesh morph target contains an invalid vertex offset"
                );
            }
        }
        std::unordered_set<std::uint64_t> affectedUvCoordinates;
        for (const UvMorphOffset& offset : target.uvOffsets)
        {
            const std::uint64_t key =
                static_cast<std::uint64_t>(offset.channel) << 32U |
                static_cast<std::uint64_t>(offset.vertexIndex);
            if (offset.vertexIndex >= this->vertexCount ||
                offset.channel >= MmdUvChannelCount ||
                !std::isfinite(offset.offset.x) ||
                !std::isfinite(offset.offset.y) ||
                !std::isfinite(offset.offset.z) ||
                !std::isfinite(offset.offset.w) ||
                !affectedUvCoordinates.emplace(key).second)
            {
                throw std::invalid_argument(
                    "Mesh morph target contains an invalid UV offset"
                );
            }
        }
    }
}

void Mesh::Attach()
{
    if (this->attached)
        return;

    auto nextVbo = std::make_unique<VBO>(this->device);
    auto nextEbo = std::make_unique<EBO>(this->device);

    nextVbo->Upload(
        this->data.vertices.data(),
        this->data.VertexBytes()
    );
    nextEbo->Upload(
        this->data.indices.data(),
        this->data.IndexBytes()
    );

    this->vbo.swap(nextVbo);
    this->ebo.swap(nextEbo);
    this->attached = true;
}

void Mesh::ConfigureVertexArray(VAO& vao)
{
    if (!this->attached)
    {
        throw std::logic_error(
            "Mesh buffers must be attached before configuring a vertex array"
        );
    }

    vao.Bind();
    vao.BindBuffer(*this->vbo, this->data.layout);
    this->ebo->Bind();
    vao.unBind();
}

void Mesh::Draw()
{
    if (!this->attached)
        throw std::logic_error("Mesh must be attached before drawing");

    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(this->data.IndexCount()),
        this->data.IndexGLType(),
        nullptr
    );
}

bool Mesh::IsAttached() const noexcept
{
    return this->attached;
}

std::size_t Mesh::IndexCount() const noexcept
{
    return this->data.IndexCount();
}

const glm::vec3& Mesh::LocalBoundsCenter() const noexcept
{
    return this->localBoundsCenter;
}

bool Mesh::IsSkinned() const noexcept
{
    return this->requiredBoneCount > 0;
}

std::size_t Mesh::RequiredBoneCount() const noexcept
{
    return this->requiredBoneCount;
}

std::size_t Mesh::VertexCount() const noexcept
{
    return this->vertexCount;
}

std::unique_ptr<Mesh> Mesh::CloneForInstance() const
{
    return std::make_unique<Mesh>(
        this->data,
        this->requiredBoneCount,
        this->morphTargets,
        this->sourceVertexIndices,
        this->device
    );
}

std::vector<float> Mesh::RebuildInterleavedVertices(
    const std::vector<float>& sourceVertices,
    std::span<const Layout> layout,
    std::span<const glm::vec3> positions,
    std::span<const glm::vec3> normals,
    std::size_t vertexCount,
    std::span<const glm::vec2> uvs
)
{
    if (positions.size() != vertexCount ||
        normals.size() != vertexCount ||
        (!uvs.empty() && uvs.size() != vertexCount))
    {
        throw std::invalid_argument(
            "Dynamic vertex upload size does not match the mesh"
        );
    }

    std::size_t stride = 0U;
    std::size_t positionOffset = 0U;
    std::size_t normalOffset = 0U;
    std::size_t texCoordOffset = 0U;
    bool foundPosition = false;
    bool foundNormal = false;
    bool foundTexCoord = false;
    for (const Layout& attribute : layout)
    {
        if (attribute.name == "position")
        {
            positionOffset = stride;
            foundPosition = true;
        }
        else if (attribute.name == "normal")
        {
            normalOffset = stride;
            foundNormal = true;
        }
        else if (attribute.name == "texCoord")
        {
            texCoordOffset = stride;
            foundTexCoord = true;
        }
        stride += attribute.size;
    }
    if (!foundPosition || !foundNormal)
    {
        throw std::invalid_argument(
            "Dynamic vertex upload requires position and normal attributes"
        );
    }
    if (!uvs.empty() && !foundTexCoord)
    {
        throw std::invalid_argument(
            "Dynamic UV upload requires a texCoord attribute"
        );
    }
    if (positionOffset + 3U > stride ||
        normalOffset + 3U > stride)
    {
        throw std::invalid_argument(
            "Dynamic vertex layout position/normal exceed the vertex stride"
        );
    }
    if (sourceVertices.size() < vertexCount * stride)
    {
        throw std::invalid_argument(
            "Dynamic vertex source array is shorter than vertexCount * stride"
        );
    }

    // The vertex buffer is interleaved (one 26-float vertex per stride), so
    // position/normal cannot be written as contiguous blocks. Rebuild the
    // interleaved array and upload it as one block.
    std::vector<float> updatedVertices = sourceVertices;
    for (std::size_t vertexIndex = 0U; vertexIndex < vertexCount; ++vertexIndex)
    {
        const std::size_t base = vertexIndex * stride;
        updatedVertices[base + positionOffset + 0U] =
            positions[vertexIndex].x;
        updatedVertices[base + positionOffset + 1U] =
            positions[vertexIndex].y;
        updatedVertices[base + positionOffset + 2U] =
            positions[vertexIndex].z;
        updatedVertices[base + normalOffset + 0U] =
            normals[vertexIndex].x;
        updatedVertices[base + normalOffset + 1U] =
            normals[vertexIndex].y;
        updatedVertices[base + normalOffset + 2U] =
            normals[vertexIndex].z;
        if (!uvs.empty())
        {
            updatedVertices[base + texCoordOffset + 0U] =
                uvs[vertexIndex].x;
            updatedVertices[base + texCoordOffset + 1U] =
                uvs[vertexIndex].y;
        }
    }
    return updatedVertices;
}

void Mesh::UploadDynamicFrame(
    std::span<const glm::vec3> positions,
    std::span<const glm::vec3> normals,
    std::span<const glm::vec2> uvs
)
{
    std::vector<float> updatedVertices = RebuildInterleavedVertices(
        this->data.vertices,
        this->data.layout,
        positions,
        normals,
        this->vertexCount,
        uvs
    );
    this->dynamicVertexSource = true;
    if (!this->attached || this->vbo == nullptr)
        return;

    glBindBuffer(GL_ARRAY_BUFFER, this->vbo->GetVBO());
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(
            updatedVertices.size() * sizeof(float)
        ),
        updatedVertices.data()
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Mesh::UploadDynamicVertices(
    std::span<const glm::vec3> positions,
    std::span<const glm::vec3> normals
)
{
    this->UploadDynamicFrame(positions, normals);
}

bool Mesh::HasDynamicVertexSource() const noexcept
{
    return this->dynamicVertexSource;
}

void Mesh::SetDynamicVertexProvider(
    MeshDynamicVertexProvider provider
)
{
    this->dynamicVertexProvider = std::move(provider);
}

const MeshDynamicVertexProvider&
Mesh::DynamicVertexProvider() const noexcept
{
    return this->dynamicVertexProvider;
}

std::shared_ptr<const void> Mesh::LifetimeToken() const noexcept
{
    return this->lifetimeToken;
}

std::span<const std::uint32_t> Mesh::SourceVertexIndices() const noexcept
{
    return this->sourceVertexIndices;
}

bool Mesh::HasMorphTargets() const noexcept
{
    return !this->morphTargets.empty();
}

std::size_t Mesh::MorphTargetCount() const noexcept
{
    return this->morphTargets.size();
}

bool Mesh::CalculateMorphOffsets(
    std::span<const float> weights,
    std::vector<glm::vec3>& output
) const
{
    bool active = false;
    for (const MeshMorphTarget& target : this->morphTargets)
    {
        if (static_cast<std::size_t>(target.morphIndex) >= weights.size())
            throw std::invalid_argument("Mesh morph target has no matching weight");
        const float weight = weights[target.morphIndex];
        if (!std::isfinite(weight))
            throw std::invalid_argument("Mesh morph weight must be finite");
        if (weight != 0.0f && !target.offsets.empty())
            active = true;
    }
    if (!active)
    {
        output.clear();
        return false;
    }

    output.assign(this->vertexCount, glm::vec3(0.0f));
    for (const MeshMorphTarget& target : this->morphTargets)
    {
        const float weight = weights[target.morphIndex];
        if (weight == 0.0f)
            continue;
        for (const VertexMorphOffset& offset : target.offsets)
            output[offset.vertexIndex] += offset.offset * weight;
    }
    return true;
}

bool Mesh::CalculateMorphDeltas(
    std::span<const float> weights,
    std::vector<MorphVertexDelta>& output
) const
{
    bool active = false;
    for (const MeshMorphTarget& target : this->morphTargets)
    {
        if (static_cast<std::size_t>(target.morphIndex) >= weights.size())
            throw std::invalid_argument("Mesh morph target has no matching weight");
        const float weight = weights[target.morphIndex];
        if (!std::isfinite(weight))
            throw std::invalid_argument("Mesh morph weight must be finite");
        if (weight != 0.0f)
            active = true;
    }
    if (!active)
    {
        output.clear();
        return false;
    }

    output.assign(this->vertexCount, MorphVertexDelta{});
    for (const MeshMorphTarget& target : this->morphTargets)
    {
        const float weight = weights[target.morphIndex];
        if (weight == 0.0f)
            continue;
        for (const VertexMorphOffset& offset : target.offsets)
            output[offset.vertexIndex].position += offset.offset * weight;
        for (const UvMorphOffset& offset : target.uvOffsets)
            output[offset.vertexIndex].uv[offset.channel] += offset.offset * weight;
    }
    return true;
}

std::size_t Mesh::AppendSkinningDebugLines(
    std::vector<PhysicsDebugLine>& lines,
    const Pose& pose,
    std::span<const std::uint8_t> drivenBoneModes,
    const glm::mat4& modelMatrix,
    const MorphState* morphState,
    std::size_t maximumSamples
) const
{
    if (!this->IsSkinned() || this->skinningDebugVertices.empty() ||
        maximumSamples == 0U)
    {
        return 0U;
    }
    if (pose.BoneCount() < this->requiredBoneCount ||
        drivenBoneModes.size() < this->requiredBoneCount)
    {
        throw std::invalid_argument(
            "Skinning debug data does not match the Mesh skeleton"
        );
    }

    std::vector<glm::vec3> morphOffsets;
    const bool hasMorphOffsets = morphState != nullptr &&
        this->CalculateMorphOffsets(
            morphState->EffectiveWeights(),
            morphOffsets
        );
    const std::span<const glm::mat4> skinningMatrices =
        pose.SkinningMatrices();
    const Skeleton& skeleton = pose.GetSkeleton();
    const std::span<const glm::mat4> globalMatrices = pose.GlobalMatrices();

    const auto drivenInfluence = [&drivenBoneModes](
        const SkinningDebugVertex& vertex
    )
    {
        for (std::size_t influence = 0U; influence < 4U; ++influence)
        {
            const BoneIndex bone = vertex.boneIndices[influence];
            if (vertex.boneWeights[influence] > 0.05f &&
                static_cast<std::size_t>(bone) < drivenBoneModes.size() &&
                drivenBoneModes[bone] != 0U)
            {
                return true;
            }
        }
        return false;
    };

    std::size_t candidateCount = 0U;
    for (const SkinningDebugVertex& vertex : this->skinningDebugVertices)
        candidateCount += drivenInfluence(vertex) ? 1U : 0U;
    if (candidateCount == 0U)
        return 0U;

    const std::size_t candidateStride = std::max<std::size_t>(
        1U,
        (candidateCount + maximumSamples - 1U) / maximumSamples
    );
    const float basisScale = (
        glm::length(glm::vec3(modelMatrix[0])) +
        glm::length(glm::vec3(modelMatrix[1])) +
        glm::length(glm::vec3(modelMatrix[2]))
    ) / 3.0f;
    const float crossSize = std::max(0.0025f, basisScale * 0.0125f);
    constexpr glm::vec3 VertexColor{0.82f, 0.18f, 1.0f};
    constexpr glm::vec3 BoneToVertexColor{0.48f, 0.08f, 0.68f};

    std::size_t candidateOrdinal = 0U;
    std::size_t sampled = 0U;
    for (std::size_t vertexIndex = 0U;
         vertexIndex < this->skinningDebugVertices.size() &&
             sampled < maximumSamples;
         ++vertexIndex)
    {
        const SkinningDebugVertex& vertex =
            this->skinningDebugVertices[vertexIndex];
        if (!drivenInfluence(vertex))
            continue;
        const bool selected = candidateOrdinal % candidateStride == 0U;
        ++candidateOrdinal;
        if (!selected)
            continue;

        glm::mat4 skinMatrix(0.0f);
        BoneIndex dominantBone = vertex.boneIndices[0U];
        float dominantWeight = -1.0f;
        for (std::size_t influence = 0U; influence < 4U; ++influence)
        {
            const BoneIndex bone = vertex.boneIndices[influence];
            const float weight = vertex.boneWeights[influence];
            skinMatrix += skinningMatrices[bone] * weight;
            if (weight > dominantWeight && drivenBoneModes[bone] != 0U)
            {
                dominantWeight = weight;
                dominantBone = bone;
            }
        }

        glm::vec3 sourcePosition = vertex.position;
        if (hasMorphOffsets)
            sourcePosition += morphOffsets[vertexIndex];
        const glm::vec3 worldPosition = glm::vec3(
            modelMatrix * skinMatrix * glm::vec4(sourcePosition, 1.0f)
        );

        lines.push_back(PhysicsDebugLine{
            worldPosition - glm::vec3(crossSize, 0.0f, 0.0f),
            worldPosition + glm::vec3(crossSize, 0.0f, 0.0f),
            VertexColor
        });
        lines.push_back(PhysicsDebugLine{
            worldPosition - glm::vec3(0.0f, crossSize, 0.0f),
            worldPosition + glm::vec3(0.0f, crossSize, 0.0f),
            VertexColor
        });
        lines.push_back(PhysicsDebugLine{
            worldPosition - glm::vec3(0.0f, 0.0f, crossSize),
            worldPosition + glm::vec3(0.0f, 0.0f, crossSize),
            VertexColor
        });

        const glm::mat4 boneModel =
            skeleton.InverseRootMatrix() * globalMatrices[dominantBone];
        const glm::vec3 boneWorldPosition = glm::vec3(
            modelMatrix * glm::vec4(glm::vec3(boneModel[3]), 1.0f)
        );
        lines.push_back(PhysicsDebugLine{
            boneWorldPosition,
            worldPosition,
            BoneToVertexColor
        });
        ++sampled;
    }
    return sampled;
}
}  // namespace wisteria
