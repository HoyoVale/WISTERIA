#include "wisteria/common/pch.hpp"
#include "wisteria/animation/pose_buffer.hpp"

#include <cmath>
#include <stdexcept>

namespace wisteria
{
PoseBuffer::PoseBuffer(const Skeleton& skeleton)
    : skeleton(&skeleton)
{
    this->bindTransforms.reserve(skeleton.BoneCount());
    this->transforms.reserve(skeleton.BoneCount());
    this->localMatrices.reserve(skeleton.BoneCount());
    for (std::size_t index = 0; index < skeleton.BoneCount(); ++index)
    {
        const glm::mat4& bindMatrix =
            skeleton.BoneAt(static_cast<BoneIndex>(index)).bindLocalMatrix;
        const BoneTransform bindTransform =
            BoneTransform::FromMatrix(bindMatrix);
        this->bindTransforms.push_back(bindTransform);
        this->transforms.push_back(bindTransform);
        // Keep the original matrix exactly for unanimated bones instead of
        // decomposing and recomposing it every frame.
        this->localMatrices.push_back(bindMatrix);
    }
}

const Skeleton& PoseBuffer::GetSkeleton() const noexcept
{
    return *this->skeleton;
}

std::size_t PoseBuffer::BoneCount() const noexcept
{
    return this->transforms.size();
}

void PoseBuffer::ResetToBindPose()
{
    this->transforms = this->bindTransforms;
    for (std::size_t index = 0; index < this->localMatrices.size(); ++index)
    {
        this->localMatrices[index] =
            this->skeleton->BoneAt(static_cast<BoneIndex>(index))
                .bindLocalMatrix;
    }
}

void PoseBuffer::CaptureFrom(const Pose& pose)
{
    if (&pose.GetSkeleton() != this->skeleton)
    {
        throw std::invalid_argument(
            "PoseBuffer and Pose must reference the same Skeleton"
        );
    }
    const std::span<const glm::mat4> matrices = pose.LocalMatrices();
    for (std::size_t index = 0; index < matrices.size(); ++index)
    {
        this->localMatrices[index] = matrices[index];
        this->transforms[index] = BoneTransform::FromMatrix(matrices[index]);
    }
}

const BoneTransform& PoseBuffer::TransformAt(BoneIndex boneIndex) const
{
    return this->transforms[this->CheckedIndex(boneIndex)];
}

std::span<const BoneTransform> PoseBuffer::BindTransforms() const noexcept
{
    return this->bindTransforms;
}

void PoseBuffer::SetTransform(
    BoneIndex boneIndex,
    const BoneTransform& transform
)
{
    const std::size_t index = this->CheckedIndex(boneIndex);
    const glm::mat4 matrix = transform.Matrix();
    this->transforms[index] = transform;
    this->localMatrices[index] = matrix;
}

std::span<const BoneTransform> PoseBuffer::Transforms() const noexcept
{
    return this->transforms;
}

std::span<const glm::mat4> PoseBuffer::LocalMatrices() const noexcept
{
    return this->localMatrices;
}

void PoseBuffer::ApplyTo(Pose& pose) const
{
    if (&pose.GetSkeleton() != this->skeleton)
    {
        throw std::invalid_argument(
            "PoseBuffer and Pose must reference the same Skeleton"
        );
    }
    pose.SetLocalMatrices(this->localMatrices);
}

std::size_t PoseBuffer::CheckedIndex(BoneIndex boneIndex) const
{
    const std::size_t index = static_cast<std::size_t>(boneIndex);
    if (index >= this->transforms.size())
        throw std::out_of_range("PoseBuffer bone index is out of range");
    return index;
}

void BlendPoseBuffers(
    const PoseBuffer& source,
    const PoseBuffer& destination,
    float weight,
    PoseBuffer& output
)
{
    if (&source.GetSkeleton() != &destination.GetSkeleton() ||
        &source.GetSkeleton() != &output.GetSkeleton())
    {
        throw std::invalid_argument(
            "Blended PoseBuffers must reference the same Skeleton"
        );
    }
    if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
    {
        throw std::invalid_argument(
            "PoseBuffer blend weight must be finite and between zero and one"
        );
    }

    for (std::size_t index = 0; index < source.BoneCount(); ++index)
    {
        const BoneIndex boneIndex = static_cast<BoneIndex>(index);
        const BoneTransform sourceTransform = source.TransformAt(boneIndex);
        const BoneTransform destinationTransform =
            destination.TransformAt(boneIndex);

        BoneTransform blended;
        blended.translation = glm::mix(
            sourceTransform.translation,
            destinationTransform.translation,
            weight
        );
        blended.rotation = glm::normalize(glm::slerp(
            sourceTransform.rotation,
            destinationTransform.rotation,
            weight
        ));
        blended.scale = glm::mix(
            sourceTransform.scale,
            destinationTransform.scale,
            weight
        );
        output.SetTransform(boneIndex, blended);
    }
}
}  // namespace wisteria
