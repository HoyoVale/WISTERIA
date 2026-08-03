#pragma once

#include "wisteria/mmd/physics/mmd_physics_asset.hpp"
#include "wisteria/mmd/physics/mmd_physics_diagnostics.hpp"
#include "wisteria/mmd/physics/mmd_physics_policy.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/physics/physics_types.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

class PhysicsWorld;
class Pose;
class Transform;

// Per-Entity runtime bridge between immutable PMX physics metadata and the
// shared scene PhysicsWorld. Bullet objects remain owned by PhysicsWorld;
// this object owns only safe WISTERIA handles and MMD synchronization state.
class MmdPhysicsInstance final : public PhysicsInstance
{
public:
    MmdPhysicsInstance(
        PhysicsWorld& world,
        const MmdPhysicsAsset& asset,
        Pose& pose,
        Transform& transform,
        const MorphState* morphState = nullptr,
        MmdPhysicsRuntimePolicy policy =
            MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults()
    );
    ~MmdPhysicsInstance();

    MmdPhysicsInstance(const MmdPhysicsInstance&) = delete;
    MmdPhysicsInstance& operator=(const MmdPhysicsInstance&) = delete;
    MmdPhysicsInstance(MmdPhysicsInstance&&) = delete;
    MmdPhysicsInstance& operator=(MmdPhysicsInstance&&) = delete;

    std::size_t RigidBodyCount() const noexcept;
    std::size_t ConstraintCount() const noexcept;
    PhysicsBodyHandle BodyHandleAt(RigidBodyIndex index) const;
    PhysicsBodyState BodyStateAt(RigidBodyIndex index) const;
    void ApplyCentralImpulse(RigidBodyIndex index, const glm::vec3& impulse);
    void ApplyTorqueImpulse(RigidBodyIndex index, const glm::vec3& impulse);
    void ApplyImpulseMorphs(const MorphState& morphState);

    void PrepareSimulation(float deltaTime) override;
    void PrepareSimulationSubstep(
        float alpha,
        float fixedTimeStep
    ) override;
    void ObserveSimulationSubstep(float fixedTimeStep) override;
    void FinishSimulation() override;
    void ResetSimulation() override;
    PhysicsStabilizationRequest StabilizationRequest() const noexcept override;
    void PrepareStabilizationStep(float fixedTimeStep) override;
    void ObserveStabilizationStep(std::size_t completedSteps) override;
    void CompleteStabilization() override;
    void AppendDebugLines(
        std::vector<PhysicsDebugLine>& lines
    ) const override;

    void SetDebugOverlay(MmdPhysicsDebugOverlay overlay) noexcept;
    MmdPhysicsDebugOverlay DebugOverlay() const noexcept;
    MmdPhysicsDebugOverlay CycleDebugOverlay() noexcept;
    const char* DebugOverlayName() const noexcept;
    const MmdPhysicsAlignmentSummary& AlignmentSummary() const noexcept;
    const MmdPhysicsRecoveryStatistics& RecoveryStatistics() const noexcept;
    const MmdPhysicsRuntimePolicy& RuntimePolicy() const noexcept;

    struct MmdRuntimeJointDiagnostics
    {
        float maximumPositionSeparation = 0.0f;
        float maximumRotationErrorDegrees = 0.0f;
        float maximumLinearLimitViolation = 0.0f;
        float maximumAngularLimitViolationDegrees = 0.0f;
        std::size_t jointsOverFailureThreshold = 0U;
        bool finite = true;
    };
    MmdRuntimeJointDiagnostics RuntimeJointDiagnostics() const;

    void SetFidelityDebugLayer(
        MmdPhysicsFidelityDebugLayer layer
    ) noexcept;
    MmdPhysicsFidelityDebugLayer FidelityDebugLayer() const noexcept;
    MmdPhysicsFidelityDebugLayer CycleFidelityDebugLayer() noexcept;
    const char* FidelityDebugLayerName() const noexcept;

    void SetPhysicsWithBoneSyncMode(
        MmdPhysicsWithBoneSyncMode mode
    ) noexcept;
    MmdPhysicsWithBoneSyncMode PhysicsWithBoneSyncMode() const noexcept;
    MmdPhysicsWithBoneSyncMode CyclePhysicsWithBoneSyncMode() noexcept;
    const char* PhysicsWithBoneSyncModeName() const noexcept;
    const MmdPhysicsFidelityStatistics& FidelityStatistics() const noexcept;
    const MmdPhysicsCollisionStatistics& CollisionStatistics() const noexcept;
    std::span<const MmdPhysicsContactDiagnostic> ContactDiagnostics() const noexcept;
    void LogCollisionReport(std::size_t maximumEntries = 20U) const;

