#pragma once

#include "wisteria/assets/model_asset.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/runtime/frame_snapshot.hpp"
#include "wisteria/core/root_motion.hpp"

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

    // R1.5: returns the runtime's pending root-motion delta (exactly-once
    // consumed) after publishing the frame view and metadata.
    RootMotionDelta Update(float deltaTime);
    void Reset();
    void UploadDynamicVertices(Mesh& mesh);

    // Latest zero-copy transient view (valid until the next Update).
    const ModelFrameView& LastFrameView() const noexcept;
    // R1.6 Phase 0C: renderer-facing transient view (valid until the next
    // runtime state mutation). Renderer consumes this, never the runtime
    // directly.
    const ModelRenderFrameView& LastRenderFrameView() const noexcept;

    // Captures the requested channels into the WISTERIA-owned persistent
    // snapshot. Only the requested channels are copied; geometry is never
    // copied implicitly. Returns the stable snapshot reference.
    const ModelFrameSnapshot& CaptureSnapshot(
        CaptureMask mask = CaptureMask::All
    );

    const ModelFrameSnapshot& LastSnapshot() const noexcept;

private:
    bool CapturePose();
    bool CaptureMorphs();
    bool CaptureGeometry();
    void ValidateRenderFrameView(const ModelRenderFrameView& view);

    const ModelAsset* asset = nullptr;
    std::unique_ptr<IModelRuntimeDriver> runtime;
    std::vector<std::unique_ptr<Mesh>> instanceMeshes;
    std::unordered_map<const Mesh*, Mesh*> meshMap;
    ModelFrameView lastView;
    ModelRenderFrameView lastRenderView;
    ModelFrameSnapshot snapshot;
    std::uint64_t updateSerial = 0U;
    bool frameValid = false;
};
}  // namespace wisteria
