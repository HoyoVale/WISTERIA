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
    void Upload(const void* data, std::size_t dataSize);
    inline GLuint GetEBO() const noexcept { return this->ebo; };
private:
    GLuint ebo = 0;
};
