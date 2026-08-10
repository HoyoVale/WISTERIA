#pragma once

#include "wisteria/rendering/model.hpp"
#include "wisteria/rendering/texture.hpp"

#include "mesh_gpu_resource.hpp"
#include "environment_gpu_resource.hpp"
#include "texture_gpu_resource.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Shared per-device environment realization. Identity = prepared source
    // payload + GPU generation parameters (resolutions/mip count/BRDF size).
    // Provenance path, intensity and drawSkybox are NOT part of identity.
    std::shared_ptr<EnvironmentMapGpuResource> AcquireEnvironment(
        const EnvironmentMapData& data
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
    std::shared_ptr<EnvironmentMapGpuResource> CreateInstanceEnvironment(
        const EnvironmentMapData& data
    );

    GraphicsDevice* Device() const noexcept;

    void Clear() noexcept;
    std::size_t TextureCount() const noexcept;
    std::size_t StaticMeshCount() const noexcept;
    std::size_t EnvironmentCount() const noexcept;

private:
    static std::string TextureKey(const TextureData& data);
    static std::uint64_t DataHash(const DefaultModelData& data);
    static std::string EnvironmentKey(const EnvironmentMapData& data);
    static bool MeshDataEqual(
        const DefaultModelData& left,
        const DefaultModelData& right
    );
    static bool TextureDataEqual(
        const TextureData& left,
        const TextureData& right
    );

    struct StaticMeshEntry
    {
        std::uint64_t hash = 0U;
        DefaultModelData data;
        std::shared_ptr<MeshGpuResource> realization;
    };
    struct TextureEntry
    {
        std::string key;
        TextureData data;
        std::shared_ptr<TextureGpuResource> realization;
    };

    GraphicsDevice* device = nullptr;
    // 6B: hash/key strings are lookup accelerators, NOT final equality
    // authority; entries keep exact data and are compared precisely on
    // collision. Small caches -> linear search over the pre-filtered set.
    std::vector<StaticMeshEntry> staticMeshes;
    std::vector<TextureEntry> textures;
    std::unordered_map<
        std::string,
        std::shared_ptr<EnvironmentMapGpuResource>
    > environments;
};
}  // namespace wisteria
