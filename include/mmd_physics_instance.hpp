#pragma once

#include "mmd_physics_asset.hpp"
#include "morph.hpp"
#include "physics_instance.hpp"
#include "physics_types.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

class PhysicsWorld;
class Pose;
class Transform;

enum class MmdPhysicsDebugOverlay : std::uint8_t
{
    Off,
    BindPose,
    ResetPose,
    Runtime,
    All
};

struct MmdPhysicsAlignmentSummary
{
    std::size_t bodyCount = 0U;
    std::size_t jointCount = 0U;
    float maximumBindPositionError = 0.0f;
    float maximumBindRotationErrorDegrees = 0.0f;
    float maximumBulletPositionError = 0.0f;
    float maximumBulletRotationErrorDegrees = 0.0f;
    float maximumPostResetPositionError = 0.0f;
    float maximumPostResetRotationErrorDegrees = 0.0f;
    float maximumSkinningBindError = 0.0f;
    bool nonIdentityBindSpace = false;
};

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
        const MorphState* morphState = nullptr
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
    };

    void PrePhysicsUpdate(const Transform& transform, float deltaTime);
    void PostPhysicsUpdate(const Transform& transform);
    void ResetToPose(const Transform& transform);
    void CaptureConstraintPreservingResetTargets();
    void ApplyResetTargets(const Transform& transform);
    JointSnapshot CaptureJointSnapshot(
        const char* stage,
        std::size_t completedSteps = 0U
    ) const;
    void LogJointSnapshot(const JointSnapshot& snapshot) const;
    void SetFailureFreeze(bool frozen);
    void BuildAlignmentDiagnostics();
    void LogAlignmentSummary() const;
    void DestroyRuntime() noexcept;

    PhysicsWorld* world = nullptr;
    const MmdPhysicsAsset* asset = nullptr;
    Pose* pose = nullptr;
    Transform* transform = nullptr;
    const MorphState* morphState = nullptr;
    std::vector<RuntimeBody> rigidBodies;
    std::vector<PhysicsConstraintHandle> constraints;
    std::vector<std::size_t> drivenRuntimeBodyByBone;
    std::vector<glm::mat4> localMatrixScratch;
    std::vector<glm::mat4> globalMatrixScratch;
    std::vector<MmdRigidBodyImpulse> impulseScratch;
    std::vector<AlignmentRecord> alignmentRecords;
    std::vector<JointSnapshot> jointSnapshots;
    JointSnapshot createdJointSnapshot{};
    MmdPhysicsAlignmentSummary alignmentSummary;
    MmdPhysicsDebugOverlay debugOverlay = MmdPhysicsDebugOverlay::Off;
    std::size_t pendingStabilizationSteps = 0U;
    bool resetTargetRefreshPending = false;
    bool stabilizationFailed = false;
    bool suppressImpulseMorphOnce = false;
};
