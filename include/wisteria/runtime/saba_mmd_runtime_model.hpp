#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"

#include <filesystem>
#include <memory>
#include <span>

namespace wisteria
{
class ModelAsset;

using SabaPhysicsSettings = MmdPhysicsRuntimeSettings;

// Saba-backed MMD runtime: uses saba::PMXModel for animation, IK, morph and
// CPU skinning (BDEF/SDEF/QDEF), producing deformed vertex data consumed by
// WISTERIA. Saba owns its per-model Bullet world (OwnsSimulationStep); the
// Scene skips the shared fixed-step lifecycle for this runtime.
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

    // Associates the WISTERIA ModelAsset so morph descriptors can resolve
    // PMX morph kinds (Saba's MMDMorph does not carry kind information).
    // Must be called before Initialize().
    void SetAsset(const ModelAsset* asset) noexcept;

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
    std::optional<CameraTrackSample>
        SampleCameraMotion(float frame) const override;
    bool LoadLightMotion(const std::filesystem::path& vmdPath) override;
    std::optional<LightTrackSample>
        SampleLightMotion(float frame) const override;

    std::size_t MorphCount() const noexcept override;
    bool DescribeMorph(
        std::size_t index,
        MorphDescriptor& output
    ) const override;
    bool ReadMorphState(
        std::size_t index,
        MorphRuntimeState& output
    ) const override;
    std::uint64_t MorphRevision() const noexcept override;

    ModelRuntimeCapabilities Capabilities() const override;
    ModelPhysicsRuntimeInfo PhysicsInfo() const override;

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
