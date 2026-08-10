#include "wisteria/common/pch.hpp"

#include "mesh_gpu_resource.hpp"

#include <stdexcept>

namespace wisteria
{
namespace
{
GLenum MapIndexFormat(IndexFormat format) noexcept
{
    switch (format)
    {
    case IndexFormat::Uint8:
        return GL_UNSIGNED_BYTE;
    case IndexFormat::Uint16:
        return GL_UNSIGNED_SHORT;
    case IndexFormat::Uint32:
        return GL_UNSIGNED_INT;
    }
    return GL_UNSIGNED_INT;
}
}  // namespace

MeshGpuResource::MeshGpuResource(GraphicsDevice* nextDevice)
    : device(nextDevice)
{
}

void MeshGpuResource::Attach(const DefaultModelData& data)
{
    if (this->attached)
        return;

    auto nextVbo = std::make_unique<VBO>(this->device);
    auto nextEbo = std::make_unique<EBO>(this->device);

    nextVbo->Upload(data.vertices.data(), data.VertexBytes());
    nextEbo->Upload(data.indices.data(), data.IndexBytes());

    this->vbo.swap(nextVbo);
    this->ebo.swap(nextEbo);
    this->attached = true;
}

void MeshGpuResource::ConfigureVertexArray(
    VAO& vertexArray,
    const DefaultModelData& data
)
{
    if (!this->attached)
    {
        throw std::logic_error(
            "Mesh buffers must be attached before configuring a vertex array"
        );
    }

    vertexArray.Bind();
    vertexArray.BindBuffer(*this->vbo, data.layout);
    this->ebo->Bind();
    vertexArray.unBind();
}

void MeshGpuResource::Draw(const DefaultModelData& data)
{
    if (!this->attached)
        throw std::logic_error("Mesh must be attached before drawing");

    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(data.IndexCount()),
        MapIndexFormat(data.IndexFormatValue()),
        nullptr
    );
}

void MeshGpuResource::UploadDynamicFrame(
    const std::vector<float>& vertices
)
{
    if (!this->attached || this->vbo == nullptr)
        return;

    glBindBuffer(GL_ARRAY_BUFFER, this->vbo->GetVBO());
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
        vertices.data()
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool MeshGpuResource::IsAttached() const noexcept
{
    return this->attached;
}
}  // namespace wisteria
