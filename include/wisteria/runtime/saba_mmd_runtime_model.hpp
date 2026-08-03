#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"

#include <filesystem>
#include <memory>
#include <span>

// Saba-backed MMD runtime: uses saba::PMXModel for animation, IK, morph and
// CPU skinning (BDEF/SDEF/QDEF), then uploads skinned vertices into our Mesh.
// Saba physics integration arrives in phase 4 (OwnsSimulationStep).
class SabaMmdRuntimeModel final : public MmdRuntimeModel
{
public:
    SabaMmdRuntimeModel(
        std::filesystem::path modelPath,
        std::filesystem::path vmdPath = {}
    );
    ~SabaMmdRuntimeModel() override;

    SabaMmdRuntimeModel(const SabaMmdRuntimeModel&) = delete;
    SabaMmdRuntimeModel& operator=(const SabaMmdRuntimeModel&) = delete;

    bool Initialize() override;
    void Update(float deltaTime) override;
    void Reset() override;
    Pose& GetPose() override;
    bool NeedsDynamicVertexUpload() const noexcept override;
    void UploadDynamicVertices(Mesh& mesh) override;
    PhysicsInstance* TryGetPhysicsInstance() noexcept override;

    void SetMmdIkEnabled(BoneIndex bone, bool enabled) override;
    void ApplyCameraTrack(
        const CameraTrack& track,
        float time,
        Camera& camera
    ) override;
    MmdSkinningKind SkinningKind() const noexcept override;
    PhysicsInstance* GetMmdPhysics() noexcept override;

    struct VertexDiagnostics
    {
        bool finite = true;
        glm::vec3 minimumPosition{0.0f};
        glm::vec3 maximumPosition{0.0f};
        float maximumDisplacementFromBind = 0.0f;
        std::size_t vertexCount = 0U;
    };
    VertexDiagnostics DiagnoseVertices() const;
    std::span<const glm::vec3> BindPositions() const;
    std::vector<std::uint32_t> Indices() const;
    std::span<const glm::vec3> UpdatePositions() const;

    struct ProfileSnapshot
    {
        double averageUpdateMilliseconds = 0.0;
        double averageUploadMilliseconds = 0.0;
        std::size_t frameCount = 0U;
    };
    ProfileSnapshot Profile() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
