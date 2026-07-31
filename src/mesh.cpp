#include "pch.hpp"
#include "mesh.hpp"
#include <utility>

Mesh::Mesh(DefaultModelData data)
    : data(std::move(data))
{
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
