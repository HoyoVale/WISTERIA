#pragma once

#include "wisteria/animation/animation.hpp"
#include "wisteria/animation/animation_state_machine.hpp"
#include "wisteria/mmd/mmd_pose_solver.hpp"
#include "wisteria/core/root_motion.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// Per-entity playback state. AnimationClip and Skeleton remain shared model
// resources; only time and the sampled Pose are instance-local.
namespace wisteria
{
class Animator
{
public:
    explicit Animator(Pose& pose, MorphState* morphState = nullptr);

    void Play(const AnimationClip& clip, bool restart = true);
    void CrossFade(const AnimationClip& destination, float duration);
    void Stop(bool resetPose = true);
    void Pause() noexcept;
    void Resume() noexcept;
    void Update(float deltaTime);
    void Evaluate();
    void SolveAfterPhysics();

    bool IsPlaying() const noexcept;
    bool IsPaused() const noexcept;
    bool IsLooping() const noexcept;
    void SetLooping(bool looping) noexcept;

    float Time() const noexcept;
    void SetTime(float time);
    float Speed() const noexcept;
    void SetSpeed(float speed);
    std::uint64_t DiscontinuityRevision() const noexcept;

    // R1.8: Generic Deterministic Mode v1 subset gate. True when the
    // animator is exactly the subset represented by the GenericR18 payload:
    // single active clip, no transition, no state machine, no parameters or
    // in-flight triggers, speed == 1, not paused, no MMD IK overrides.
    bool IsDeterministicSubsetCompatible() const noexcept;

    // R1.8: canonical absolute evaluation. Evaluates the active clip at
    // canonicalTime (wrapped for loopMotion, clamped otherwise) and stores
    // exactly one root-motion delta for the canonical interval
    // [previousCanonicalTime, canonicalTime] into pendingRootMotion.
    // Callers must gate with IsDeterministicSubsetCompatible() first.
    void EvaluateCanonicalFrame(
        float previousCanonicalTime,
        float canonicalTime,
        bool loopMotion
    );

    void SetRootMotionBone(BoneIndex boneIndex);
    void ClearRootMotionBone();
    std::optional<BoneIndex> RootMotionBone() const noexcept;
    void SetRootMotionEnabled(bool enabled);
    bool IsRootMotionEnabled() const noexcept;
    RootMotionDelta ConsumeRootMotion() noexcept;
    void SetMmdIkEnabled(BoneIndex controllerBone, bool enabled);
    void ClearMmdIkOverride(BoneIndex controllerBone);
    void ClearMmdIkOverrides();
    bool IsMmdIkEnabled(BoneIndex controllerBone) const;
    void SetMorphState(MorphState& morphState);
    MorphState* TryGetMorphState() noexcept;
    const MorphState* TryGetMorphState() const noexcept;

    const AnimationClip* CurrentClip() const noexcept;
    bool IsTransitioning() const noexcept;
    float TransitionProgress() const noexcept;
    AnimationStateMachine& GetStateMachine() noexcept;
    const AnimationStateMachine& GetStateMachine() const noexcept;

    void SetFloat(std::string_view name, float value);
    float GetFloat(std::string_view name) const;
    void SetBool(std::string_view name, bool value);
    bool GetBool(std::string_view name) const;
    void SetTrigger(std::string_view name);
    bool IsTriggerSet(std::string_view name) const;
    void ResetTrigger(std::string_view name);

    Pose& GetPose() noexcept;
    const Pose& GetPose() const noexcept;

private:
    struct TransitionState
    {
        const AnimationClip* sourceClip = nullptr;
        float sourceTime = 0.0f;
        float sourceSpeed = 1.0f;
        float elapsed = 0.0f;
        float duration = 0.0f;
        bool sourceLooping = true;
        bool sourceFrozen = false;
        std::unordered_map<BoneIndex, bool> frozenMmdIkStates;
    };

    struct PlaybackAdvance
    {
        float previousTime = 0.0f;
        float currentTime = 0.0f;
        std::uint64_t loopCount = 0;
    };

    void ValidateClip(const AnimationClip& clip) const;
    PlaybackAdvance Advance(
        const AnimationClip& clip,
        float deltaTime,
        float playbackSpeed,
        bool playbackLooping,
        float& time,
        bool& remainsPlaying
    ) const;
    RootMotionDelta ExtractRootMotion(
        const AnimationClip& clip,
        const PlaybackAdvance& advance
    ) const;
    void AccumulateRootMotion(const RootMotionDelta& delta);
    void ApplyEvaluatedPose(const PoseBuffer& evaluatedPose);
    void SampleMorphWeights(
        const AnimationClip& clip,
        float time,
        std::vector<float>& output
    ) const;
    void ApplyEvaluatedMorphWeights(std::span<const float> weights);
    bool EvaluateMmdIkState(BoneIndex controllerBone) const;
    void ResetRootMotion() noexcept;
    void MarkDiscontinuity() noexcept;

    Pose* pose = nullptr;
    MorphState* morphState = nullptr;
    const AnimationClip* currentClip = nullptr;
    PoseBuffer sampledPose;
    PoseBuffer transitionSourcePose;
    PoseBuffer blendedPose;
    PoseBuffer outputPose;
    PoseBuffer beforePhysicsPose;
    MmdPoseSolver mmdPoseSolver;
    std::vector<float> sampledMorphWeights;
    std::vector<float> transitionSourceMorphWeights;
    std::vector<float> blendedMorphWeights;
    AnimationStateMachine stateMachine;
    std::optional<TransitionState> transition;
    std::unordered_map<std::string, float> floatParameters;
    std::unordered_map<std::string, bool> boolParameters;
    std::unordered_set<std::string> triggerParameters;
    std::unordered_set<std::string> activeTriggers;
    std::optional<BoneIndex> rootMotionBone;
    RootMotionDelta pendingRootMotion;
    std::unordered_map<BoneIndex, bool> mmdIkOverrides;
    std::uint64_t lastAppliedMorphRevision =
        std::numeric_limits<std::uint64_t>::max();
    std::uint64_t discontinuityRevision = 0U;
    float currentTime = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool paused = false;
    bool looping = true;
    bool rootMotionEnabled = false;
};
}  // namespace wisteria
