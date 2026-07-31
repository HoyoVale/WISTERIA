#pragma once
#include <glad/gl.h>
#include <unordered_map>
#include <string>
#include "vbo.hpp"

class VAO{
public:
    VAO();
    ~VAO();

    VAO(const VAO&) = delete;
    VAO& operator=(const VAO&) = delete;

    void Bind();
    void unBind();

    void BindBuffer(VBO& vbo, const std::vector<Layout>& layout);
    inline GLuint GetVAO() const noexcept { return this->vao; };
    const std::unordered_map<unsigned int, std::string>& GetAttribList() const noexcept
    {
        return this->attribList;
    }
private:
    static GLenum ParseType(DataType type);
    static std::size_t ParseTypeSize(DataType type);
private:
    GLuint vao = 0;
    std::unordered_map<unsigned int, std::string> attribList;
    unsigned int index = 0;
};