    void SetGravityMode(MmdPhysicsGravityMode mode);
    MmdPhysicsGravityMode GravityMode() const noexcept;
    MmdPhysicsGravityMode CycleGravityMode();
    const char* GravityModeName() const noexcept;
    const MmdPhysicsGravityStatistics& GravityStatistics() const noexcept;
    std::span<const MmdPhysicsChainBalanceStatistics>
        ChainBalanceStatistics() const noexcept;
    void LogGravityReport() const;
    std::span<const std::uint8_t> DrivenBoneModes() const noexcept;
    void SetSampledVertexCount(std::size_t count) noexcept;
    bool StabilizationFailed() const noexcept;
    std::size_t PendingStabilizationSteps() const noexcept;
    void LogAlignmentReport(std::size_t maximumEntries = 16U) const;

private:

    struct AlignmentRecord
    {
        std::size_t bodyIndex = 0U;
        glm::mat4 sourceBindModel{1.0f};
        glm::mat4 skeletonBindModel{1.0f};
        glm::mat4 createdBulletBindModel{1.0f};
        glm::mat4 resetTargetModel{1.0f};
        glm::mat4 postResetBulletModel{1.0f};
        float bindPositionError = 0.0f;
        float bindRotationErrorDegrees = 0.0f;
        float bulletPositionError = 0.0f;
        float bulletRotationErrorDegrees = 0.0f;
        float postResetPositionError = 0.0f;
        float postResetRotationErrorDegrees = 0.0f;
    };

    struct JointSnapshot
    {
        const char* stage = "none";
        std::size_t completedSteps = 0U;
        float maximumPositionSeparation = 0.0f;
        float maximumRotationErrorDegrees = 0.0f;
        float maximumLinearLimitViolation = 0.0f;
        float maximumAngularLimitViolationDegrees = 0.0f;
        float maximumStabilizationLinearViolation = 0.0f;
        float maximumStabilizationAngularViolationDegrees = 0.0f;
        std::size_t wideTravelHelperJoints = 0U;
        std::size_t jointsOverFailureThreshold = 0U;
        std::size_t maximumJointIndex = std::numeric_limits<std::size_t>::max();
        std::size_t maximumLinearViolationJointIndex =
            std::numeric_limits<std::size_t>::max();
        std::size_t maximumAngularViolationJointIndex =
            std::numeric_limits<std::size_t>::max();
        bool finite = true;
    };

    struct RuntimeBody
    {
        const MmdRigidBodyDefinition* definition = nullptr;
        PhysicsBodyHandle handle{};
        // Last target actually submitted before a Bullet fixed tick.
        glm::vec3 lastAnimatedPosition{0.0f};
        glm::quat lastAnimatedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        // Immutable previous/current render-frame animation endpoints. Every
        // fixed tick samples these at its exact time inside the render frame,
        // while lastAnimated* remains the previous submitted physics sample
        // for velocity calculation.
        glm::vec3 frameStartAnimatedPosition{0.0f};
        glm::quat frameStartAnimatedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 frameTargetAnimatedPosition{0.0f};
        glm::quat frameTargetAnimatedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool hasAnimatedTransform = false;
        glm::mat4 createdBulletBindModelTransform{1.0f};
        glm::mat4 resetTargetModelTransform{1.0f};
        glm::mat4 postResetBulletModelTransform{1.0f};
        glm::mat4 prePhysicsAnimatedModelTransform{1.0f};
        bool ccdCandidate = false;
        bool ccdActive = false;
        bool denseMarginAdjusted = false;
        float ccdFeatureSize = 0.0f;
        float ccdMaximumExtent = 0.0f;
        float ccdMotionThreshold = 0.0f;
        float ccdSweptSphereRadius = 0.0f;
        float ccdIdleSeconds = 0.0f;
        float baseLinearDamping = 0.0f;
        float baseAngularDamping = 0.0f;
        float appliedGravityScale = 1.0f;
        float appliedLinearDamping = 0.0f;
        float appliedAngularDamping = 0.0f;
    };

    struct RecoveryEdge
    {
        std::size_t bodyIndex = std::numeric_limits<std::size_t>::max();
        std::size_t jointIndex = std::numeric_limits<std::size_t>::max();
    };

    struct RecoveryTrigger
    {
        MmdPhysicsRecoveryReason reason = MmdPhysicsRecoveryReason::None;
        std::size_t seedBodyIndex =
            std::numeric_limits<std::size_t>::max();
        std::size_t jointIndex =
            std::numeric_limits<std::size_t>::max();
        bool immediate = false;
        float score = 0.0f;
        float positionError = 0.0f;
        float linearViolation = 0.0f;
        float angularViolationDegrees = 0.0f;
        float linearSpeed = 0.0f;
        float angularSpeed = 0.0f;
        float anchorDistance = 0.0f;
        float bindChainLength = 0.0f;
        float normalizedExtension = 0.0f;
    };

