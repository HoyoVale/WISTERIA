#include "pch.hpp"
#include "vbo.hpp"
#include <cstdint>

VBO::VBO()
{
    glGenBuffers(1, &this->vbo);
}

VBO::~VBO()
{
    if (vbo != 0) {
        unBind();
        glDeleteBuffers(1, &this->vbo);
    }
}

void VBO::Bind()
{
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo);
}

void VBO::unBind()
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VBO::Upload(const void* data,unsigned int &dataSize)
{
    this->Bind();
    glBufferData(
        GL_ARRAY_BUFFER,
        dataSize,
        data,
        GL_STATIC_DRAW
    );
}

void VBO::Upload(const void* data,const unsigned int &dataSize)
{
    this->Bind();
    glBufferData(
        GL_ARRAY_BUFFER,
        dataSize,
        data,
        GL_STATIC_DRAW
    );
}


