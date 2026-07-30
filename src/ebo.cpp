#include "pch.hpp"
#include "ebo.hpp"

EBO::EBO()
{
    glGenBuffers(1, &this->ebo);
}

EBO::~EBO()
{
    if(!this->ebo == 0){
        this->unBind();
        glDeleteBuffers(1, &this->ebo);
    }
}

void EBO::Bind()
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->ebo);
}

void EBO::unBind()
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void EBO::Upload(const void* data,unsigned int &dataSize)
{
    this->Bind();
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        dataSize,
        data,
        GL_STATIC_DRAW
    );
}

void EBO::Upload(const void* data, const unsigned int &dataSize)
{
    this->Bind();
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        dataSize,
        data,
        GL_STATIC_DRAW
    );
}