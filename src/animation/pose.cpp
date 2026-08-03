#include "wisteria/common/pch.hpp"
#include "wisteria/animation/pose.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
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

BoneTransform BoneTransform::FromMatrix(const glm::mat4& matrix)
{
    if (!IsFinite(matrix))
        throw std::invalid_argument("Bone transform matrix must be finite");

    constexpr float epsilon = 0.000001f;
    glm::vec3 axisX(matrix[0]);
    glm::vec3 axisY(matrix[1]);
    glm::vec3 axisZ(matrix[2]);
    glm::vec3 extractedScale(
        glm::length(axisX),
        glm::length(axisY),
        glm::length(axisZ)
    );
    if (extractedScale.x <= epsilon || extractedScale.y <= epsilon ||
        extractedScale.z <= epsilon)
    {
        throw std::invalid_argument(
            "Bone transform matrix contains a zero scale axis"
        );
    }

    axisX /= extractedScale.x;
    axisY /= extractedScale.y;
    axisZ /= extractedScale.z;
    if (glm::determinant(glm::mat3(axisX, axisY, axisZ)) < 0.0f)
    {
        extractedScale.x = -extractedScale.x;
        axisX = -axisX;
    }

    constexpr float orthogonalTolerance = 0.001f;
    if (std::abs(glm::dot(axisX, axisY)) > orthogonalTolerance ||
        std::abs(glm::dot(axisX, axisZ)) > orthogonalTolerance ||
        std::abs(glm::dot(axisY, axisZ)) > orthogonalTolerance)
    {
        throw std::invalid_argument(
            "Bone transform matrix contains unsupported shear"
        );
    }

    BoneTransform result;
    result.translation = glm::vec3(matrix[3]);
    result.rotation = glm::normalize(
        glm::quat_cast(glm::mat3(axisX, axisY, axisZ))
    );
    result.scale = extractedScale;
    return result;
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

void Pose::SetLocalMatrices(std::span<const glm::mat4> matrices)
{
    if (matrices.size() != this->localMatrices.size())
    {
        throw std::invalid_argument(
            "Pose local matrix count must match the skeleton bone count"
        );
    }
    for (const glm::mat4& matrix : matrices)
    {
        if (!IsFinite(matrix))
            throw std::invalid_argument("Bone local matrix must be finite");
    }

    this->localMatrices.assign(matrices.begin(), matrices.end());
    this->dirty = true;
    ++this->revision;
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
