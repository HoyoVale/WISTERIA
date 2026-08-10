#pragma once
#include <glad/gl.h>
#include "wisteria/rendering/graphics_device.hpp"
#include "wisteria/rendering/vertex_layout.hpp"
#include <string>
#include <vector>
#include <cstddef>

namespace wisteria
{
class VBO{
public:
    explicit VBO(GraphicsDevice* device = nullptr);
    ~VBO();

    VBO(const VBO&) = delete;
    VBO& operator=(const VBO&) = delete;

    void Bind();
    void unBind();
    void Upload(const void* data, std::size_t dataSize);
    inline GLuint GetVBO() const noexcept { return this->vbo; };
private:
    GraphicsDevice* device = nullptr;
    GLuint vbo = 0;
};
}  // namespace wisteria
