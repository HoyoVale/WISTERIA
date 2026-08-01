#pragma once
#include "model.hpp"
#include "vao.hpp"
#include "vbo.hpp"
#include "ebo.hpp"
#include <memory>

class Mesh{
public:
    explicit Mesh(DefaultModelData data);
    ~Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = delete;
    Mesh& operator=(Mesh&&) = delete;

    void Attach();
    void Bind();
    void Draw();
    void Unbind();

    bool IsAttached() const noexcept;
    std::size_t IndexCount() const noexcept;
    const glm::vec3& LocalBoundsCenter() const noexcept;

private:
    DefaultModelData data;
    std::unique_ptr<VAO> vao;
    std::unique_ptr<VBO> vbo;
    std::unique_ptr<EBO> ebo;
    glm::vec3 localBoundsCenter{0.0f};
    bool attached = false;
};
