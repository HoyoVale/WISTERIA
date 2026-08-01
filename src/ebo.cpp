#include "pch.hpp"
#include "ebo.hpp"
#include <limits>
#include <stdexcept>

namespace
{
GLsizeiptr BufferSize(std::size_t size)
{
    if (size > static_cast<std::size_t>(
            std::numeric_limits<GLsizeiptr>::max()))
    {
        throw std::overflow_error("EBO size exceeds OpenGL buffer limits");
    }
    return static_cast<GLsizeiptr>(size);
}
}

EBO::EBO()
{
    glGenBuffers(1, &this->ebo);
}

EBO::~EBO()
{
    if (this->ebo != 0)
    {
        glDeleteBuffers(1, &this->ebo);
        this->ebo = 0;
        this->capacity = 0;
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

void EBO::Upload(
    const void* data,
    std::size_t dataSize,
    GLenum usage
)
{
    if (dataSize > 0 && data == nullptr)
        throw std::invalid_argument("EBO upload data must not be null");

    this->Bind();
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        BufferSize(dataSize),
        data,
        usage
    );
    this->capacity = dataSize;
}

void EBO::Allocate(std::size_t dataSize, GLenum usage)
{
    this->Bind();
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        BufferSize(dataSize),
        nullptr,
        usage
    );
    this->capacity = dataSize;
}

void EBO::Update(
    const void* data,
    std::size_t dataSize,
    std::size_t offset
)
{
    if (dataSize == 0)
        return;
    if (data == nullptr)
        throw std::invalid_argument("EBO update data must not be null");
    if (offset > this->capacity || dataSize > this->capacity - offset)
        throw std::out_of_range("EBO update exceeds allocated capacity");

    this->Bind();
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLintptr>(offset),
        BufferSize(dataSize),
        data
    );
}
