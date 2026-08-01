#pragma once
#include "model.hpp"
#include "morph.hpp"
#include "vbo.hpp"
#include "ebo.hpp"
#include <memory>

class VAO;

class Mesh{
public:
    explicit Mesh(
        DefaultModelData data,
        std::size_t requiredBoneCount = 0,
        std::vector<MeshMorphTarget> morphTargets = {}
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
    std::size_t VertexCount() const noexcept;
    bool HasMorphTargets() const noexcept;
    std::size_t MorphTargetCount() const noexcept;
    bool CalculateMorphOffsets(
        std::span<const float> weights,
        std::vector<glm::vec3>& output
    ) const;

private:
    DefaultModelData data;
    std::unique_ptr<VBO> vbo;
    std::unique_ptr<EBO> ebo;
    glm::vec3 localBoundsCenter{0.0f};
    std::vector<MeshMorphTarget> morphTargets;
    std::size_t vertexCount = 0U;
    std::size_t requiredBoneCount = 0;
    bool attached = false;
};
