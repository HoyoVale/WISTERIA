#pragma once

#include "pose_buffer.hpp"
#include "morph.hpp"
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

enum class AnimationInterpolation
{
    Linear,
    CubicBezier
};

// Maps normalized time to normalized channel progress. VMD stores one curve
// per translation axis and one for rotation on the destination keyframe.
struct KeyframeInterpolation
{
    AnimationInterpolation mode = AnimationInterpolation::Linear;
    glm::vec2 controlPoint1{0.0f, 0.0f};
    glm::vec2 controlPoint2{1.0f, 1.0f};

    float Evaluate(float normalizedTime) const noexcept;
};

struct VectorKeyframe
{
    float time = 0.0f;
    glm::vec3 value{0.0f};
    std::array<KeyframeInterpolation, 3> interpolation{};
};

struct QuaternionKeyframe
{
    float time = 0.0f;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
    KeyframeInterpolation interpolation{};
};

struct BoolKeyframe
{
    float time = 0.0f;
    bool value = true;
};

struct FloatKeyframe
{
    float time = 0.0f;
    float value = 0.0f;
};

class MorphWeightTrack
{
public:
    MorphWeightTrack(MorphIndex morphIndex, std::vector<FloatKeyframe> keys);

    MorphIndex Morph() const noexcept;
    std::span<const FloatKeyframe> Keys() const noexcept;
    float EndTime() const noexcept;
    float Sample(float time, float fallback = 0.0f) const;

private:
    MorphIndex morphIndex = InvalidMorphIndex;
    std::vector<FloatKeyframe> keys;
};

// VMD IK switches are discrete state changes rather than interpolated bone
// transforms. Before the first keyframe, MMD IK is enabled by default.
class MmdIkStateTrack
{
public:
    MmdIkStateTrack(
        BoneIndex controllerBone,
        std::vector<BoolKeyframe> keys
    );

    BoneIndex ControllerBone() const noexcept;
    std::span<const BoolKeyframe> Keys() const noexcept;
    float EndTime() const noexcept;
    bool Sample(float time, bool fallback = true) const;

private:
    BoneIndex controllerBone = InvalidBoneIndex;
    std::vector<BoolKeyframe> keys;
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
        std::vector<AnimationTrack> tracks,
        std::vector<MmdIkStateTrack> mmdIkStateTracks = {},
        std::vector<MorphWeightTrack> morphWeightTracks = {}
    );

    const std::string& Name() const noexcept;
    float Duration() const noexcept;
    std::size_t TrackCount() const noexcept;
    std::span<const AnimationTrack> Tracks() const noexcept;
    const AnimationTrack* FindTrack(BoneIndex boneIndex) const noexcept;
    std::size_t MmdIkStateTrackCount() const noexcept;
    std::span<const MmdIkStateTrack> MmdIkStateTracks() const noexcept;
    const MmdIkStateTrack* FindMmdIkStateTrack(
        BoneIndex controllerBone
    ) const noexcept;
    bool SampleMmdIkState(
        BoneIndex controllerBone,
        float time,
        bool fallback = true
    ) const;
    std::size_t MorphWeightTrackCount() const noexcept;
    std::span<const MorphWeightTrack> MorphWeightTracks() const noexcept;
    const MorphWeightTrack* FindMorphWeightTrack(
        MorphIndex morphIndex
    ) const noexcept;
    void SampleMorphWeights(float time, std::span<float> output) const;
    void Sample(float time, PoseBuffer& output) const;

private:
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationTrack> tracks;
    std::vector<MmdIkStateTrack> mmdIkStateTracks;
    std::vector<MorphWeightTrack> morphWeightTracks;
    std::unordered_map<BoneIndex, std::size_t> trackLookup;
    std::unordered_map<BoneIndex, std::size_t> mmdIkStateTrackLookup;
    std::unordered_map<MorphIndex, std::size_t> morphWeightTrackLookup;
};
