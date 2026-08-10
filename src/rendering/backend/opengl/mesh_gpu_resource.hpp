#pragma once

#include "wisteria/rendering/ebo.hpp"
#include "wisteria/rendering/model.hpp"
#include "wisteria/rendering/vao.hpp"
#include "wisteria/rendering/vbo.hpp"

#include <memory>
#include <vector>

namespace wisteria
{
// R2.0 Phase 0C: per-device GPU realization of a Mesh CPU asset.
//
// The GPU half of a mesh (VBO/EBO + attach/draw/upload GL operations) lives
// here, outside the CPU semantic asset. One ModelAsset mesh may eventually
// share one static realization per device (RenderResourceCache); runtime-
// deformed instances must each own their own realization (ModelInstance
// identity), so this class is intentionally instance-owned for now.
class MeshGpuResource
{
public:
    explicit MeshGpuResource(GraphicsDevice* device);

    void Attach(const DefaultModelData& data);
    void ConfigureVertexArray(
        VAO& vertexArray,
        const DefaultModelData& data
    );
    void Draw(const DefaultModelData& data);
    // Uploads an already-rebuilt interleaved vertex array (CPU rebuild stays
    // in the asset layer; only the GPU write belongs to the realization).
    void UploadDynamicFrame(const std::vector<float>& vertices);
    bool IsAttached() const noexcept;

private:
    GraphicsDevice* device = nullptr;
    std::unique_ptr<VBO> vbo;
    std::unique_ptr<EBO> ebo;
    bool attached = false;
};
}  // namespace wisteria
