#pragma once
#include <glad/gl.h>
#include <cstddef>

class EBO{
public:
    EBO();
    ~EBO();

    void Bind();
    void unBind();
    void Upload(const void* data, std::size_t dataSize);
    inline GLuint GetEBO() { return this->ebo; };
private:
    GLuint ebo = 0;
};
