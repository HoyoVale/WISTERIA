#include "pch.hpp"
#include "animation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
bool IsFinite(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsFinite(const glm::quat& value) noexcept
{
    return std::isfinite(value.w) && std::isfinite(value.x) &&
        std::isfinite(value.y) && std::isfinite(value.z);
}

void ValidateInterpolation(const KeyframeInterpolation& interpolation)
{
    if (interpolation.mode != AnimationInterpolation::Linear &&
        interpolation.mode != AnimationInterpolation::CubicBezier)
    {
        throw std::invalid_argument("Animation interpolation mode is invalid");
    }
    if (interpolation.mode == AnimationInterpolation::Linear)
        return;

    const glm::vec2 first = interpolation.controlPoint1;
    const glm::vec2 second = interpolation.controlPoint2;
    const auto validControlPoint = [](const glm::vec2& point)
    {
        return std::isfinite(point.x) && std::isfinite(point.y) &&
            point.x >= 0.0f && point.x <= 1.0f &&
            point.y >= 0.0f && point.y <= 1.0f;
    };
    if (!validControlPoint(first) || !validControlPoint(second))
    {
        throw std::invalid_argument(
            "Animation Bezier control points must be finite and normalized"
        );
    }
}

void ValidateVectorKeys(
    const std::vector<VectorKeyframe>& keys,
    const char* channelName
)
{
    float previousTime = -1.0f;
    for (const VectorKeyframe& key : keys)
    {
        if (!std::isfinite(key.time) || key.time < 0.0f ||
            key.time <= previousTime)
        {
            throw std::invalid_argument(
                std::string("Animation ") + channelName +
                " key times must be finite, non-negative and increasing"
            );
        }
        if (!IsFinite(key.value))
        {
            throw std::invalid_argument(
                std::string("Animation ") + channelName +
                " key values must be finite"
            );
        }
        for (const KeyframeInterpolation& interpolation : key.interpolation)
            ValidateInterpolation(interpolation);
        previousTime = key.time;
    }
}

void ValidateAndNormalizeQuaternionKeys(
    std::vector<QuaternionKeyframe>& keys
)
{
    float previousTime = -1.0f;
    for (QuaternionKeyframe& key : keys)
    {
        if (!std::isfinite(key.time) || key.time < 0.0f ||
            key.time <= previousTime)
        {
            throw std::invalid_argument(
                "Animation rotation key times must be finite, non-negative and increasing"
            );
        }
        const float length = glm::length(key.value);
        if (!IsFinite(key.value) || !std::isfinite(length) ||
            length <= 0.000001f)
        {
            throw std::invalid_argument(
                "Animation rotation keys must contain finite non-zero quaternions"
            );
        }
        key.value /= length;
        ValidateInterpolation(key.interpolation);
        previousTime = key.time;
    }
}

glm::vec3 SampleVectorKeys(
    const std::vector<VectorKeyframe>& keys,
    float time,
    const glm::vec3& fallback
)
{
    if (keys.empty())
        return fallback;
    if (keys.size() == 1 || time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;

    const auto upper = std::upper_bound(
        keys.begin(),
        keys.end(),
        time,
        [](float sampleTime, const VectorKeyframe& key)
        {
            return sampleTime < key.time;
        }
    );
    const VectorKeyframe& next = *upper;
    const VectorKeyframe& previous = *(upper - 1);
    const float normalizedTime =
        (time - previous.time) / (next.time - previous.time);
    glm::vec3 factor(0.0f);
    for (glm::length_t axis = 0; axis < 3; ++axis)
        factor[axis] = next.interpolation[axis].Evaluate(normalizedTime);
    return previous.value + (next.value - previous.value) * factor;
}

glm::quat SampleQuaternionKeys(
    const std::vector<QuaternionKeyframe>& keys,
    float time,
    const glm::quat& fallback
)
{
    if (keys.empty())
        return fallback;
    if (keys.size() == 1 || time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;

    const auto upper = std::upper_bound(
        keys.begin(),
        keys.end(),
        time,
        [](float sampleTime, const QuaternionKeyframe& key)
        {
            return sampleTime < key.time;
        }
    );
    const QuaternionKeyframe& next = *upper;
    const QuaternionKeyframe& previous = *(upper - 1);
    const float normalizedTime =
        (time - previous.time) / (next.time - previous.time);
    const float factor = next.interpolation.Evaluate(normalizedTime);
    return glm::normalize(glm::slerp(previous.value, next.value, factor));
}
}

float KeyframeInterpolation::Evaluate(float normalizedTime) const noexcept
{
    const float input = std::clamp(normalizedTime, 0.0f, 1.0f);
    if (this->mode == AnimationInterpolation::Linear)
        return input;

    const auto component = [](float first, float second, float parameter)
    {
        const float inverse = 1.0f - parameter;
        return 3.0f * inverse * inverse * parameter * first +
            3.0f * inverse * parameter * parameter * second +
            parameter * parameter * parameter;
    };

    // The curve is parameterized by X, but animation sampling starts with X
    // (normalized time). Find its Bezier parameter using bounded bisection.
    float lower = 0.0f;
    float upper = 1.0f;
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        const float middle = (lower + upper) * 0.5f;
        if (component(
                this->controlPoint1.x,
                this->controlPoint2.x,
                middle
            ) < input)
        {
            lower = middle;
        }
        else
        {
            upper = middle;
        }
    }
    const float parameter = (lower + upper) * 0.5f;
    return component(
        this->controlPoint1.y,
        this->controlPoint2.y,
        parameter
    );
}

AnimationTrack::AnimationTrack(
    BoneIndex boneIndex,
    std::vector<VectorKeyframe> translationKeys,
    std::vector<QuaternionKeyframe> rotationKeys,
    std::vector<VectorKeyframe> scaleKeys
)
    : boneIndex(boneIndex),
      translationKeys(std::move(translationKeys)),
      rotationKeys(std::move(rotationKeys)),
      scaleKeys(std::move(scaleKeys))
{
    if (this->boneIndex == InvalidBoneIndex)
        throw std::invalid_argument("Animation track bone index is invalid");
    if (this->translationKeys.empty() && this->rotationKeys.empty() &&
        this->scaleKeys.empty())
    {
        throw std::invalid_argument(
            "Animation track must contain at least one channel"
        );
    }

    ValidateVectorKeys(this->translationKeys, "translation");
    ValidateAndNormalizeQuaternionKeys(this->rotationKeys);
    ValidateVectorKeys(this->scaleKeys, "scale");

    if (!this->translationKeys.empty())
        this->endTime = this->translationKeys.back().time;
    if (!this->rotationKeys.empty())
        this->endTime = std::max(this->endTime, this->rotationKeys.back().time);
    if (!this->scaleKeys.empty())
        this->endTime = std::max(this->endTime, this->scaleKeys.back().time);
}

BoneIndex AnimationTrack::Bone() const noexcept
{
    return this->boneIndex;
}

std::span<const VectorKeyframe> AnimationTrack::TranslationKeys() const noexcept
{
    return this->translationKeys;
}

std::span<const QuaternionKeyframe> AnimationTrack::RotationKeys() const noexcept
{
    return this->rotationKeys;
}

std::span<const VectorKeyframe> AnimationTrack::ScaleKeys() const noexcept
{
    return this->scaleKeys;
}

float AnimationTrack::EndTime() const noexcept
{
    return this->endTime;
}

BoneTransform AnimationTrack::Sample(
    float time,
    const BoneTransform& fallback
) const
{
    if (!std::isfinite(time))
        throw std::invalid_argument("Animation sample time must be finite");

    BoneTransform result;
    result.translation = SampleVectorKeys(
        this->translationKeys,
        time,
        fallback.translation
    );
    result.rotation = SampleQuaternionKeys(
        this->rotationKeys,
        time,
        fallback.rotation
    );
    result.scale = SampleVectorKeys(
        this->scaleKeys,
        time,
        fallback.scale
    );
    return result;
}

AnimationClip::AnimationClip(
    std::string name,
    float durationSeconds,
    std::vector<AnimationTrack> tracks
)
    : name(std::move(name)),
      duration(durationSeconds),
      tracks(std::move(tracks))
{
    if (this->name.empty())
        throw std::invalid_argument("Animation clip name must not be empty");
    if (!std::isfinite(this->duration) || this->duration <= 0.0f)
    {
        throw std::invalid_argument(
            "Animation clip duration must be finite and positive"
        );
    }
    if (this->tracks.empty())
        throw std::invalid_argument("Animation clip must contain tracks");

    this->trackLookup.reserve(this->tracks.size());
    for (std::size_t index = 0; index < this->tracks.size(); ++index)
    {
        const AnimationTrack& track = this->tracks[index];
        if (track.EndTime() > this->duration + 0.0001f)
        {
            throw std::invalid_argument(
                "Animation track extends beyond clip duration"
            );
        }
        if (!this->trackLookup.emplace(track.Bone(), index).second)
        {
            throw std::invalid_argument(
                "Animation clip contains duplicate tracks for one bone"
            );
        }
    }
}

const std::string& AnimationClip::Name() const noexcept
{
    return this->name;
}

float AnimationClip::Duration() const noexcept
{
    return this->duration;
}

std::size_t AnimationClip::TrackCount() const noexcept
{
    return this->tracks.size();
}

std::span<const AnimationTrack> AnimationClip::Tracks() const noexcept
{
    return this->tracks;
}

const AnimationTrack* AnimationClip::FindTrack(BoneIndex boneIndex) const noexcept
{
    const auto iterator = this->trackLookup.find(boneIndex);
    return iterator == this->trackLookup.end()
        ? nullptr
        : &this->tracks[iterator->second];
}

void AnimationClip::Sample(float time, PoseBuffer& output) const
{
    if (!std::isfinite(time))
        throw std::invalid_argument("Animation clip sample time must be finite");

    const float clampedTime = std::clamp(time, 0.0f, this->duration);
    output.ResetToBindPose();
    for (const AnimationTrack& track : this->tracks)
    {
        if (static_cast<std::size_t>(track.Bone()) >= output.BoneCount())
        {
            throw std::invalid_argument(
                "Animation clip references a bone outside the PoseBuffer Skeleton"
            );
        }
        output.SetTransform(
            track.Bone(),
            track.Sample(
                clampedTime,
                output.TransformAt(track.Bone())
            )
        );
    }
}
