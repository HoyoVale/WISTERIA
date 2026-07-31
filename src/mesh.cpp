#include "pch.hpp"
#include "mesh.hpp"

Mesh::Mesh(const DefaultModelData& data)
    :data(&data)
{
}

void Mesh::Attach()
{
    if (this->data == nullptr)
        throw std::logic_error("Mesh has no model data");

    if (this->attached)
        return;

    this->vao = std::make_unique<VAO>();
    this->vao->Bind();
    this->vbo = std::make_unique<VBO>();
    this->vbo->Upload(
        this->data->vertices.data(),
        this->data->VertexBytes()
    );
    this->vao->BindBuffer(*this->vbo, this->data->layout);
    this->ebo = std::make_unique<EBO>();
    this->ebo->Bind();
    this->ebo->Upload(
        this->data->indices.data(),
        this->data->IndexBytes()
    );
    this->vao->unBind();
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
    this->Bind();

    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(this->data->IndexCount()),
        GL_UNSIGNED_INT,
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
    return this->data != nullptr ? this->data->IndexCount() : 0;
}
