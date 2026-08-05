#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"

#include <filesystem>
#include <memory>
#include <span>

namespace wisteria
{
using SabaPhysicsSettings = MmdPhysicsRuntimeSettings;

// Saba-backed MMD runtime: uses saba::PMXModel for animation, IK, morph and
// CPU skinning (BDEF/SDEF/QDEF), then uploads skinned vertices into our Mesh.
// Saba owns its per-model Bullet world (OwnsSimulationStep); the Scene skips
// the shared fixed-step lifecycle for this runtime.
class SabaMmdRuntimeModel final : public MmdRuntimeModel
{
public:
    SabaMmdRuntimeModel(
        std::filesystem::path modelPath,
        std::filesystem::path vmdPath = {},
        SabaPhysicsSettings physicsSettings = {}
    );
    ~SabaMmdRuntimeModel() override;

    SabaMmdRuntimeModel(const SabaMmdRuntimeModel&) = delete;
    SabaMmdRuntimeModel& operator=(const SabaMmdRuntimeModel&) = delete;

    // Overrides physics settings. Calling before Initialize() applies them at
    // startup; calling after Initialize() reapplies them to the live world.
    void SetPhysicsSettings(const SabaPhysicsSettings& settings);
    void SetMmdPhysicsSettings(
        const MmdPhysicsRuntimeSettings& settings
    ) override;
    void ResetMmdPhysics() override;

    bool Initialize() override;
    void Update(float deltaTime) override;
    void Reset() override;
    Pose& GetPose() override;
    const Pose& GetPose() const override;
    bool NeedsDynamicVertexUpload() const noexcept override;
    ModelVertexFrame VertexFrame() const noexcept override;
    PhysicsInstance* TryGetPhysicsInstance() noexcept override;
    const PhysicsInstance* TryGetPhysicsInstance() const noexcept override;
    std::string_view BackendName() const noexcept override;
    bool SetMorphWeight(std::string_view name, float weight) override;
    std::optional<float> MorphWeight(
        std::string_view name
    ) const override;

    void SetMmdIkEnabled(BoneIndex bone, bool enabled) override;
    BoneIndex FindBoneIndex(const std::string& name) const override;

    bool LoadMotion(const std::filesystem::path& vmdPath) override;
    void ClearMotion() override;
    bool HasMotion() const noexcept override;
    void SetMotionLooping(bool looping) override;
    bool IsMotionLooping() const noexcept override;
    void PauseMotion() override;
    void ResumeMotion() override;
    bool IsMotionPaused() const noexcept override;
    void RestartMotion(bool resetPhysics = true) override;
    double MotionFrame() const noexcept override;
    void SetMotionFrame(double frame) override;
    double MotionMaxFrame() const noexcept override;

    bool LoadCameraMotion(const std::filesystem::path& vmdPath) override;
    void ApplyCameraMotion(float frame, Camera& camera) override;
    void ApplyCameraTrack(
        const CameraTrack& track,
        float time,
        Camera& camera
    ) override;
    bool LoadLightMotion(const std::filesystem::path& vmdPath) override;
    void ApplyLightMotion(float frame, DirectionalLight& light) override;
    void ApplyLightTrack(
        const LightTrack& track,
        float time,
        DirectionalLight& light
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
    // Applies saba's per-body activation to match enabled (called at
    // Initialize and on SetPhysicsSettings).
    void ApplyPhysicsActivation();
    void ApplyMmdIkOverrides() noexcept;
    void SyncPoseFromSaba();

    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace wisteria
