#include "pch.hpp"
#include "mesh.hpp"
#include <cstring>
#include <limits>
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

}

Mesh::Mesh(DefaultModelData data)
    : data(std::move(data))
{
    this->localBoundsCenter = CalculateBoundsCenter(this->data);
}

void Mesh::Attach()
{
    if (this->attached)
        return;

    auto nextVao = std::make_unique<VAO>();
    auto nextVbo = std::make_unique<VBO>();
    auto nextEbo = std::make_unique<EBO>();

    nextVao->Bind();
    nextVbo->Upload(
        this->data.vertices.data(),
        this->data.VertexBytes()
    );
    nextVao->BindBuffer(*nextVbo, this->data.layout);
    nextEbo->Upload(
        this->data.indices.data(),
        this->data.IndexBytes()
    );
    nextVao->unBind();

    this->vao.swap(nextVao);
    this->vbo.swap(nextVbo);
    this->ebo.swap(nextEbo);
    this->attached = true;
}

void Mesh::Bind()
{
    if (!this->attached)
        throw std::logic_error("Mesh must be attached before binding");

    this->vao->Bind();
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

void Mesh::Unbind()
{
    if (this->attached)
        this->vao->unBind();
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
