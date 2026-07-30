#pragma once
#include <glad/gl.h>

class EBO{
public:
    EBO();
    ~EBO();

    void Bind();
    void unBind();
    void Upload(const void* data, unsigned int &dataSize);
    void Upload(const void* data, const unsigned int &dataSize);
    inline GLuint GetEBO() { return this->ebo; };
private:
    GLuint ebo = 0;
};