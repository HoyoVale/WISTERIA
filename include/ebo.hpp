#pragma once
#include <glad/gl.h>
#include <cstddef>

class EBO{
public:
    EBO();
    ~EBO();

    EBO(const EBO&) = delete;
    EBO& operator=(const EBO&) = delete;

    void Bind();
    void unBind();
    void Upload(
        const void* data,
        std::size_t dataSize,
        GLenum usage = GL_STATIC_DRAW
    );
    void Allocate(
        std::size_t dataSize,
        GLenum usage = GL_STREAM_DRAW
    );
    void Update(
        const void* data,
        std::size_t dataSize,
        std::size_t offset = 0
    );
    std::size_t Capacity() const noexcept { return this->capacity; }
    inline GLuint GetEBO() const noexcept { return this->ebo; };
private:
    GLuint ebo = 0;
    std::size_t capacity = 0;
};
