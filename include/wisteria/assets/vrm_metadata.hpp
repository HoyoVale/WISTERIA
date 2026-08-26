#pragma once

#include <glm/glm.hpp>

#include "wisteria/animation/bone.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wisteria
{

// Engine-owned VRM semantic layer. VRM 0.x parsing is performed by the
// vendored VRM.h header inside the assets module; runtime and rendering code
// only sees these backend-neutral structures.

enum class VrmHumanoidBoneKind : std::uint8_t
{
    Hips = 0,
    LeftUpperLeg,
    RightUpperLeg,
    LeftLowerLeg,
    RightLowerLeg,
    LeftFoot,
    RightFoot,
    Spine,
    Chest,
    Neck,
    Head,
    LeftShoulder,
    RightShoulder,
    LeftUpperArm,
    RightUpperArm,
    LeftLowerArm,
    RightLowerArm,
    LeftHand,
    RightHand,
    LeftToes,
    RightToes,
    LeftEye,
    RightEye,
    Jaw,
    LeftThumbProximal,
    LeftThumbIntermediate,
    LeftThumbDistal,
    LeftIndexProximal,
    LeftIndexIntermediate,
    LeftIndexDistal,
    LeftMiddleProximal,
    LeftMiddleIntermediate,
    LeftMiddleDistal,
    LeftRingProximal,
    LeftRingIntermediate,
    LeftRingDistal,
    LeftLittleProximal,
    LeftLittleIntermediate,
    LeftLittleDistal,
    RightThumbProximal,
    RightThumbIntermediate,
    RightThumbDistal,
    RightIndexProximal,
    RightIndexIntermediate,
    RightIndexDistal,
    RightMiddleProximal,
    RightMiddleIntermediate,
    RightMiddleDistal,
    RightRingProximal,
    RightRingIntermediate,
    RightRingDistal,
    RightLittleProximal,
    RightLittleIntermediate,
    RightLittleDistal,
    UpperChest
};

enum class VrmExpressionPreset : std::uint8_t
{
    Unknown = 0,
    Neutral,
    A,
    I,
    U,
    E,
    O,
    Blink,
    Joy,
    Angry,
    Sorrow,
    Fun,
    LookUp,
    LookDown,
    LookLeft,
    LookRight,
    BlinkLeft,
    BlinkRight,
    Relaxed,
    Surprised
};

enum class VrmLookAtType : std::uint8_t
{
    Bone = 0,
    BlendShape = 1
};

enum class VrmLicenseName : std::uint8_t
{
    RedistributionProhibited = 0,
    Cc0,
    CcBy,
    CcByNc,
    CcBySa,
    CcByNcSa,
    CcByNd,
    CcByNcNd,
    Other
};

struct VrmModelInfo
{
    // VRM 1.0 uses `name` / `authors`; VRM 0.x uses `title` / `author`.
    std::string name;
    std::string title;
    std::vector<std::string> authors;
    std::string copyrightInformation;
    std::string licenseUrl;

    std::string version;
    std::string author;
    std::string contactInformation;
    std::string reference;
    VrmLicenseName licenseName = VrmLicenseName::Other;
    std::string otherLicenseUrl;
};

struct VrmHumanoidBoneBinding
{
    VrmHumanoidBoneKind kind = VrmHumanoidBoneKind::Hips;
    BoneIndex bone = InvalidBoneIndex;
    std::uint32_t sourceNode = 0U;
    std::string sourceNodeName;
    bool useDefaultValues = false;
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
    glm::vec3 center{0.0f};
    float axisLength = 0.0f;
};

struct VrmExpressionDefinition
{
    std::string name;
    VrmExpressionPreset preset = VrmExpressionPreset::Unknown;
    bool isBinary = false;
};

struct VrmFirstPerson
{
    BoneIndex bone = InvalidBoneIndex;
    std::uint32_t sourceNode = 0U;
    std::string sourceNodeName;
    VrmLookAtType lookAtType = VrmLookAtType::Bone;
};

struct VrmMetadata
{
    std::string specVersion;
    VrmModelInfo model;
    std::vector<VrmHumanoidBoneBinding> humanoidBones;
    std::vector<VrmExpressionDefinition> expressions;
    std::optional<VrmFirstPerson> firstPerson;
};

}  // namespace wisteria
