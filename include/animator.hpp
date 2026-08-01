#pragma once

#include "animation.hpp"
#include "animation_state_machine.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// Per-entity playback state. AnimationClip and Skeleton remain shared model
// resources; only time and the sampled Pose are instance-local.
class Animator
{
public:
    explicit Animator(Pose& pose);

    void Play(const AnimationClip& clip, bool restart = true);
    void CrossFade(const AnimationClip& destination, float duration);
    void Stop(bool resetPose = true);
    void Pause() noexcept;
    void Resume() noexcept;
    void Update(float deltaTime);
    void Evaluate();

    bool IsPlaying() const noexcept;
    bool IsPaused() const noexcept;
    bool IsLooping() const noexcept;
    void SetLooping(bool looping) noexcept;

    float Time() const noexcept;
    void SetTime(float time);
    float Speed() const noexcept;
    void SetSpeed(float speed);

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
    };

    void ValidateClip(const AnimationClip& clip) const;

    Pose* pose = nullptr;
    const AnimationClip* currentClip = nullptr;
    PoseBuffer sampledPose;
    PoseBuffer transitionSourcePose;
    PoseBuffer blendedPose;
    AnimationStateMachine stateMachine;
    std::optional<TransitionState> transition;
    std::unordered_map<std::string, float> floatParameters;
    std::unordered_map<std::string, bool> boolParameters;
    std::unordered_set<std::string> triggerParameters;
    std::unordered_set<std::string> activeTriggers;
    float currentTime = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool paused = false;
    bool looping = true;
};
