#pragma once

namespace wisteria
{
class Pose;
class Mesh;
class PhysicsInstance;

// Format-agnostic runtime abstraction. Every model format (MMD, glTF, ...)
// implements this interface so Entity/Scene never depend on format details.
class RuntimeModelBase
{
public:
    virtual ~RuntimeModelBase() = default;

    // Builds runtime state (clips, physics, skinning) from imported assets.
    virtual bool Initialize() = 0;

    // Advances one frame: animation sampling + IK/append + physics + skinning.
    virtual void Update(float deltaTime) = 0;

    virtual void Reset() = 0;

    // Current bone pose consumed by the renderer for skinning matrices.
    virtual Pose& GetPose() = 0;

    // True when skinning happens outside the GPU palette (e.g. Saba CPU
    // BDEF/SDEF/QDEF) and UploadDynamicVertices must run every frame.
    virtual bool NeedsDynamicVertexUpload() const noexcept = 0;

    // Writes skinned positions/normals into the mesh.
    virtual void UploadDynamicVertices(Mesh& mesh) = 0;

    // Optional physics adapter. Scene drives it through PhysicsInstance.
    virtual PhysicsInstance* TryGetPhysicsInstance() noexcept = 0;
};
}  // namespace wisteria
