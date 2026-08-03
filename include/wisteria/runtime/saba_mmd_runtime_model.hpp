#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"

#include <filesystem>
#include <memory>
#include <span>

struct SabaPhysicsSettings
{
    float fixedTimeStep = 1.0f / 120.0f;
    int maxSubSteps = 10;
    glm::vec3 gravity{0.0f, -98.0f, 0.0f};
};

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

    bool Initialize() override;
    void Update(float deltaTime) override;
    void Reset() override;
    Pose& GetPose() override;
    bool NeedsDynamicVertexUpload() const noexcept override;
    void UploadDynamicVertices(Mesh& mesh) override;
    PhysicsInstance* TryGetPhysicsInstance() noexcept override;

    void SetMmdIkEnabled(BoneIndex bone, bool enabled) override;

    bool LoadMotion(const std::filesystem::path& vmdPath) override;
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
    struct Impl;
    std::unique_ptr<Impl> impl;
};
