#include "pch.hpp"
#include "vbo.hpp"
#include <cstdint>

VBO::VBO()
{
    glGenBuffers(1, &this->vbo);
}

VBO::~VBO()
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
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

void VBO::Upload(const void* data,unsigned int dataSize, std::vector<Layout> layout)
{
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo);
    unsigned int index = 0, offset = 0;
    for (Layout l : layout) 
    {
        this->stride += l.size * ParseTypeSize(l.type);
    }
    for (Layout l : layout)
    {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(
            index,
            l.size,
            ParseType(l.type),
            l.normalized,
            this->stride,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset))
        );
        index ++;
        offset += l.size * ParseTypeSize(l.type);
    }
    glBufferData(
        GL_ARRAY_BUFFER,
        dataSize,
        data,
        GL_STATIC_DRAW
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    this->stride = 0;
}

GLenum VBO::ParseType(DataType type)
{
    switch (type) {
    case FLOAT:
        return GL_FLOAT;

    case INT:
        return GL_INT;

    case UINT:
        return GL_UNSIGNED_INT;

    case UCHAR:
        return GL_UNSIGNED_BYTE;
    }

    return 0;
}

size_t VBO::ParseTypeSize(DataType type)
{
    switch (type) {
    case FLOAT:
        return sizeof(float);

    case INT:
        return sizeof(int);

    case UINT:
        return sizeof(unsigned int);

    case UCHAR:
        return sizeof(unsigned char);
    }
    return 0;
}



