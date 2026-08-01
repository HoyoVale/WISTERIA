#pragma once

#include "pose.hpp"
#include <cstddef>
#include <span>
#include <vector>

// Temporary local-space skeleton pose used by animation sampling and blending.
// Unlike Pose, it does not evaluate hierarchy/global/skinning matrices.
class PoseBuffer
{
public:
    explicit PoseBuffer(const Skeleton& skeleton);

    const Skeleton& GetSkeleton() const noexcept;
    std::size_t BoneCount() const noexcept;

    void ResetToBindPose();
    const BoneTransform& TransformAt(BoneIndex boneIndex) const;
    std::span<const BoneTransform> BindTransforms() const noexcept;
    void SetTransform(BoneIndex boneIndex, const BoneTransform& transform);

    std::span<const BoneTransform> Transforms() const noexcept;
    std::span<const glm::mat4> LocalMatrices() const noexcept;
    void ApplyTo(Pose& pose) const;

private:
    std::size_t CheckedIndex(BoneIndex boneIndex) const;

    const Skeleton* skeleton = nullptr;
    std::vector<BoneTransform> bindTransforms;
    std::vector<BoneTransform> transforms;
    std::vector<glm::mat4> localMatrices;
};

// Component-wise local-pose blend. Translation and scale use linear
// interpolation; rotation uses normalized quaternion slerp.
void BlendPoseBuffers(
    const PoseBuffer& source,
    const PoseBuffer& destination,
    float weight,
    PoseBuffer& output
);
