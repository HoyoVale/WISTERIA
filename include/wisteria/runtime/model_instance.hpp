#pragma once

#include "wisteria/assets/model_asset.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace wisteria
{
class Mesh;
class MmdRuntimeModel;

// Per-scene-instance owner of mutable model runtime and render data. Shared
// ModelAsset resources remain immutable; dynamic meshes belong to this object.
class ModelInstance
{
public:
    ModelInstance(
        const ModelAsset& asset,
        std::unique_ptr<IModelRuntimeDriver> runtime
    );
    ~ModelInstance() = default;

    ModelInstance(const ModelInstance&) = delete;
    ModelInstance& operator=(const ModelInstance&) = delete;
    ModelInstance(ModelInstance&&) = delete;
    ModelInstance& operator=(ModelInstance&&) = delete;

    const ModelAsset& Asset() const noexcept;
    bool HasRuntime() const noexcept;
    IModelRuntimeDriver* TryGetRuntime() noexcept;
    const IModelRuntimeDriver* TryGetRuntime() const noexcept;
    MmdRuntimeModel* TryGetMmdRuntime() noexcept;
    const MmdRuntimeModel* TryGetMmdRuntime() const noexcept;

    Mesh& ResolveMesh(const Mesh& assetMesh);
    const Mesh& ResolveMesh(const Mesh& assetMesh) const;
    std::size_t InstanceMeshCount() const noexcept;

    void Update(float deltaTime);
    void Reset();
    void UploadDynamicVertices(Mesh& mesh);

private:
    const ModelAsset* asset = nullptr;
    std::unique_ptr<IModelRuntimeDriver> runtime;
    std::vector<std::unique_ptr<Mesh>> instanceMeshes;
    std::unordered_map<const Mesh*, Mesh*> meshMap;
};
}  // namespace wisteria
