#pragma once
#include <glad/gl.h>
#include <unordered_map>
#include <string>
#include "vbo.hpp"

class VAO{
public:
    VAO();

    ~VAO();

    void Bind();
    void unBind();

    void BindBuffer(VBO &vbo, std::vector<Layout> &layout);
    void BindBuffer(VBO &vbo, const std::vector<Layout> &layout);
    inline GLuint GetVAO() { return this->vao; };
    inline std::unordered_map<unsigned int, std::string> GetAttribList() { return this->attribList; };
private:
    GLenum ParseType(DataType &type);
    size_t ParseTypeSize(DataType &type);
private:
    GLuint vao = 0;
    std::unordered_map<unsigned int, std::string> attribList;
    unsigned int index = 0;
};