#pragma once
#include "model.hpp"
#include "morph.hpp"
#include "vbo.hpp"
#include "ebo.hpp"
#include "physics_types.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class VAO;
class Pose;

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
    bool CalculateMorphDeltas(
        std::span<const float> weights,
        std::vector<MorphVertexDelta>& output
    ) const;
    std::size_t AppendSkinningDebugLines(
        std::vector<PhysicsDebugLine>& lines,
        const Pose& pose,
        std::span<const std::uint8_t> drivenBoneModes,
        const glm::mat4& modelMatrix,
        const MorphState* morphState,
        std::size_t maximumSamples = 96U
    ) const;

private:
    struct SkinningDebugVertex
    {
        glm::vec3 position{0.0f};
        std::array<BoneIndex, 4U> boneIndices{};
        glm::vec4 boneWeights{0.0f};
    };

    DefaultModelData data;
    std::unique_ptr<VBO> vbo;
    std::unique_ptr<EBO> ebo;
    glm::vec3 localBoundsCenter{0.0f};
    std::vector<MeshMorphTarget> morphTargets;
    std::vector<SkinningDebugVertex> skinningDebugVertices;
    std::size_t vertexCount = 0U;
    std::size_t requiredBoneCount = 0;
    bool attached = false;
};
