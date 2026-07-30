#include "pch.hpp"
#include "vao.hpp"

VAO::VAO()
{
    glGenVertexArrays(1, &this->vao);
}

VAO::~VAO()
{
    if(!vao == 0){
        unBind();
        glDeleteVertexArrays(1, &this->vao);
    }
        
}

void VAO::Bind()
{
    glBindVertexArray(this->vao);
}

void VAO::unBind()
{
    glBindVertexArray(0);
}

void VAO::BindBuffer(VBO &vbo, std::vector<Layout> &layout)
{
    Bind();
    vbo.Bind();
    unsigned int index = this->index, offset = 0, stride = 0;
    for (Layout l : layout) 
    {
        stride += l.size * ParseTypeSize(l.type); // compute stride
    }
    for (Layout l : layout)
    {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(
            index,
            l.size,
            ParseType(l.type),
            l.normalized,
            stride,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset))
        );
        this->attribList[index] = l.name; // unoerdered list store the attrib list
        index ++;
        offset += l.size * ParseTypeSize(l.type);
    }
    this->index = index;
}

void VAO::BindBuffer(VBO &vbo, const std::vector<Layout> &layout)
{
    Bind();
    vbo.Bind();
    unsigned int index = this->index, offset = 0, stride = 0;
    for (Layout l : layout) 
    {
        stride += l.size * ParseTypeSize(l.type); // compute stride
    }
    for (Layout l : layout)
    {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(
            index,
            l.size,
            ParseType(l.type),
            l.normalized,
            stride,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset))
        );
        this->attribList[index] = l.name; // unoerdered list store the attrib list
        index ++;
        offset += l.size * ParseTypeSize(l.type);
    }
    this->index = index;
}

GLenum VAO::ParseType(DataType &type)
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

size_t VAO::ParseTypeSize(DataType &type)
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
