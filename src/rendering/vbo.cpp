#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/vbo.hpp"
#include <cstdint>

VBO::VBO()
{
    glGenBuffers(1, &this->vbo);
}

VBO::~VBO()
{
    if (this->vbo != 0)
    {
        glDeleteBuffers(1, &this->vbo);
        this->vbo = 0;
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

void VBO::Upload(const void* data, std::size_t dataSize)
{
    this->Bind();
    glBufferData(
        GL_ARRAY_BUFFER,
        dataSize,
        data,
        GL_STATIC_DRAW
    );
}


