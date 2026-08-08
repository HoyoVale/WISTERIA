#pragma once
#include "wisteria/rendering/model.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/rendering/vbo.hpp"
#include "wisteria/rendering/ebo.hpp"
#include "wisteria/physics/physics_types.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace wisteria
{
class VAO;
class Pose;
class Mesh;

// Called by the Renderer with the owning window's GL context current. Used by
// CPU-skinned models (Saba) to upload vertices at the correct time/context.
using MeshDynamicVertexProvider = std::function<void(Mesh&)>;

class Mesh{
public:
    explicit Mesh(
        DefaultModelData data,
        std::size_t requiredBoneCount = 0,
        std::vector<MeshMorphTarget> morphTargets = {},
        std::vector<std::uint32_t> sourceVertexIndices = {},
        GraphicsDevice* device = nullptr
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

    std::unique_ptr<Mesh> CloneForInstance() const;

    // CPU-skinning bridge (Saba BDEF/SDEF/QDEF): uploads skinned
    // positions/normals every frame without rebuilding the whole VBO.
    void UploadDynamicVertices(
        std::span<const glm::vec3> positions,
        std::span<const glm::vec3> normals
    );
    // R1.6 Phase 0C combined dynamic frame upload: positions + normals +
    // optional dynamic UVs are rebuilt into the interleaved vertex buffer in
    // ONE pass. An empty uvs span keeps the static texCoord attribute.
    void UploadDynamicFrame(
        std::span<const glm::vec3> positions,
        std::span<const glm::vec3> normals,
        std::span<const glm::vec2> uvs = {}
    );
    bool HasDynamicVertexSource() const noexcept;
    void SetDynamicVertexProvider(MeshDynamicVertexProvider provider);
    const MeshDynamicVertexProvider& DynamicVertexProvider() const noexcept;
    std::span<const std::uint32_t> SourceVertexIndices() const noexcept;
    // Shared lifetime marker. Renderer VAO caches hold a weak reference and
    // rebuild the VAO once a mesh is destroyed, instead of dangling on the
    // raw Mesh pointer after the ResourceManager releases it.
    std::shared_ptr<const void> LifetimeToken() const noexcept;

    // Pure rebuild of an interleaved vertex array with new position/normal
    // values. Exposed for regression tests; UploadDynamicVertices uses it.
    static std::vector<float> RebuildInterleavedVertices(
        const std::vector<float>& sourceVertices,
        std::span<const Layout> layout,
        std::span<const glm::vec3> positions,
        std::span<const glm::vec3> normals,
        std::size_t vertexCount,
        std::span<const glm::vec2> uvs = {}
    );

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

    GraphicsDevice* device = nullptr;
    DefaultModelData data;
    std::unique_ptr<VBO> vbo;
    std::unique_ptr<EBO> ebo;
    glm::vec3 localBoundsCenter{0.0f};
    std::vector<MeshMorphTarget> morphTargets;
    std::vector<SkinningDebugVertex> skinningDebugVertices;
    std::size_t vertexCount = 0U;
    std::size_t requiredBoneCount = 0;
    bool attached = false;
    bool dynamicVertexSource = false;
    MeshDynamicVertexProvider dynamicVertexProvider;
    std::shared_ptr<const void> lifetimeToken =
        std::make_shared<int>(0);
    std::vector<std::uint32_t> sourceVertexIndices;
};
}  // namespace wisteria
