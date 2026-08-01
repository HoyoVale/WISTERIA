#include "pch.hpp"
#include "animator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
std::string ParameterName(std::string_view name)
{
    if (name.empty())
        throw std::invalid_argument("Animator parameter name must not be empty");
    return std::string(name);
}

bool AdvancePlaybackTime(
    const AnimationClip& clip,
    float deltaTime,
    float speed,
    bool looping,
    float& time
)
{
    const double nextTime = static_cast<double>(time) +
        static_cast<double>(deltaTime) * static_cast<double>(speed);
    if (!std::isfinite(nextTime))
        throw std::overflow_error("Animator playback time overflowed");

    const double duration = static_cast<double>(clip.Duration());
    if (looping)
    {
        time = static_cast<float>(std::fmod(nextTime, duration));
        return true;
    }
    if (nextTime >= duration)
    {
        time = clip.Duration();
        return false;
    }
    time = static_cast<float>(nextTime);
    return true;
}
}

Animator::Animator(Pose& pose)
    : pose(&pose),
      sampledPose(pose.GetSkeleton()),
      transitionSourcePose(pose.GetSkeleton()),
      blendedPose(pose.GetSkeleton())
{
}

void Animator::Play(const AnimationClip& clip, bool restart)
{
    this->ValidateClip(clip);
    this->transition.reset();
    if (this->currentClip != &clip || restart)
        this->currentTime = 0.0f;
    this->currentClip = &clip;
    this->playing = true;
    this->paused = false;
    this->Evaluate();
}

void Animator::CrossFade(
    const AnimationClip& destination,
    float duration
)
{
    this->ValidateClip(destination);
    if (!std::isfinite(duration) || duration < 0.0f)
    {
        throw std::invalid_argument(
            "Animation cross-fade duration must be finite and non-negative"
        );
    }
    if (this->currentClip == nullptr || duration == 0.0f)
    {
        this->Play(destination, true);
        return;
    }

    TransitionState nextTransition;
    if (this->transition.has_value())
    {
        // Preserve the exact currently displayed mixed pose. This avoids a
        // visual pop when a new transition interrupts an unfinished one.
        this->transitionSourcePose = this->blendedPose;
        nextTransition.sourceFrozen = true;
    }
    else
    {
        nextTransition.sourceClip = this->currentClip;
        nextTransition.sourceTime = this->currentTime;
        nextTransition.sourceSpeed = this->speed;
        nextTransition.sourceLooping = this->looping;
    }
    nextTransition.duration = duration;

    this->transition = nextTransition;
    this->currentClip = &destination;
    this->currentTime = 0.0f;
    this->playing = true;
    this->paused = false;
    this->Evaluate();
}

void Animator::Stop(bool resetPose)
{
    this->transition.reset();
    this->currentClip = nullptr;
    this->currentTime = 0.0f;
    this->playing = false;
    this->paused = false;
    if (resetPose)
        this->pose->ResetToBindPose();
}

void Animator::Pause() noexcept
{
    if (this->playing || this->transition.has_value())
        this->paused = true;
}

void Animator::Resume() noexcept
{
    if (this->currentClip != nullptr &&
        (this->playing || this->transition.has_value()))
    {
        this->paused = false;
    }
}

void Animator::Update(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
    {
        throw std::invalid_argument(
            "Animator delta time must be finite and non-negative"
        );
    }
    try
    {
        this->stateMachine.Update(*this);
    }
    catch (...)
    {
        this->activeTriggers.clear();
        throw;
    }
    this->activeTriggers.clear();

    if (this->paused || this->currentClip == nullptr ||
        (!this->playing && !this->transition.has_value()))
    {
        return;
    }

    if (this->transition.has_value())
    {
        TransitionState& activeTransition = *this->transition;
        if (!activeTransition.sourceFrozen)
        {
            AdvancePlaybackTime(
                *activeTransition.sourceClip,
                deltaTime,
                activeTransition.sourceSpeed,
                activeTransition.sourceLooping,
                activeTransition.sourceTime
            );
        }
        this->playing = AdvancePlaybackTime(
            *this->currentClip,
            deltaTime,
            this->speed,
            this->looping,
            this->currentTime
        );
        activeTransition.elapsed = std::min(
            activeTransition.duration,
            activeTransition.elapsed + deltaTime
        );
        this->Evaluate();
        if (activeTransition.elapsed >= activeTransition.duration)
        {
            this->transition.reset();
            this->sampledPose.ApplyTo(*this->pose);
        }
        return;
    }

    this->playing = AdvancePlaybackTime(
        *this->currentClip,
        deltaTime,
        this->speed,
        this->looping,
        this->currentTime
    );
    this->Evaluate();
}

