#include "wisteria/common/pch.hpp"
#include "wisteria/animation/animation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
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

CameraTrack::CameraTrack(std::vector<CameraKeyframe> nextKeys)
    : keys(std::move(nextKeys))
{
    for (const CameraKeyframe& key : this->keys)
        this->endTime = std::max(this->endTime, key.time);
}

std::span<const CameraKeyframe> CameraTrack::Keys() const noexcept
{
    return this->keys;
}

float CameraTrack::EndTime() const noexcept
{
    return this->endTime;
}

bool CameraTrack::Sample(float time, CameraKeyframe& output) const
{
    if (this->keys.empty())
        return false;
    if (time <= this->keys.front().time)
    {
        output = this->keys.front();
        return true;
    }
    if (time >= this->keys.back().time)
    {
        output = this->keys.back();
        return true;
    }

    const auto upper = std::upper_bound(
        this->keys.begin(),
        this->keys.end(),
        time,
        [](float sampleTime, const CameraKeyframe& key)
        {
            return sampleTime < key.time;
        }
    );
    const CameraKeyframe& next = *upper;
    const CameraKeyframe& previous = *(upper - 1);
    const float normalizedTime =
        (time - previous.time) / (next.time - previous.time);
    std::array<float, 4> factors{};
    for (std::size_t channel = 0U; channel < factors.size(); ++channel)
    {
        factors[channel] = next.interpolation[channel].Evaluate(
            normalizedTime
        );
    }

    output.time = time;
    output.interest = glm::mix(
        previous.interest,
        next.interest,
        glm::vec3(factors[0], factors[1], factors[2])
    );
    // The track stores four curves (interest X/Y/Z + distance); rotation,
    // view angle and the perspective flag use linear/step semantics.
    output.rotation = glm::mix(
        previous.rotation,
        next.rotation,
        normalizedTime
    );
    output.distance = glm::mix(
        previous.distance,
        next.distance,
        factors[3]
    );
    output.viewAngle = glm::mix(
        previous.viewAngle,
        next.viewAngle,
        normalizedTime
    );
    output.perspective = next.perspective;
    output.interpolation = next.interpolation;
    return true;
}

LightTrack::LightTrack(std::vector<LightKeyframe> nextKeys)
    : keys(std::move(nextKeys))
{
    for (const LightKeyframe& key : this->keys)
        this->endTime = std::max(this->endTime, key.time);
}

std::span<const LightKeyframe> LightTrack::Keys() const noexcept
{
    return this->keys;
}

float LightTrack::EndTime() const noexcept
{
    return this->endTime;
}

