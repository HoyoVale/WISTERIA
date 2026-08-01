#include "pch.hpp"
#include "animator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <stdexcept>

namespace
{
std::string ParameterName(std::string_view name)
{
    if (name.empty())
        throw std::invalid_argument("Animator parameter name must not be empty");
    return std::string(name);
}

RootMotionDelta ComposeRootMotion(
    const RootMotionDelta& first,
    const RootMotionDelta& second
)
{
    return RootMotionDelta{
        first.translation + first.rotation * second.translation,
        glm::normalize(first.rotation * second.rotation)
    };
}

RootMotionDelta BlendRootMotion(
    const RootMotionDelta& source,
    const RootMotionDelta& destination,
    float weight
)
{
    glm::quat destinationRotation = destination.rotation;
    if (glm::dot(source.rotation, destinationRotation) < 0.0f)
        destinationRotation = -destinationRotation;
    return RootMotionDelta{
        glm::mix(source.translation, destination.translation, weight),
        glm::normalize(glm::slerp(
            source.rotation,
            destinationRotation,
            weight
        ))
    };
}

glm::mat4 MotionMatrix(const BoneTransform& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.translation) *
        glm::mat4_cast(glm::normalize(transform.rotation));
}

glm::mat4 MatrixPower(glm::mat4 base, std::uint64_t exponent)
{
    glm::mat4 result(1.0f);
    while (exponent > 0U)
    {
        if ((exponent & 1U) != 0U)
            result *= base;
        exponent >>= 1U;
        if (exponent > 0U)
            base *= base;
    }
    return result;
}

RootMotionDelta RootMotionFromMatrix(const glm::mat4& matrix)
{
    const BoneTransform transform = BoneTransform::FromMatrix(matrix);
    return RootMotionDelta{transform.translation, transform.rotation};
}
}

Animator::Animator(Pose& pose)
    : pose(&pose),
      sampledPose(pose.GetSkeleton()),
      transitionSourcePose(pose.GetSkeleton()),
      blendedPose(pose.GetSkeleton()),
      outputPose(pose.GetSkeleton())
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
    this->ResetRootMotion();
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
    this->ResetRootMotion();
    this->Evaluate();
}

