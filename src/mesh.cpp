#include "pch.hpp"
#include "mesh.hpp"
#include "vao.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

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
    std::vector<MeshMorphTarget> morphTargets
)
    : data(std::move(data)),
      morphTargets(std::move(morphTargets)),
      requiredBoneCount(requiredBoneCount)
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

    std::unordered_set<MorphIndex> targetIndices;
    for (const MeshMorphTarget& target : this->morphTargets)
    {
        if (target.morphIndex == InvalidMorphIndex || target.offsets.empty() ||
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
    }
}

void Mesh::Attach()
{
    if (this->attached)
        return;

    auto nextVbo = std::make_unique<VBO>();
    auto nextEbo = std::make_unique<EBO>();

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
        if (weight == 0.0f)
            continue;
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
