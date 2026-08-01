#include "pch.hpp"
#include "vao.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

VAO::VAO()
{
    glGenVertexArrays(1, &this->vao);
}

VAO::~VAO()
{
    if (this->vao != 0)
    {
        glDeleteVertexArrays(1, &this->vao);
        this->vao = 0;
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

void VAO::BindBuffer(VBO& vbo, const std::vector<Layout>& layout)
{
    if (layout.empty())
        throw std::invalid_argument("VAO layout must contain at least one attribute");

    GLint maxAttributes = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttributes);
    if (maxAttributes <= 0)
        throw std::runtime_error("OpenGL reported no available vertex attributes");

    std::size_t strideBytes = 0;
    for (const Layout& attribute : layout)
    {
        if (attribute.size == 0 || attribute.size > 4)
            throw std::invalid_argument("Vertex attribute size must be between 1 and 4");

        if (attribute.integer && attribute.type == FLOAT)
            throw std::invalid_argument("Integer shader attributes cannot use FLOAT storage");

        if (attribute.integer && attribute.normalized)
            throw std::invalid_argument("Integer shader attributes cannot be normalized");

        strideBytes += attribute.size * ParseTypeSize(attribute.type);
    }

    if (strideBytes > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
        throw std::overflow_error("VAO vertex stride exceeds GLsizei range");

    this->Bind();
    vbo.Bind();

    GLuint nextAutomaticIndex = this->index;
    std::size_t offsetBytes = 0;
    const GLsizei stride = static_cast<GLsizei>(strideBytes);
    for (const Layout& attribute : layout)
    {
        GLuint index = attribute.location;
        if (index == AutomaticAttributeLocation)
        {
            while (this->attribList.contains(nextAutomaticIndex))
                ++nextAutomaticIndex;
            index = nextAutomaticIndex;
        }
        if (index >= static_cast<GLuint>(maxAttributes))
            throw std::out_of_range("VAO attribute location exceeds GL_MAX_VERTEX_ATTRIBS");
        if (this->attribList.contains(index))
            throw std::invalid_argument("VAO attribute location is already in use");

        glEnableVertexAttribArray(index);
        const void* offset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(offsetBytes)
        );

        if (attribute.integer)
        {
            glVertexAttribIPointer(
                index,
                static_cast<GLint>(attribute.size),
                ParseType(attribute.type),
                stride,
                offset
            );
        }
        else
        {
            glVertexAttribPointer(
                index,
                static_cast<GLint>(attribute.size),
                ParseType(attribute.type),
                attribute.normalized ? GL_TRUE : GL_FALSE,
                stride,
                offset
            );
        }

        this->attribList[index] = attribute.name;
        nextAutomaticIndex = std::max(nextAutomaticIndex, index + 1U);
        offsetBytes += attribute.size * ParseTypeSize(attribute.type);
    }
    this->index = nextAutomaticIndex;
}

GLenum VAO::ParseType(DataType type)
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

    throw std::invalid_argument("Unsupported vertex attribute data type");
}

std::size_t VAO::ParseTypeSize(DataType type)
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
    throw std::invalid_argument("Unsupported vertex attribute data type");
}
