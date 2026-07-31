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
    // Set true when the shader input is ivec*/uvec* rather than vec*.
    bool integer = false;
};

class VBO{
public:
    VBO();
    ~VBO();

    VBO(const VBO&) = delete;
    VBO& operator=(const VBO&) = delete;

    void Bind();
    void unBind();
    void Upload(const void* data, std::size_t dataSize);
    inline GLuint GetVBO() const noexcept { return this->vbo; };
private:
    GLuint vbo = 0;
};
