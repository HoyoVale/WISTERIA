#include "pch.hpp"
#include "animator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

Animator::Animator(Pose& pose)
    : pose(&pose)
{
    const Skeleton& skeleton = pose.GetSkeleton();
    this->bindTransforms.reserve(skeleton.BoneCount());
    this->sampledLocalMatrices.reserve(skeleton.BoneCount());
    for (std::size_t index = 0; index < skeleton.BoneCount(); ++index)
    {
        const glm::mat4& bindMatrix =
            skeleton.BoneAt(static_cast<BoneIndex>(index)).bindLocalMatrix;
        this->bindTransforms.push_back(BoneTransform::FromMatrix(bindMatrix));
        this->sampledLocalMatrices.push_back(bindMatrix);
    }
}

void Animator::Play(const AnimationClip& clip, bool restart)
{
    this->ValidateClip(clip);
    if (this->currentClip != &clip || restart)
        this->currentTime = 0.0f;
    this->currentClip = &clip;
    this->playing = true;
    this->paused = false;
    this->Evaluate();
}

void Animator::Stop(bool resetPose)
{
    this->currentClip = nullptr;
    this->currentTime = 0.0f;
    this->playing = false;
    this->paused = false;
    if (resetPose)
        this->pose->ResetToBindPose();
}

void Animator::Pause() noexcept
{
    if (this->playing)
        this->paused = true;
}

void Animator::Resume() noexcept
{
    if (this->currentClip != nullptr && this->playing)
        this->paused = false;
}

void Animator::Update(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
    {
        throw std::invalid_argument(
            "Animator delta time must be finite and non-negative"
        );
    }
    if (!this->playing || this->paused || this->currentClip == nullptr)
        return;

    const float duration = this->currentClip->Duration();
    const float nextTime = this->currentTime + deltaTime * this->speed;
    if (this->looping)
    {
        this->currentTime = std::fmod(nextTime, duration);
    }
    else if (nextTime >= duration)
    {
        this->currentTime = duration;
        this->playing = false;
    }
    else
    {
        this->currentTime = nextTime;
    }
    this->Evaluate();
}

void Animator::Evaluate()
{
    if (this->currentClip == nullptr)
        return;

    const Skeleton& skeleton = this->pose->GetSkeleton();
    for (std::size_t index = 0; index < skeleton.BoneCount(); ++index)
    {
        this->sampledLocalMatrices[index] =
            skeleton.BoneAt(static_cast<BoneIndex>(index)).bindLocalMatrix;
    }
    for (const AnimationTrack& track : this->currentClip->Tracks())
    {
        const std::size_t index = static_cast<std::size_t>(track.Bone());
        this->sampledLocalMatrices[index] =
            track.Sample(this->currentTime, this->bindTransforms[index]).Matrix();
    }
    this->pose->SetLocalMatrices(this->sampledLocalMatrices);
}

bool Animator::IsPlaying() const noexcept
{
    return this->playing && !this->paused;
}

bool Animator::IsPaused() const noexcept
{
    return this->paused;
}

bool Animator::IsLooping() const noexcept
{
    return this->looping;
}

void Animator::SetLooping(bool looping) noexcept
{
    this->looping = looping;
}

float Animator::Time() const noexcept
{
    return this->currentTime;
}

void Animator::SetTime(float time)
{
    if (this->currentClip == nullptr)
        throw std::logic_error("Animator has no current clip");
    if (!std::isfinite(time))
        throw std::invalid_argument("Animator time must be finite");
    this->currentTime = std::clamp(
        time,
        0.0f,
        this->currentClip->Duration()
    );
    this->Evaluate();
}

float Animator::Speed() const noexcept
{
    return this->speed;
}

void Animator::SetSpeed(float speed)
{
    if (!std::isfinite(speed) || speed < 0.0f)
    {
        throw std::invalid_argument(
            "Animator speed must be finite and non-negative"
        );
    }
    this->speed = speed;
}

const AnimationClip* Animator::CurrentClip() const noexcept
{
    return this->currentClip;
}

Pose& Animator::GetPose() noexcept
{
    return *this->pose;
}

const Pose& Animator::GetPose() const noexcept
{
    return *this->pose;
}

void Animator::ValidateClip(const AnimationClip& clip) const
{
    for (const AnimationTrack& track : clip.Tracks())
    {
        if (static_cast<std::size_t>(track.Bone()) >= this->pose->BoneCount())
        {
            throw std::invalid_argument(
                "Animation clip references a bone outside this skeleton"
            );
        }
    }
}
