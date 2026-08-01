#include "pch.hpp"
#include "pose.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <stdexcept>

namespace
{
bool IsFinite(const glm::mat4& matrix) noexcept
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrix[column][row]))
                return false;
        }
    }
    return true;
}

bool IsFinite(const glm::vec3& vector) noexcept
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) &&
        std::isfinite(vector.z);
}

bool IsFinite(const glm::quat& quaternion) noexcept
{
    return std::isfinite(quaternion.w) && std::isfinite(quaternion.x) &&
        std::isfinite(quaternion.y) && std::isfinite(quaternion.z);
}
}

glm::mat4 BoneTransform::Matrix() const
{
    if (!IsFinite(this->translation) || !IsFinite(this->rotation) ||
        !IsFinite(this->scale))
    {
        throw std::invalid_argument("Bone transform must be finite");
    }

    const float rotationLength = glm::length(this->rotation);
    if (!std::isfinite(rotationLength) || rotationLength <= 0.000001f)
        throw std::invalid_argument("Bone rotation quaternion must be non-zero");

    return glm::translate(glm::mat4(1.0f), this->translation) *
        glm::mat4_cast(this->rotation / rotationLength) *
        glm::scale(glm::mat4(1.0f), this->scale);
}

Pose::Pose(const Skeleton& skeleton)
    : skeleton(&skeleton),
      localMatrices(skeleton.BoneCount(), glm::mat4(1.0f)),
      globalMatrices(skeleton.BoneCount(), glm::mat4(1.0f)),
      skinningMatrices(skeleton.BoneCount(), glm::mat4(1.0f))
{
    this->ResetToBindPose();
}

const Skeleton& Pose::GetSkeleton() const noexcept
{
    return *this->skeleton;
}

std::size_t Pose::BoneCount() const noexcept
{
    return this->localMatrices.size();
}

void Pose::ResetToBindPose()
{
    for (std::size_t index = 0; index < this->localMatrices.size(); ++index)
    {
        this->localMatrices[index] =
            this->skeleton->BoneAt(static_cast<BoneIndex>(index))
                .bindLocalMatrix;
    }
    this->dirty = true;
    ++this->revision;
}

void Pose::SetLocalMatrix(BoneIndex boneIndex, const glm::mat4& matrix)
{
    if (!IsFinite(matrix))
        throw std::invalid_argument("Bone local matrix must be finite");
    this->localMatrices[this->CheckedIndex(boneIndex)] = matrix;
    this->dirty = true;
    ++this->revision;
}

void Pose::SetLocalTransform(
    BoneIndex boneIndex,
    const BoneTransform& transform
)
{
    this->SetLocalMatrix(boneIndex, transform.Matrix());
}

const glm::mat4& Pose::LocalMatrix(BoneIndex boneIndex) const
{
    return this->localMatrices[this->CheckedIndex(boneIndex)];
}

std::span<const glm::mat4> Pose::LocalMatrices() const noexcept
{
    return this->localMatrices;
}

const glm::mat4& Pose::GlobalMatrix(BoneIndex boneIndex) const
{
    const std::size_t index = this->CheckedIndex(boneIndex);
    this->Recalculate();
    return this->globalMatrices[index];
}

std::span<const glm::mat4> Pose::GlobalMatrices() const
{
    this->Recalculate();
    return this->globalMatrices;
}

std::span<const glm::mat4> Pose::SkinningMatrices() const
{
    this->Recalculate();
    return this->skinningMatrices;
}

bool Pose::IsDirty() const noexcept
{
    return this->dirty;
}

std::uint64_t Pose::Revision() const noexcept
{
    return this->revision;
}

void Pose::Recalculate() const
{
    if (!this->dirty)
        return;

    for (BoneIndex boneIndex : this->skeleton->EvaluationOrder())
    {
        const Bone& bone = this->skeleton->BoneAt(boneIndex);
        this->globalMatrices[boneIndex] =
            bone.parentIndex == InvalidBoneIndex
                ? this->localMatrices[boneIndex]
                : this->globalMatrices[bone.parentIndex] *
                    this->localMatrices[boneIndex];
        this->skinningMatrices[boneIndex] =
            this->skeleton->InverseRootMatrix() *
            this->globalMatrices[boneIndex] * bone.inverseBindMatrix;
    }
    this->dirty = false;
}

std::size_t Pose::CheckedIndex(BoneIndex boneIndex) const
{
    const std::size_t index = static_cast<std::size_t>(boneIndex);
    if (index >= this->localMatrices.size())
        throw std::out_of_range("Pose bone index is out of range");
    return index;
}