bool LightTrack::Sample(float time, LightKeyframe& output) const
{
    if (this->keys.empty())
        return false;
    if (time <= this->keys.front().time)
    {
        output = this->keys.front();
        return true;
    }
    if (time >= this->keys.back().time)
    {
        output = this->keys.back();
        return true;
    }

    const auto upper = std::upper_bound(
        this->keys.begin(),
        this->keys.end(),
        time,
        [](float sampleTime, const LightKeyframe& key)
        {
            return sampleTime < key.time;
        }
    );
    const LightKeyframe& next = *upper;
    const LightKeyframe& previous = *(upper - 1);
    const float normalizedTime =
        (time - previous.time) / (next.time - previous.time);
    std::array<float, 6> factors{};
    for (std::size_t channel = 0U; channel < factors.size(); ++channel)
    {
        factors[channel] = next.interpolation[channel].Evaluate(
            normalizedTime
        );
    }

    output.time = time;
    output.color = glm::mix(
        previous.color,
        next.color,
        glm::vec3(factors[0], factors[1], factors[2])
    );
    output.position = glm::mix(
        previous.position,
        next.position,
        glm::vec3(factors[3], factors[4], factors[5])
    );
    output.interpolation = next.interpolation;
    return true;
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

MmdIkStateTrack::MmdIkStateTrack(
    BoneIndex controllerBone,
    std::vector<BoolKeyframe> keys
)
    : controllerBone(controllerBone),
      keys(std::move(keys))
{
    if (this->controllerBone == InvalidBoneIndex)
        throw std::invalid_argument("MMD IK state track bone index is invalid");
    if (this->keys.empty())
        throw std::invalid_argument("MMD IK state track must contain keys");

    float previousTime = -1.0f;
    for (const BoolKeyframe& key : this->keys)
    {
        if (!std::isfinite(key.time) || key.time < 0.0f ||
            key.time <= previousTime)
        {
            throw std::invalid_argument(
                "MMD IK state key times must be finite, non-negative and increasing"
            );
        }
        previousTime = key.time;
    }
}

MorphWeightTrack::MorphWeightTrack(
    MorphIndex morphIndex,
    std::vector<FloatKeyframe> keys
)
    : morphIndex(morphIndex),
      keys(std::move(keys))
{
    if (this->morphIndex == InvalidMorphIndex)
        throw std::invalid_argument("Morph weight track index is invalid");
    if (this->keys.empty())
        throw std::invalid_argument("Morph weight track must contain keys");
    float previousTime = -1.0f;
    for (const FloatKeyframe& key : this->keys)
    {
        if (!std::isfinite(key.time) || key.time < 0.0f ||
            key.time <= previousTime || !std::isfinite(key.value))
        {
            throw std::invalid_argument(
                "Morph weight keys must be finite with increasing non-negative times"
            );
        }
        previousTime = key.time;
    }
}

MorphIndex MorphWeightTrack::Morph() const noexcept
{
    return this->morphIndex;
}

std::span<const FloatKeyframe> MorphWeightTrack::Keys() const noexcept
{
    return this->keys;
}

float MorphWeightTrack::EndTime() const noexcept
{
    return this->keys.back().time;
}

float MorphWeightTrack::Sample(float time, float fallback) const
{
    if (!std::isfinite(time) || !std::isfinite(fallback))
        throw std::invalid_argument("Morph weight sample values must be finite");
    if (time < this->keys.front().time)
        return fallback;
    if (this->keys.size() == 1U || time >= this->keys.back().time)
        return this->keys.back().value;
    const auto upper = std::upper_bound(
        this->keys.begin(),
        this->keys.end(),
        time,
        [](float sampleTime, const FloatKeyframe& key)
        {
            return sampleTime < key.time;
        }
    );
    const FloatKeyframe& next = *upper;
    const FloatKeyframe& previous = *(upper - 1);
    const float factor = (time - previous.time) /
        (next.time - previous.time);
    return previous.value + (next.value - previous.value) * factor;
}

BoneIndex MmdIkStateTrack::ControllerBone() const noexcept
{
    return this->controllerBone;
}

std::span<const BoolKeyframe> MmdIkStateTrack::Keys() const noexcept
{
    return this->keys;
}

float MmdIkStateTrack::EndTime() const noexcept
{
    return this->keys.back().time;
}

bool MmdIkStateTrack::Sample(float time, bool fallback) const
{
    if (!std::isfinite(time))
        throw std::invalid_argument("MMD IK state sample time must be finite");
    if (time < this->keys.front().time)
        return fallback;

    const auto upper = std::upper_bound(
        this->keys.begin(),
        this->keys.end(),
        time,
        [](float sampleTime, const BoolKeyframe& key)
        {
            return sampleTime < key.time;
        }
    );
    return (upper - 1)->value;
}

AnimationClip::AnimationClip(
    std::string name,
    float durationSeconds,
    std::vector<AnimationTrack> tracks,
    std::vector<MmdIkStateTrack> mmdIkStateTracks,
    std::vector<MorphWeightTrack> morphWeightTracks
)
    : name(std::move(name)),
      duration(durationSeconds),
      tracks(std::move(tracks)),
      mmdIkStateTracks(std::move(mmdIkStateTracks)),
      morphWeightTracks(std::move(morphWeightTracks))
{
    if (this->name.empty())
        throw std::invalid_argument("Animation clip name must not be empty");
    if (!std::isfinite(this->duration) || this->duration <= 0.0f)
    {
        throw std::invalid_argument(
            "Animation clip duration must be finite and positive"
        );
    }
    if (this->tracks.empty() && this->mmdIkStateTracks.empty() &&
        this->morphWeightTracks.empty())
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

    this->morphWeightTrackLookup.reserve(this->morphWeightTracks.size());
    for (std::size_t index = 0; index < this->morphWeightTracks.size(); ++index)
    {
        const MorphWeightTrack& track = this->morphWeightTracks[index];
        if (track.EndTime() > this->duration + 0.0001f)
        {
            throw std::invalid_argument(
                "Morph weight track extends beyond clip duration"
            );
        }
        if (!this->morphWeightTrackLookup.emplace(track.Morph(), index).second)
        {
            throw std::invalid_argument(
                "Animation clip contains duplicate tracks for one morph"
            );
        }
    }

    this->mmdIkStateTrackLookup.reserve(this->mmdIkStateTracks.size());
    for (std::size_t index = 0; index < this->mmdIkStateTracks.size(); ++index)
    {
        const MmdIkStateTrack& track = this->mmdIkStateTracks[index];
        if (track.EndTime() > this->duration + 0.0001f)
        {
            throw std::invalid_argument(
                "MMD IK state track extends beyond clip duration"
            );
        }
        if (!this->mmdIkStateTrackLookup.emplace(
                track.ControllerBone(),
                index
            ).second)
        {
            throw std::invalid_argument(
                "Animation clip contains duplicate MMD IK state tracks"
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

std::size_t AnimationClip::MmdIkStateTrackCount() const noexcept
{
    return this->mmdIkStateTracks.size();
}

std::span<const MmdIkStateTrack> AnimationClip::MmdIkStateTracks() const noexcept
{
    return this->mmdIkStateTracks;
}

const MmdIkStateTrack* AnimationClip::FindMmdIkStateTrack(
    BoneIndex controllerBone
) const noexcept
{
    const auto iterator = this->mmdIkStateTrackLookup.find(controllerBone);
    return iterator == this->mmdIkStateTrackLookup.end()
        ? nullptr
        : &this->mmdIkStateTracks[iterator->second];
}

bool AnimationClip::SampleMmdIkState(
    BoneIndex controllerBone,
    float time,
    bool fallback
) const
{
    if (!std::isfinite(time))
        throw std::invalid_argument("MMD IK state sample time must be finite");
    const MmdIkStateTrack* track = this->FindMmdIkStateTrack(controllerBone);
    return track == nullptr
        ? fallback
        : track->Sample(std::clamp(time, 0.0f, this->duration), fallback);
}

std::size_t AnimationClip::MorphWeightTrackCount() const noexcept
{
    return this->morphWeightTracks.size();
}

std::span<const MorphWeightTrack> AnimationClip::MorphWeightTracks() const noexcept
{
    return this->morphWeightTracks;
}

const MorphWeightTrack* AnimationClip::FindMorphWeightTrack(
    MorphIndex morphIndex
) const noexcept
{
    const auto iterator = this->morphWeightTrackLookup.find(morphIndex);
    return iterator == this->morphWeightTrackLookup.end()
        ? nullptr
        : &this->morphWeightTracks[iterator->second];
}

void AnimationClip::SampleMorphWeights(
    float time,
    std::span<float> output
) const
{
    if (!std::isfinite(time))
        throw std::invalid_argument("Morph weight sample time must be finite");
    std::fill(output.begin(), output.end(), 0.0f);
    const float clampedTime = std::clamp(time, 0.0f, this->duration);
    for (const MorphWeightTrack& track : this->morphWeightTracks)
    {
        if (static_cast<std::size_t>(track.Morph()) >= output.size())
        {
            throw std::invalid_argument(
                "Animation clip references a morph outside its MorphState"
            );
        }
        output[track.Morph()] = track.Sample(clampedTime);
    }
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
