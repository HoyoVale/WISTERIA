#include "pch.hpp"
#include "mesh.hpp"

Mesh::Mesh(const Model &_model)
    :model(&_model)
{
}

Mesh::~Mesh()
{
    // model 由外部管理，Mesh 只保存非拥有指针。
    delete this->vao;
    delete this->vbo;
    delete this->ebo;
}

void Mesh::Attach()
{
    this->vao = new VAO();
    this->vao->Bind();
    this->vbo = new VBO();
    this->vbo->Upload(
        this->model->data->vertices.data(),
        static_cast<unsigned int>(this->model->data->VertexBytes())
    );
    this->vao->BindBuffer(*this->vbo, this->model->data->layout);
    this->ebo = new EBO();
    this->ebo->Bind();
    this->ebo->Upload(
        this->model->data->indices.data(),
        static_cast<unsigned int>(this->model->data->IndexBytes())
    );
    this->vao->unBind();
}

void Mesh::Draw()
{
    this->vao->Bind();
    //glDrawArrays(GL_TRIANGLES, 0, 3);
    //glDepthMask(GL_FALSE);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(this->model->data->IndexCount()),
        GL_UNSIGNED_INT,
        nullptr
    );
    //glDepthMask(GL_TRUE);
}
