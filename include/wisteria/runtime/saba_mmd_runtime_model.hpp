#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/runtime/determinism.hpp"

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
class SabaMmdRuntimeModel final
    : public MmdRuntimeModel,
      public IDeterministicFrameStepper,
      public IDeterministicPhysicsObservation,
      public IPhysicsStateAccess
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
    bool SetMorphOverride(std::string_view name, float weight) override;
    void ClearMorphOverride(std::string_view name) override;
    void ClearAllMorphOverrides() override;

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

    // R1.2A deterministic timeline (see determinism.hpp for semantics).
    TimelineStatus EvaluateTick(
        MotionFrameIndex target,
        SeekPolicy policy,
        const ReplayConfig& config = {}
    ) override;

    // IDeterministicFrameStepper
    TimelineStatus PrepareFrameZero(
        const ReplayConfig& config
    ) override;
    TimelineStatus StepMotionFrameExact(
        MotionFrameIndex frame,
        const ReplayConfig& config
    ) override;

    // IDeterministicPhysicsObservation
    TimelineStatus CaptureState(PhysicsSnapshot& output) const override;
    TimelineStatus ReadStepDiagnostics(
        PhysicsStepDiagnostics& output
    ) const override;

    // R1.2B physics snapshot restore (see determinism.hpp / R1.2B contract).
    TimelineStatus CapturePhysicsSnapshot(
        PhysicsSnapshot& output
    ) const override;
    TimelineStatus RestorePhysicsSnapshot(
        const PhysicsSnapshot& snapshot
    ) override;

    // IPhysicsStateAccess
    TimelineStatus RestoreState(
        const PhysicsSnapshot& snapshot
    ) override;

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
    // R1.2A deterministic helpers. They own the exact evaluation order; the
    // public EvaluateTick/stepper entries only validate and delegate.
    TimelineStatus ValidateReplayConfig(
        const ReplayConfig& config
    );
    // No-step canonical reset: bind kinematic motion states, re-seat body
    // transforms at the current animated pose, zero velocities/forces,
    // clean broadphase pairs and reset Bullet's frame accumulator.
    TimelineStatus ResetCanonicalNoStep();
    // Evaluates one motion frame without physics stepping, then performs a
    // canonical no-step reset and publishes Pose/Vertex.
    TimelineStatus EvaluateFrameCanonical(
        MotionFrameIndex frame,
        const ReplayConfig& config
    );
    // Executes exactly one 30Hz motion frame including the fixed physics
    // substeps, records step diagnostics and publishes Pose/Vertex.
    TimelineStatus StepFrameExact(
        MotionFrameIndex frame,
        const ReplayConfig& config
    );
    // R1.2B Phase 0 validation (read-only; never mutates the world).
    TimelineStatus ValidateSnapshotForRestore(
        const PhysicsSnapshot& snapshot
    ) const;
    // R1.2B Phase 1-6 write-back. Only called after validation passes.
    TimelineStatus RestorePhases(const PhysicsSnapshot& snapshot);
    void ComputeLayoutFingerprint(
        std::uint64_t& fingerprint
    ) const;
    void ComputeConfigurationFingerprint(
        std::uint64_t& fingerprint
    ) const;
    bool IsPoisoned() const noexcept;
    void EnterPoisoned() noexcept;
    // Re-applies engine-level named morph overrides after VMD evaluation so
    // they survive playback/replay (contract §5 application order).
    void ApplyUserMorphOverrides();

#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)
public:
    // Test-only: advances the restored Bullet world by exactSubsteps fixed
    // substeps without re-evaluating animation and without touching the
    // public deterministicPrepared contract. Used by T5/T18.
    TimelineStatus StepRestoredPhysicsForProbe(
        std::uint32_t exactSubsteps
    );
    // Test-only fault injection: make RestorePhases throw after completing
    // the given phase (1..6) so the instance enters Poisoned.
    void SetFaultInjectionPhase(int phase) noexcept;
    int FaultInjectionPhase() const noexcept;
#endif

    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace wisteria
