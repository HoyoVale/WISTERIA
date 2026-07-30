#pragma once
#include <glad/gl.h>
#include <string>
#include <vector>

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
    void Upload(const void* data,const unsigned int &dataSize);
    void Upload(const void* data,unsigned int &dataSize);
    inline GLuint GetVBO() { return this->vbo; };
private:
    GLuint vbo = 0;
};