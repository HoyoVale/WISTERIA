#pragma once

#include "pose_buffer.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct VectorKeyframe
{
    float time = 0.0f;
    glm::vec3 value{0.0f};
};

struct QuaternionKeyframe
{
    float time = 0.0f;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

// A single bone's translation, rotation and scale channels. Keyframe times
// are measured in seconds.
class AnimationTrack
{
public:
    AnimationTrack(
        BoneIndex boneIndex,
        std::vector<VectorKeyframe> translationKeys = {},
        std::vector<QuaternionKeyframe> rotationKeys = {},
        std::vector<VectorKeyframe> scaleKeys = {}
    );

    BoneIndex Bone() const noexcept;
    std::span<const VectorKeyframe> TranslationKeys() const noexcept;
    std::span<const QuaternionKeyframe> RotationKeys() const noexcept;
    std::span<const VectorKeyframe> ScaleKeys() const noexcept;
    float EndTime() const noexcept;

    BoneTransform Sample(
        float time,
        const BoneTransform& fallback
    ) const;

private:
    BoneIndex boneIndex = InvalidBoneIndex;
    std::vector<VectorKeyframe> translationKeys;
    std::vector<QuaternionKeyframe> rotationKeys;
    std::vector<VectorKeyframe> scaleKeys;
    float endTime = 0.0f;
};

// Shared immutable animation data owned by ModelAsset.
class AnimationClip
{
public:
    AnimationClip(
        std::string name,
        float durationSeconds,
        std::vector<AnimationTrack> tracks
    );

    const std::string& Name() const noexcept;
    float Duration() const noexcept;
    std::size_t TrackCount() const noexcept;
    std::span<const AnimationTrack> Tracks() const noexcept;
    const AnimationTrack* FindTrack(BoneIndex boneIndex) const noexcept;
    void Sample(float time, PoseBuffer& output) const;

private:
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationTrack> tracks;
    std::unordered_map<BoneIndex, std::size_t> trackLookup;
};
