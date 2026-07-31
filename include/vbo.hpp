#pragma once
#include <glad/gl.h>
#include <string>
#include <vector>
#include <cstddef>

enum DataType{
    FLOAT, INT, UINT, UCHAR
};

struct Layout{
    std::string name;
    unsigned int size;
    DataType type;
    bool normalized = false;
};

class VBO{
public:
    VBO();
    ~VBO();

    void Bind();
    void unBind();
    void Upload(const void* data, std::size_t dataSize);
    inline GLuint GetVBO() { return this->vbo; };
private:
    GLuint vbo = 0;
};