void Animator::Evaluate()
{
    if (this->currentClip == nullptr)
        return;

    this->currentClip->Sample(this->currentTime, this->sampledPose);
    if (!this->transition.has_value())
    {
        this->sampledPose.ApplyTo(*this->pose);
        return;
    }

    const TransitionState& activeTransition = *this->transition;
    if (!activeTransition.sourceFrozen)
    {
        activeTransition.sourceClip->Sample(
            activeTransition.sourceTime,
            this->transitionSourcePose
        );
    }
    const float weight = std::clamp(
        activeTransition.elapsed / activeTransition.duration,
        0.0f,
        1.0f
    );
    BlendPoseBuffers(
        this->transitionSourcePose,
        this->sampledPose,
        weight,
        this->blendedPose
    );
    this->blendedPose.ApplyTo(*this->pose);
}

bool Animator::IsPlaying() const noexcept
{
    return (this->playing || this->transition.has_value()) && !this->paused;
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

bool Animator::IsTransitioning() const noexcept
{
    return this->transition.has_value();
}

float Animator::TransitionProgress() const noexcept
{
    if (!this->transition.has_value())
        return 0.0f;
    return std::clamp(
        this->transition->elapsed / this->transition->duration,
        0.0f,
        1.0f
    );
}

AnimationStateMachine& Animator::GetStateMachine() noexcept
{
    return this->stateMachine;
}

const AnimationStateMachine& Animator::GetStateMachine() const noexcept
{
    return this->stateMachine;
}

void Animator::SetFloat(std::string_view name, float value)
{
    const std::string key = ParameterName(name);
    if (!std::isfinite(value))
        throw std::invalid_argument("Animator float parameter must be finite");
    if (this->boolParameters.contains(key) ||
        this->triggerParameters.contains(key))
        throw std::invalid_argument("Animator parameter has a different type: " + key);
    this->floatParameters[key] = value;
}

float Animator::GetFloat(std::string_view name) const
{
    const std::string key = ParameterName(name);
    const auto iterator = this->floatParameters.find(key);
    if (iterator == this->floatParameters.end())
        throw std::out_of_range("Animator float parameter does not exist: " + key);
    return iterator->second;
}

void Animator::SetBool(std::string_view name, bool value)
{
    const std::string key = ParameterName(name);
    if (this->floatParameters.contains(key) ||
        this->triggerParameters.contains(key))
        throw std::invalid_argument("Animator parameter has a different type: " + key);
    this->boolParameters[key] = value;
}

bool Animator::GetBool(std::string_view name) const
{
    const std::string key = ParameterName(name);
    const auto iterator = this->boolParameters.find(key);
    if (iterator == this->boolParameters.end())
        throw std::out_of_range("Animator bool parameter does not exist: " + key);
    return iterator->second;
}

void Animator::SetTrigger(std::string_view name)
{
    const std::string key = ParameterName(name);
    if (this->floatParameters.contains(key) || this->boolParameters.contains(key))
        throw std::invalid_argument("Animator parameter has a different type: " + key);
    this->triggerParameters.insert(key);
    this->activeTriggers.insert(key);
}

bool Animator::IsTriggerSet(std::string_view name) const
{
    return this->activeTriggers.contains(std::string(name));
}

void Animator::ResetTrigger(std::string_view name)
{
    this->activeTriggers.erase(std::string(name));
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
