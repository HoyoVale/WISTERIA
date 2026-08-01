#pragma once
#include "model.hpp"
#include "vbo.hpp"
#include "ebo.hpp"
#include <memory>

class VAO;

class Mesh{
public:
    explicit Mesh(
        DefaultModelData data,
        std::size_t requiredBoneCount = 0
    );
    ~Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = delete;
    Mesh& operator=(Mesh&&) = delete;

    void Attach();
    void ConfigureVertexArray(VAO& vao);
    void Draw();

    bool IsAttached() const noexcept;
    std::size_t IndexCount() const noexcept;
    const glm::vec3& LocalBoundsCenter() const noexcept;
    bool IsSkinned() const noexcept;
    std::size_t RequiredBoneCount() const noexcept;

private:
    DefaultModelData data;
    std::unique_ptr<VBO> vbo;
    std::unique_ptr<EBO> ebo;
    glm::vec3 localBoundsCenter{0.0f};
    std::size_t requiredBoneCount = 0;
    bool attached = false;
};
