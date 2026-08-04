#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/ebo.hpp"
#include <limits>
#include <stdexcept>

namespace wisteria
{
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

class ScopedCopyWriteBuffer
{
public:
    explicit ScopedCopyWriteBuffer(GLuint buffer)
    {
        glGetIntegerv(
            GL_COPY_WRITE_BUFFER,
            &this->previousBuffer
        );
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    }

    ~ScopedCopyWriteBuffer()
    {
        glBindBuffer(
            GL_COPY_WRITE_BUFFER,
            static_cast<GLuint>(this->previousBuffer)
        );
    }

private:
    GLint previousBuffer = 0;
};
}

EBO::EBO(GraphicsDevice* device)
    : device(device)
{
    glGenBuffers(1, &this->ebo);
}

EBO::~EBO()
{
    if (this->ebo != 0)
    {
        if (this->device != nullptr)
        {
            this->device->DeleteResource(
                GraphicsDevice::ResourceKind::Buffer,
                this->ebo
            );
        }
        else
        {
            glDeleteBuffers(1, &this->ebo);
        }
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

    const ScopedCopyWriteBuffer binding(this->ebo);
    glBufferData(
        GL_COPY_WRITE_BUFFER,
        BufferSize(dataSize),
        data,
        usage
    );
    this->capacity = dataSize;
}

void EBO::Allocate(std::size_t dataSize, GLenum usage)
{
    const ScopedCopyWriteBuffer binding(this->ebo);
    glBufferData(
        GL_COPY_WRITE_BUFFER,
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

    const ScopedCopyWriteBuffer binding(this->ebo);
    glBufferSubData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLintptr>(offset),
        BufferSize(dataSize),
        data
    );
}
}  // namespace wisteria
