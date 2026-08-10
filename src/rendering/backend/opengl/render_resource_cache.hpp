#pragma once

#include "wisteria/rendering/model.hpp"
#include "wisteria/rendering/texture.hpp"

#include "mesh_gpu_resource.hpp"
#include "texture_gpu_resource.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace wisteria
{
// R2.0 Phase 0C Step 6: per-RenderDevice cache of shared GPU realizations.
//
// Static/immutable assets may share one realization per device. Runtime-
// deformed geometry must NEVER be merged here: instance clones keep their
// own realization (Mesh::CloneForInstance does not consult this cache), so
// ModelInstances referencing the same ModelAsset never share dynamic state.
class RenderResourceCache
{
public:
    explicit RenderResourceCache(GraphicsDevice* device);

    // Shared realization for a texture asset (identity = file path or
    // encoded/RGBA8 payload hash). Callers keep the shared_ptr for as long
    // as the realization must stay alive.
    std::shared_ptr<TextureGpuResource> AcquireTexture(
        const TextureData& data
    );

    // Shared realization for static mesh data (identity = FNV-1a of
    // vertices/indices/layout). Dynamic/deformed meshes must not use this.
    std::shared_ptr<MeshGpuResource> AcquireStaticMesh(
        const DefaultModelData& data
    );

    // Instance-local realization for runtime-deformed geometry. Never
    // cached; each call returns a distinct realization so ModelInstances
    // referencing the same asset never share dynamic state.
    std::shared_ptr<MeshGpuResource> CreateInstanceMesh(
        const DefaultModelData& data
    );
    std::shared_ptr<TextureGpuResource> CreateInstanceTexture(
        const TextureData& data
    );

    GraphicsDevice* Device() const noexcept;

    void Clear() noexcept;
    std::size_t TextureCount() const noexcept;
    std::size_t StaticMeshCount() const noexcept;

private:
    static std::string TextureKey(const TextureData& data);
    static std::uint64_t DataHash(const DefaultModelData& data);

    GraphicsDevice* device = nullptr;
    std::unordered_map<
        std::string,
        std::shared_ptr<TextureGpuResource>
    > textures;
    std::unordered_map<
        std::uint64_t,
        std::shared_ptr<MeshGpuResource>
    > staticMeshes;
};
}  // namespace wisteria
