#pragma once
#include "model.hpp"
#include "vao.hpp"
#include "vbo.hpp"
#include "ebo.hpp"
#include <memory>

class Mesh{
public:
    explicit Mesh(const DefaultModelData& data);
    ~Mesh() = default;

    void Attach();
    void Bind();
    void Draw();
    void Unbind();

    bool IsAttached() const noexcept;
    std::size_t IndexCount() const noexcept;

private:
    const DefaultModelData* data = nullptr;
    std::unique_ptr<VAO> vao;
    std::unique_ptr<VBO> vbo;
    std::unique_ptr<EBO> ebo;
    bool attached = false;
};
