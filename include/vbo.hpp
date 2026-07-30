#pragma once
#include <glad/gl.h>
#include <vector>
#include <unordered_map>

enum DataType{
    FLOAT, INT, UINT, UCHAR
};

struct Layout{
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
    void Upload(const void* data,unsigned int dataSize, std::vector<Layout> layout);
    inline GLuint GetVBO() { return vbo; };
private:
    GLenum ParseType(DataType type);
    size_t ParseTypeSize(DataType type);
private:
    GLuint vbo = 0;
    unsigned int stride = 0;
};