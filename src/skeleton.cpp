#include "pch.hpp"
#include "skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_set>
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
        if (bone.appendTransform.has_value())
        {
            const MmdAppendTransform& append = *bone.appendTransform;
            if (append.sourceBone == InvalidBoneIndex ||
                static_cast<std::size_t>(append.sourceBone) >=
                    this->bones.size() ||
                append.sourceBone == static_cast<BoneIndex>(index))
            {
                throw std::invalid_argument(
                    "MMD append transform references an invalid source bone"
                );
            }
            if (!std::isfinite(append.weight) ||
                (!append.affectRotation && !append.affectTranslation))
            {
                throw std::invalid_argument(
                    "MMD append transform parameters are invalid"
                );
            }
        }
        if (bone.ikConstraint.has_value())
        {
            const MmdIkConstraint& ik = *bone.ikConstraint;
            if (ik.targetBone == InvalidBoneIndex ||
                static_cast<std::size_t>(ik.targetBone) >= this->bones.size() ||
                ik.targetBone == static_cast<BoneIndex>(index) ||
                ik.iterations == 0U || !std::isfinite(ik.angleLimit) ||
                ik.angleLimit <= 0.0f || ik.links.empty())
            {
                throw std::invalid_argument("MMD IK constraint is invalid");
            }
            std::unordered_set<BoneIndex> linkBones;
            for (const MmdIkLink& link : ik.links)
            {
                if (link.bone == InvalidBoneIndex ||
                    static_cast<std::size_t>(link.bone) >= this->bones.size() ||
                    !linkBones.emplace(link.bone).second)
                {
                    throw std::invalid_argument("MMD IK link is invalid");
                }
                if (link.hasLimits)
                {
                    for (glm::length_t axis = 0; axis < 3; ++axis)
                    {
                        if (!std::isfinite(link.minimumAngle[axis]) ||
                            !std::isfinite(link.maximumAngle[axis]) ||
                            link.minimumAngle[axis] > link.maximumAngle[axis])
                        {
                            throw std::invalid_argument(
                                "MMD IK angle limits are invalid"
                            );
                        }
                    }
                }
            }
        }

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

    this->children.resize(this->bones.size());
    for (std::size_t index = 0; index < this->bones.size(); ++index)
    {
        const BoneIndex parentIndex = this->bones[index].parentIndex;
        if (parentIndex != InvalidBoneIndex)
        {
            this->children[parentIndex].push_back(
                static_cast<BoneIndex>(index)
            );
        }
    }

    for (std::size_t index = 0; index < this->bones.size(); ++index)
    {
        const Bone& bone = this->bones[index];
        if (bone.appendTransform.has_value() || bone.ikConstraint.has_value())
            this->mmdConstraintOrder.push_back(static_cast<BoneIndex>(index));
    }
    std::stable_sort(
        this->mmdConstraintOrder.begin(),
        this->mmdConstraintOrder.end(),
        [this](BoneIndex left, BoneIndex right)
        {
            const Bone& leftBone = this->bones[left];
            const Bone& rightBone = this->bones[right];
            if (leftBone.deformLayer != rightBone.deformLayer)
                return leftBone.deformLayer < rightBone.deformLayer;
            return leftBone.sourceOrder < rightBone.sourceOrder;
        }
    );

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

std::span<const BoneIndex> Skeleton::Children(BoneIndex boneIndex) const
{
    if (static_cast<std::size_t>(boneIndex) >= this->children.size())
        throw std::out_of_range("Bone index is out of range");
    return this->children[boneIndex];
}

std::span<const glm::mat4> Skeleton::BindGlobalMatrices() const noexcept
{
    return this->bindGlobalMatrices;
}

const glm::mat4& Skeleton::InverseRootMatrix() const noexcept
{
    return this->inverseRootMatrix;
}

bool Skeleton::HasMmdConstraints() const noexcept
{
    return !this->mmdConstraintOrder.empty();
}

std::span<const BoneIndex> Skeleton::MmdConstraintOrder() const noexcept
{
    return this->mmdConstraintOrder;
}