void Animator::Stop(bool resetPose)
{
    this->transition.reset();
    this->currentClip = nullptr;
    this->currentTime = 0.0f;
    this->playing = false;
    this->paused = false;
    this->ResetRootMotion();
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
        const float previousWeight = std::clamp(
            activeTransition.elapsed / activeTransition.duration,
            0.0f,
            1.0f
        );
        PlaybackAdvance sourceAdvance;
        bool sourcePlaying = true;
        if (!activeTransition.sourceFrozen)
        {
            sourceAdvance = this->Advance(
                *activeTransition.sourceClip,
                deltaTime,
                activeTransition.sourceSpeed,
                activeTransition.sourceLooping,
                activeTransition.sourceTime,
                sourcePlaying
            );
        }
        const PlaybackAdvance destinationAdvance = this->Advance(
            *this->currentClip,
            deltaTime,
            this->speed,
            this->looping,
            this->currentTime,
            this->playing
        );
        activeTransition.elapsed = std::min(
            activeTransition.duration,
            activeTransition.elapsed + deltaTime
        );
        const float currentWeight = std::clamp(
            activeTransition.elapsed / activeTransition.duration,
            0.0f,
            1.0f
        );
        const RootMotionDelta sourceMotion = activeTransition.sourceFrozen
            ? RootMotionDelta{}
            : this->ExtractRootMotion(
                *activeTransition.sourceClip,
                sourceAdvance
            );
        const RootMotionDelta destinationMotion = this->ExtractRootMotion(
            *this->currentClip,
            destinationAdvance
        );
        this->AccumulateRootMotion(BlendRootMotion(
            sourceMotion,
            destinationMotion,
            (previousWeight + currentWeight) * 0.5f
        ));
        this->Evaluate();
        if (activeTransition.elapsed >= activeTransition.duration)
        {
            this->transition.reset();
            this->ApplyEvaluatedPose(this->sampledPose);
        }
        return;
    }

    const PlaybackAdvance advance = this->Advance(
        *this->currentClip,
        deltaTime,
        this->speed,
        this->looping,
        this->currentTime,
        this->playing
    );
    this->AccumulateRootMotion(
        this->ExtractRootMotion(*this->currentClip, advance)
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
        this->ApplyEvaluatedPose(this->sampledPose);
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
    this->ApplyEvaluatedPose(this->blendedPose);
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
    this->ResetRootMotion();
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

void Animator::SetRootMotionBone(BoneIndex boneIndex)
{
    if (static_cast<std::size_t>(boneIndex) >= this->pose->BoneCount())
        throw std::out_of_range("Root motion bone index is out of range");
    this->rootMotionBone = boneIndex;
    this->ResetRootMotion();
    if (this->rootMotionEnabled)
        this->Evaluate();
}

void Animator::ClearRootMotionBone()
{
    this->rootMotionEnabled = false;
    this->rootMotionBone.reset();
    this->ResetRootMotion();
    this->Evaluate();
}

std::optional<BoneIndex> Animator::RootMotionBone() const noexcept
{
    return this->rootMotionBone;
}

void Animator::SetRootMotionEnabled(bool enabled)
{
    if (enabled && !this->rootMotionBone.has_value())
    {
        throw std::logic_error(
            "Animator requires a root motion bone before enabling root motion"
        );
    }
    this->rootMotionEnabled = enabled;
    this->ResetRootMotion();
    this->Evaluate();
}

bool Animator::IsRootMotionEnabled() const noexcept
{
    return this->rootMotionEnabled;
}

RootMotionDelta Animator::ConsumeRootMotion() noexcept
{
    const RootMotionDelta result = this->pendingRootMotion;
    this->pendingRootMotion = {};
    return result;
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

Animator::PlaybackAdvance Animator::Advance(
    const AnimationClip& clip,
    float deltaTime,
    float playbackSpeed,
    bool playbackLooping,
    float& time,
    bool& remainsPlaying
) const
{
    PlaybackAdvance result;
    result.previousTime = time;
    const double nextTime = static_cast<double>(time) +
        static_cast<double>(deltaTime) *
        static_cast<double>(playbackSpeed);
    if (!std::isfinite(nextTime))
        throw std::overflow_error("Animator playback time overflowed");

    const double duration = static_cast<double>(clip.Duration());
    if (playbackLooping)
    {
        const double loopCount = std::floor(nextTime / duration);
        if (loopCount > static_cast<double>(
                std::numeric_limits<std::uint64_t>::max()
            ))
        {
            throw std::overflow_error("Animator loop count overflowed");
        }
        result.loopCount = static_cast<std::uint64_t>(loopCount);
        time = static_cast<float>(std::fmod(nextTime, duration));
        remainsPlaying = true;
    }
    else if (nextTime >= duration)
    {
        time = clip.Duration();
        remainsPlaying = false;
    }
    else
    {
        time = static_cast<float>(nextTime);
        remainsPlaying = true;
    }
    result.currentTime = time;
    return result;
}

RootMotionDelta Animator::ExtractRootMotion(
    const AnimationClip& clip,
    const PlaybackAdvance& advance
) const
{
    if (!this->rootMotionEnabled || !this->rootMotionBone.has_value())
        return {};

    const BoneIndex boneIndex = *this->rootMotionBone;
    const AnimationTrack* track = clip.FindTrack(boneIndex);
    if (track == nullptr)
        return {};

    const BoneTransform bindTransform = BoneTransform::FromMatrix(
        this->pose->GetSkeleton().BoneAt(boneIndex).bindLocalMatrix
    );
    const glm::mat4 inverseBind = glm::inverse(bindTransform.Matrix());
    const auto sampleMotion = [track, &bindTransform, &inverseBind](float time)
    {
        const BoneTransform sampled = track->Sample(time, bindTransform);
        const BoneTransform relative = BoneTransform::FromMatrix(
            inverseBind * sampled.Matrix()
        );
        return MotionMatrix(relative);
    };

    const glm::mat4 previous = sampleMotion(advance.previousTime);
    const glm::mat4 current = sampleMotion(advance.currentTime);
    glm::mat4 delta(1.0f);
    if (advance.loopCount == 0U)
    {
        delta = glm::inverse(previous) * current;
    }
    else
    {
        const glm::mat4 start = sampleMotion(0.0f);
        const glm::mat4 end = sampleMotion(clip.Duration());
        delta = glm::inverse(previous) * end;
        if (advance.loopCount > 1U)
        {
            delta *= MatrixPower(
                glm::inverse(start) * end,
                advance.loopCount - 1U
            );
        }
        delta *= glm::inverse(start) * current;
    }
    return RootMotionFromMatrix(delta);
}

void Animator::AccumulateRootMotion(const RootMotionDelta& delta)
{
    if (!this->rootMotionEnabled)
        return;
    this->pendingRootMotion = ComposeRootMotion(
        this->pendingRootMotion,
        delta
    );
}

void Animator::ApplyEvaluatedPose(const PoseBuffer& evaluatedPose)
{
    if (!this->rootMotionEnabled || !this->rootMotionBone.has_value())
    {
        evaluatedPose.ApplyTo(*this->pose);
        return;
    }

    this->outputPose = evaluatedPose;
    const BoneIndex boneIndex = *this->rootMotionBone;
    BoneTransform transform = this->outputPose.TransformAt(boneIndex);
    const BoneTransform bindTransform = BoneTransform::FromMatrix(
        this->pose->GetSkeleton().BoneAt(boneIndex).bindLocalMatrix
    );
    transform.translation = bindTransform.translation;
    transform.rotation = bindTransform.rotation;
    this->outputPose.SetTransform(boneIndex, transform);
    this->outputPose.ApplyTo(*this->pose);
}

void Animator::ResetRootMotion() noexcept
{
    this->pendingRootMotion = {};
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
