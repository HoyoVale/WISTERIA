#include "pch.hpp"
#include "skeleton.hpp"

#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

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
}

Skeleton::Skeleton(
    std::vector<Bone> bones,
    const glm::mat4& inverseRootMatrix
)
    : bones(std::move(bones)),
      inverseRootMatrix(inverseRootMatrix)
{
    if (!IsFinite(this->inverseRootMatrix))
        throw std::invalid_argument("Skeleton inverse root matrix must be finite");
    if (this->bones.empty())
        throw std::invalid_argument("Skeleton must contain at least one bone");
    if (this->bones.size() >= static_cast<std::size_t>(InvalidBoneIndex))
        throw std::length_error("Skeleton contains too many bones");

    this->boneIndices.reserve(this->bones.size());
    for (std::size_t index = 0; index < this->bones.size(); ++index)
    {
        const Bone& bone = this->bones[index];
        if (bone.name.empty())
            throw std::invalid_argument("Bone name must not be empty");
        if (!IsFinite(bone.bindLocalMatrix) ||
            !IsFinite(bone.inverseBindMatrix))
        {
            throw std::invalid_argument("Bone matrices must be finite");
        }
        if (bone.parentIndex != InvalidBoneIndex &&
            static_cast<std::size_t>(bone.parentIndex) >= this->bones.size())
        {
            throw std::invalid_argument("Bone parent index is out of range");
        }
        if (bone.parentIndex == static_cast<BoneIndex>(index))
            throw std::invalid_argument("Bone cannot be its own parent");

        const bool inserted = this->boneIndices.emplace(
            bone.name,
            static_cast<BoneIndex>(index)
        ).second;
        if (!inserted)
            throw std::invalid_argument("Bone names must be unique");
        if (bone.parentIndex == InvalidBoneIndex)
            ++this->rootCount;
    }

    std::vector<std::uint8_t> visitState(this->bones.size(), 0U);
    this->evaluationOrder.reserve(this->bones.size());
    const std::function<void(BoneIndex)> visit =
        [&](BoneIndex boneIndex)
    {
        const std::size_t index = static_cast<std::size_t>(boneIndex);
        if (visitState[index] == 2U)
            return;
        if (visitState[index] == 1U)
            throw std::invalid_argument("Skeleton hierarchy contains a cycle");

        visitState[index] = 1U;
        const BoneIndex parentIndex = this->bones[index].parentIndex;
        if (parentIndex != InvalidBoneIndex)
            visit(parentIndex);
        visitState[index] = 2U;
        this->evaluationOrder.push_back(boneIndex);
    };

    for (std::size_t index = 0; index < this->bones.size(); ++index)
        visit(static_cast<BoneIndex>(index));

    if (this->rootCount == 0)
        throw std::invalid_argument("Skeleton must contain a root bone");

    this->bindGlobalMatrices.resize(this->bones.size(), glm::mat4(1.0f));
    for (BoneIndex boneIndex : this->evaluationOrder)
    {
        const Bone& bone = this->bones[boneIndex];
        this->bindGlobalMatrices[boneIndex] =
            bone.parentIndex == InvalidBoneIndex
                ? bone.bindLocalMatrix
                : this->bindGlobalMatrices[bone.parentIndex] *
                    bone.bindLocalMatrix;
    }
}

std::size_t Skeleton::BoneCount() const noexcept
{
    return this->bones.size();
}

std::size_t Skeleton::RootCount() const noexcept
{
    return this->rootCount;
}

const Bone& Skeleton::BoneAt(BoneIndex index) const
{
    if (static_cast<std::size_t>(index) >= this->bones.size())
        throw std::out_of_range("Bone index is out of range");
    return this->bones[index];
}

std::span<const Bone> Skeleton::Bones() const noexcept
{
    return this->bones;
}

std::optional<BoneIndex> Skeleton::FindBone(
    std::string_view name
) const
{
    const auto iterator = this->boneIndices.find(std::string(name));
    if (iterator == this->boneIndices.end())
        return std::nullopt;
    return iterator->second;
}

std::span<const BoneIndex> Skeleton::EvaluationOrder() const noexcept
{
    return this->evaluationOrder;
}

std::span<const glm::mat4> Skeleton::BindGlobalMatrices() const noexcept
{
    return this->bindGlobalMatrices;
}

const glm::mat4& Skeleton::InverseRootMatrix() const noexcept
{
    return this->inverseRootMatrix;
}
