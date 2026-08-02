#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using BoneIndex = std::uint32_t;

inline constexpr BoneIndex InvalidBoneIndex =
    std::numeric_limits<BoneIndex>::max();

struct MmdAppendTransform
{
    BoneIndex sourceBone = InvalidBoneIndex;
    float weight = 0.0f;
    bool affectRotation = false;
    bool affectTranslation = false;
};

struct MmdIkLink
{
    BoneIndex bone = InvalidBoneIndex;
    bool hasLimits = false;
    glm::vec3 minimumAngle{0.0f};
    glm::vec3 maximumAngle{0.0f};
};

struct MmdIkConstraint
{
    BoneIndex targetBone = InvalidBoneIndex;
    std::uint32_t iterations = 1U;
    float angleLimit = 0.0f;
    std::vector<MmdIkLink> links;
};

// Immutable shared bind-pose data for one bone. Runtime animation state lives
// in Pose rather than here so multiple model instances can share a Skeleton.
struct Bone
{
    std::string name;
    BoneIndex parentIndex = InvalidBoneIndex;
    glm::mat4 bindLocalMatrix{1.0f};
    glm::mat4 inverseBindMatrix{1.0f};
    std::int32_t deformLayer = 0;
    std::uint32_t sourceOrder = 0U;
    bool deformAfterPhysics = false;
    std::optional<MmdAppendTransform> appendTransform;
    std::optional<MmdIkConstraint> ikConstraint;
};
