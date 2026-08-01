#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <limits>
#include <string>

using BoneIndex = std::uint32_t;

inline constexpr BoneIndex InvalidBoneIndex =
    std::numeric_limits<BoneIndex>::max();

// Immutable shared bind-pose data for one bone. Runtime animation state lives
// in Pose rather than here so multiple model instances can share a Skeleton.
struct Bone
{
    std::string name;
    BoneIndex parentIndex = InvalidBoneIndex;
    glm::mat4 bindLocalMatrix{1.0f};
    glm::mat4 inverseBindMatrix{1.0f};
};
