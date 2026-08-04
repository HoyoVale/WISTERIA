#pragma once

#include "wisteria/animation/pose_buffer.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace wisteria
{
enum class MmdPosePhase : std::uint8_t
{
    BeforePhysics,
    AfterPhysics
};

class MmdPoseSolver
{
public:
    using IkEnabledPredicate = std::function<bool(BoneIndex)>;

    void Solve(
        PoseBuffer& pose,
        MmdPosePhase phase,
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
}  // namespace wisteria