    struct RecoveryChain
    {
        std::vector<std::size_t> bodyIndices;
        std::vector<std::size_t> jointIndices;
        std::size_t anchorBodyIndex =
            std::numeric_limits<std::size_t>::max();
        float abnormalSeconds = 0.0f;
        float cooldownSeconds = 0.0f;
        float fuseWindowSeconds = 0.0f;
        float fuseRemainingSeconds = 0.0f;
        std::size_t recoveriesInWindow = 0U;
        bool fuseSuppressionLatched = false;
        RecoveryTrigger pendingTrigger{};
    };

    struct GravityChain
    {
        std::vector<std::size_t> bodyIndices;
        MmdPhysicsChainKind kind = MmdPhysicsChainKind::General;
        float gravityScale = 1.0f;
        float minimumLinearDamping = 0.0f;
        float minimumAngularDamping = 0.0f;
        std::size_t anchorBodyIndex =
            std::numeric_limits<std::size_t>::max();
    };

    void PrePhysicsUpdate(const Transform& transform, float deltaTime);
    void PostPhysicsUpdate(const Transform& transform);
    void ResetToPose(const Transform& transform);
    void CaptureConstraintPreservingResetTargets();
    void ApplyResetTargets(const Transform& transform);
    void BuildRecoveryChains();
    void ConfigureGravityBalanceProfiles();
    void ApplyGravityBalanceSettings(bool force = false);
    void UpdateGravityBalanceStatistics();
    void ConfigureCollisionTopology();
    void UpdateAdaptiveCcd(float fixedTimeStep);
    void UpdateCollisionDiagnostics();
    void RecoverAbnormalChains(float fixedTimeStep);
    std::vector<std::size_t> CollectRecoveryRegion(
        std::size_t chainIndex,
        const RecoveryTrigger& trigger
    ) const;
    void RecoverChain(
        std::size_t chainIndex,
        const RecoveryTrigger& trigger
    );
    void UpdateRecoveryStatistics() noexcept;
    JointSnapshot CaptureJointSnapshot(
        const char* stage,
        std::size_t completedSteps = 0U
    ) const;
    void LogJointSnapshot(const JointSnapshot& snapshot) const;
    void SetFailureFreeze(bool frozen);
    void BuildAlignmentDiagnostics();
    void LogAlignmentSummary() const;
    void DestroyRuntime() noexcept;
    void UpdateFidelityStatistics();

    PhysicsWorld* world = nullptr;
    const MmdPhysicsAsset* asset = nullptr;
    Pose* pose = nullptr;
    Transform* transform = nullptr;
    const MorphState* morphState = nullptr;
    MmdPhysicsRuntimePolicy runtimePolicy{};
    std::vector<RuntimeBody> rigidBodies;
    std::vector<std::size_t> runtimeBodyByWorldHandle;
    std::vector<PhysicsConstraintHandle> constraints;
    std::vector<std::size_t> drivenRuntimeBodyByBone;
    // 0 = not driven by physics, otherwise MmdRigidBodyMode + 1.
    std::vector<std::uint8_t> drivenBoneModes;
    std::vector<glm::mat4> localMatrixScratch;
    std::vector<glm::mat4> globalMatrixScratch;
    std::vector<MmdRigidBodyImpulse> impulseScratch;
    std::vector<RecoveryChain> recoveryChains;
    std::vector<std::size_t> recoveryChainByBody;
    std::vector<std::vector<RecoveryEdge>> recoveryAdjacency;
    std::vector<GravityChain> gravityChains;
    std::vector<std::size_t> gravityChainByBody;
    std::vector<float> recoveryJointSeverityHistory;
    std::vector<float> recoveryBindPathLengthByBody;
    std::vector<float> recoveryPreviousNormalizedExtension;
    std::vector<AlignmentRecord> alignmentRecords;
    std::vector<JointSnapshot> jointSnapshots;
    JointSnapshot createdJointSnapshot{};
    MmdPhysicsAlignmentSummary alignmentSummary;
    MmdPhysicsRecoveryStatistics recoveryStatistics;
    MmdPhysicsFidelityStatistics fidelityStatistics;
    MmdPhysicsCollisionStatistics collisionStatistics;
    std::vector<MmdPhysicsContactDiagnostic> contactDiagnostics;
    MmdPhysicsGravityStatistics gravityStatistics;
    std::vector<MmdPhysicsChainBalanceStatistics> chainBalanceStatistics;
    MmdPhysicsGravityMode gravityMode = MmdPhysicsGravityMode::Original;
    glm::vec3 lastAppliedWorldGravity{std::numeric_limits<float>::quiet_NaN()};
    MmdPhysicsDebugOverlay debugOverlay = MmdPhysicsDebugOverlay::Off;
    MmdPhysicsFidelityDebugLayer fidelityDebugLayer =
        MmdPhysicsFidelityDebugLayer::Off;
    MmdPhysicsWithBoneSyncMode physicsWithBoneSyncMode =
        MmdPhysicsWithBoneSyncMode::RotationOnly;
    std::size_t pendingStabilizationSteps = 0U;
    bool resetTargetRefreshPending = false;
    bool stabilizationFailed = false;
    bool suppressImpulseMorphOnce = false;
};
