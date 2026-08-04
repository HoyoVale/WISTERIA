#pragma once
#include <glad/gl.h>
#include "wisteria/rendering/graphics_device.hpp"
#include <unordered_map>
#include <string>
#include "wisteria/rendering/vbo.hpp"

namespace wisteria
{
class VAO{
public:
    explicit VAO(GraphicsDevice* device = nullptr);
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
    GraphicsDevice* device = nullptr;
    GLuint vao = 0;
    std::unordered_map<unsigned int, std::string> attribList;
    unsigned int index = 0;
};
}  // namespace wisteria
