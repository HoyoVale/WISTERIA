#pragma once

#include "pose_buffer.hpp"
#include <cstdint>
#include <functional>
#include <vector>

class MmdPoseSolver
{
public:
    using IkEnabledPredicate = std::function<bool(BoneIndex)>;

    void Solve(
        PoseBuffer& pose,
        const IkEnabledPredicate& ikEnabled = {}
    );

private:
    void RecalculateAll(const Skeleton& skeleton);
    void RecalculateSubtree(const Skeleton& skeleton, BoneIndex rootBone);

    std::vector<BoneTransform> transforms;
    std::vector<glm::mat4> localMatrices;
    std::vector<glm::mat4> globalMatrices;
    std::vector<std::uint8_t> changed;
    std::vector<BoneIndex> traversalStack;
};
