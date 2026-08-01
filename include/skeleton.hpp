#pragma once

#include "bone.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

// Shared, model-space skeleton definition. Skeleton validates the hierarchy
// once and stores a parent-before-child order used by every Pose instance.
class Skeleton
{
public:
    explicit Skeleton(
        std::vector<Bone> bones,
        const glm::mat4& inverseRootMatrix = glm::mat4(1.0f)
    );

    std::size_t BoneCount() const noexcept;
    std::size_t RootCount() const noexcept;

    const Bone& BoneAt(BoneIndex index) const;
    std::span<const Bone> Bones() const noexcept;
    std::optional<BoneIndex> FindBone(std::string_view name) const;

    std::span<const BoneIndex> EvaluationOrder() const noexcept;
    std::span<const glm::mat4> BindGlobalMatrices() const noexcept;
    const glm::mat4& InverseRootMatrix() const noexcept;

private:
    std::vector<Bone> bones;
    std::unordered_map<std::string, BoneIndex> boneIndices;
    std::vector<BoneIndex> evaluationOrder;
    std::vector<glm::mat4> bindGlobalMatrices;
    glm::mat4 inverseRootMatrix{1.0f};
    std::size_t rootCount = 0;
};
