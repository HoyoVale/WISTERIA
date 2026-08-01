#pragma once

#include "animation.hpp"
#include <vector>

// Per-entity playback state. AnimationClip and Skeleton remain shared model
// resources; only time and the sampled Pose are instance-local.
class Animator
{
public:
    explicit Animator(Pose& pose);

    void Play(const AnimationClip& clip, bool restart = true);
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
    Pose& GetPose() noexcept;
    const Pose& GetPose() const noexcept;

private:
    void ValidateClip(const AnimationClip& clip) const;

    Pose* pose = nullptr;
    const AnimationClip* currentClip = nullptr;
    std::vector<BoneTransform> bindTransforms;
    std::vector<glm::mat4> sampledLocalMatrices;
    float currentTime = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool paused = false;
    bool looping = true;
};
