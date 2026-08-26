#include "assets/vrm_parser.hpp"

#include <nlohmann/json.hpp>

#define USE_VRMC_VRM_0_0
#include <VRMC/VRM.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace wisteria::assets
{
namespace
{

using VrmSource = VRMC_VRM_0_0::Vrm;
using VrmSourceBone = VRMC_VRM_0_0::HumanoidBone::Bone;
using VrmSourcePreset = VRMC_VRM_0_0::BlendshapeGroup::PresetName;
using VrmSourceLookAt = VRMC_VRM_0_0::Firstperson::LookAtTypeName;
using VrmSourceLicense = VRMC_VRM_0_0::Meta::LicenseName;

VrmHumanoidBoneKind ToEngineBoneKind(const VrmSourceBone kind)
{
    switch (kind)
    {
    case VrmSourceBone::Hips: return VrmHumanoidBoneKind::Hips;
    case VrmSourceBone::LeftUpperLeg: return VrmHumanoidBoneKind::LeftUpperLeg;
    case VrmSourceBone::RightUpperLeg: return VrmHumanoidBoneKind::RightUpperLeg;
    case VrmSourceBone::LeftLowerLeg: return VrmHumanoidBoneKind::LeftLowerLeg;
    case VrmSourceBone::RightLowerLeg: return VrmHumanoidBoneKind::RightLowerLeg;
    case VrmSourceBone::LeftFoot: return VrmHumanoidBoneKind::LeftFoot;
    case VrmSourceBone::RightFoot: return VrmHumanoidBoneKind::RightFoot;
    case VrmSourceBone::Spine: return VrmHumanoidBoneKind::Spine;
    case VrmSourceBone::Chest: return VrmHumanoidBoneKind::Chest;
    case VrmSourceBone::Neck: return VrmHumanoidBoneKind::Neck;
    case VrmSourceBone::Head: return VrmHumanoidBoneKind::Head;
    case VrmSourceBone::LeftShoulder: return VrmHumanoidBoneKind::LeftShoulder;
    case VrmSourceBone::RightShoulder: return VrmHumanoidBoneKind::RightShoulder;
    case VrmSourceBone::LeftUpperArm: return VrmHumanoidBoneKind::LeftUpperArm;
    case VrmSourceBone::RightUpperArm: return VrmHumanoidBoneKind::RightUpperArm;
    case VrmSourceBone::LeftLowerArm: return VrmHumanoidBoneKind::LeftLowerArm;
    case VrmSourceBone::RightLowerArm: return VrmHumanoidBoneKind::RightLowerArm;
    case VrmSourceBone::LeftHand: return VrmHumanoidBoneKind::LeftHand;
    case VrmSourceBone::RightHand: return VrmHumanoidBoneKind::RightHand;
    case VrmSourceBone::LeftToes: return VrmHumanoidBoneKind::LeftToes;
    case VrmSourceBone::RightToes: return VrmHumanoidBoneKind::RightToes;
    case VrmSourceBone::LeftEye: return VrmHumanoidBoneKind::LeftEye;
    case VrmSourceBone::RightEye: return VrmHumanoidBoneKind::RightEye;
    case VrmSourceBone::Jaw: return VrmHumanoidBoneKind::Jaw;
    case VrmSourceBone::LeftThumbProximal: return VrmHumanoidBoneKind::LeftThumbProximal;
    case VrmSourceBone::LeftThumbIntermediate: return VrmHumanoidBoneKind::LeftThumbIntermediate;
    case VrmSourceBone::LeftThumbDistal: return VrmHumanoidBoneKind::LeftThumbDistal;
    case VrmSourceBone::LeftIndexProximal: return VrmHumanoidBoneKind::LeftIndexProximal;
    case VrmSourceBone::LeftIndexIntermediate: return VrmHumanoidBoneKind::LeftIndexIntermediate;
    case VrmSourceBone::LeftIndexDistal: return VrmHumanoidBoneKind::LeftIndexDistal;
    case VrmSourceBone::LeftMiddleProximal: return VrmHumanoidBoneKind::LeftMiddleProximal;
    case VrmSourceBone::LeftMiddleIntermediate: return VrmHumanoidBoneKind::LeftMiddleIntermediate;
    case VrmSourceBone::LeftMiddleDistal: return VrmHumanoidBoneKind::LeftMiddleDistal;
    case VrmSourceBone::LeftRingProximal: return VrmHumanoidBoneKind::LeftRingProximal;
    case VrmSourceBone::LeftRingIntermediate: return VrmHumanoidBoneKind::LeftRingIntermediate;
    case VrmSourceBone::LeftRingDistal: return VrmHumanoidBoneKind::LeftRingDistal;
    case VrmSourceBone::LeftLittleProximal: return VrmHumanoidBoneKind::LeftLittleProximal;
    case VrmSourceBone::LeftLittleIntermediate: return VrmHumanoidBoneKind::LeftLittleIntermediate;
    case VrmSourceBone::LeftLittleDistal: return VrmHumanoidBoneKind::LeftLittleDistal;
    case VrmSourceBone::RightThumbProximal: return VrmHumanoidBoneKind::RightThumbProximal;
    case VrmSourceBone::RightThumbIntermediate: return VrmHumanoidBoneKind::RightThumbIntermediate;
    case VrmSourceBone::RightThumbDistal: return VrmHumanoidBoneKind::RightThumbDistal;
    case VrmSourceBone::RightIndexProximal: return VrmHumanoidBoneKind::RightIndexProximal;
    case VrmSourceBone::RightIndexIntermediate: return VrmHumanoidBoneKind::RightIndexIntermediate;
    case VrmSourceBone::RightIndexDistal: return VrmHumanoidBoneKind::RightIndexDistal;
    case VrmSourceBone::RightMiddleProximal: return VrmHumanoidBoneKind::RightMiddleProximal;
    case VrmSourceBone::RightMiddleIntermediate: return VrmHumanoidBoneKind::RightMiddleIntermediate;
    case VrmSourceBone::RightMiddleDistal: return VrmHumanoidBoneKind::RightMiddleDistal;
    case VrmSourceBone::RightRingProximal: return VrmHumanoidBoneKind::RightRingProximal;
    case VrmSourceBone::RightRingIntermediate: return VrmHumanoidBoneKind::RightRingIntermediate;
    case VrmSourceBone::RightRingDistal: return VrmHumanoidBoneKind::RightRingDistal;
    case VrmSourceBone::RightLittleProximal: return VrmHumanoidBoneKind::RightLittleProximal;
    case VrmSourceBone::RightLittleIntermediate: return VrmHumanoidBoneKind::RightLittleIntermediate;
    case VrmSourceBone::RightLittleDistal: return VrmHumanoidBoneKind::RightLittleDistal;
    case VrmSourceBone::UpperChest: return VrmHumanoidBoneKind::UpperChest;
    }
    return VrmHumanoidBoneKind::Hips;
}

VrmExpressionPreset ToEnginePreset(const VrmSourcePreset preset)
{
    switch (preset)
    {
    case VrmSourcePreset::Unknown: return VrmExpressionPreset::Unknown;
    case VrmSourcePreset::Neutral: return VrmExpressionPreset::Neutral;
    case VrmSourcePreset::A: return VrmExpressionPreset::A;
    case VrmSourcePreset::I: return VrmExpressionPreset::I;
    case VrmSourcePreset::U: return VrmExpressionPreset::U;
    case VrmSourcePreset::E: return VrmExpressionPreset::E;
    case VrmSourcePreset::O: return VrmExpressionPreset::O;
    case VrmSourcePreset::Blink: return VrmExpressionPreset::Blink;
    case VrmSourcePreset::Joy: return VrmExpressionPreset::Joy;
    case VrmSourcePreset::Angry: return VrmExpressionPreset::Angry;
    case VrmSourcePreset::Sorrow: return VrmExpressionPreset::Sorrow;
    case VrmSourcePreset::Fun: return VrmExpressionPreset::Fun;
    case VrmSourcePreset::Lookup: return VrmExpressionPreset::LookUp;
    case VrmSourcePreset::Lookdown: return VrmExpressionPreset::LookDown;
    case VrmSourcePreset::Lookleft: return VrmExpressionPreset::LookLeft;
    case VrmSourcePreset::Lookright: return VrmExpressionPreset::LookRight;
    case VrmSourcePreset::Blink_l: return VrmExpressionPreset::BlinkLeft;
    case VrmSourcePreset::Blink_r: return VrmExpressionPreset::BlinkRight;
    }
    return VrmExpressionPreset::Unknown;
}

VrmLookAtType ToEngineLookAtType(const VrmSourceLookAt type)
{
    return type == VrmSourceLookAt::BlendShape
        ? VrmLookAtType::BlendShape
        : VrmLookAtType::Bone;
}

VrmLicenseName ToEngineLicenseName(const VrmSourceLicense license)
{
    switch (license)
    {
    case VrmSourceLicense::Redistribution_Prohibited: return VrmLicenseName::RedistributionProhibited;
    case VrmSourceLicense::CC0: return VrmLicenseName::Cc0;
    case VrmSourceLicense::CC_BY: return VrmLicenseName::CcBy;
    case VrmSourceLicense::CC_BY_NC: return VrmLicenseName::CcByNc;
    case VrmSourceLicense::CC_BY_SA: return VrmLicenseName::CcBySa;
    case VrmSourceLicense::CC_BY_NC_SA: return VrmLicenseName::CcByNcSa;
    case VrmSourceLicense::CC_BY_ND: return VrmLicenseName::CcByNd;
    case VrmSourceLicense::CC_BY_NC_ND: return VrmLicenseName::CcByNcNd;
    case VrmSourceLicense::Other: return VrmLicenseName::Other;
    }
    return VrmLicenseName::Other;
}

std::string GltfNodeName(
    const nlohmann::json& gltfJson,
    const std::uint32_t nodeIndex
)
{
    const auto nodes = gltfJson.find("nodes");
    if (nodes == gltfJson.end() || !nodes->is_array())
        return {};

    if (nodeIndex >= nodes->size())
        return {};

    const nlohmann::json& node = (*nodes)[nodeIndex];
    if (!node.is_object())
        return {};

    const auto name = node.find("name");
    if (name == node.end() || !name->is_string())
        return {};

    return name->get<std::string>();
}

BoneIndex ResolveEngineBone(
    const Skeleton* skeleton,
    const std::string& nodeName
) noexcept
{
    if (skeleton == nullptr || nodeName.empty())
        return InvalidBoneIndex;

    const std::optional<BoneIndex> resolved = skeleton->FindBone(nodeName);
    return resolved.value_or(InvalidBoneIndex);
}

}  // namespace

bool HasVrmExtension(const nlohmann::json& gltfJson) noexcept
{
    try
    {
        const auto extensions = gltfJson.find("extensions");
        if (extensions == gltfJson.end() || !extensions->is_object())
            return false;
        return extensions->contains("VRMC_vrm");
    }
    catch (...)
    {
        return false;
    }
}

std::optional<VrmMetadata> ParseVrmMetadata(
    const nlohmann::json& gltfJson,
    const Skeleton* skeleton,
    std::string& error
)
{
    error.clear();
    try
    {
        const nlohmann::json& source =
            gltfJson.at("extensions").at("VRMC_vrm");

        const auto specVersion = source.find("specVersion");
        if (specVersion != source.end() && specVersion->is_string() &&
            specVersion->get<std::string>().starts_with("1."))
        {
            error = "VRM 1.0 metadata is not supported yet";
            return std::nullopt;
        }

        const VrmSource vrm = source.get<VrmSource>();

        VrmMetadata result;
        result.specVersion = vrm.specVersion;
        result.model.title = vrm.meta.title;
        result.model.version = vrm.meta.version;
        result.model.author = vrm.meta.author;
        result.model.contactInformation = vrm.meta.contactInformation;
        result.model.reference = vrm.meta.reference;
        result.model.licenseName = ToEngineLicenseName(vrm.meta.licenseName);
        result.model.otherLicenseUrl = vrm.meta.otherLicenseUrl;

        result.humanoidBones.reserve(vrm.humanoid.humanBones.size());
        for (const auto& sourceBone : vrm.humanoid.humanBones)
        {
            VrmHumanoidBoneBinding binding;
            binding.kind = ToEngineBoneKind(sourceBone.bone);
            binding.sourceNode = sourceBone.node;
            binding.sourceNodeName =
                GltfNodeName(gltfJson, sourceBone.node);
            binding.bone = ResolveEngineBone(
                skeleton,
                binding.sourceNodeName
            );
            binding.useDefaultValues = sourceBone.useDefaultValues;
            binding.minimum = {
                sourceBone.min.x,
                sourceBone.min.y,
                sourceBone.min.z
            };
            binding.maximum = {
                sourceBone.max.x,
                sourceBone.max.y,
                sourceBone.max.z
            };
            binding.center = {
                sourceBone.center.x,
                sourceBone.center.y,
                sourceBone.center.z
            };
            binding.axisLength = sourceBone.axisLength;
            result.humanoidBones.push_back(std::move(binding));
        }

        result.expressions.reserve(
            vrm.blendShapeMaster.blendShapeGroups.size()
        );
        for (const auto& sourceExpression :
             vrm.blendShapeMaster.blendShapeGroups)
        {
            VrmExpressionDefinition expression;
            expression.name = sourceExpression.name;
            expression.preset = ToEnginePreset(
                sourceExpression.presetName
            );
            expression.isBinary = sourceExpression.isBinary;
            result.expressions.push_back(std::move(expression));
        }

        if (source.contains("firstPerson"))
        {
            VrmFirstPerson firstPerson;
            firstPerson.sourceNode = vrm.firstPerson.firstPersonBone;
            firstPerson.sourceNodeName =
                GltfNodeName(gltfJson, firstPerson.sourceNode);
            firstPerson.bone = ResolveEngineBone(
                skeleton,
                firstPerson.sourceNodeName
            );
            firstPerson.lookAtType = ToEngineLookAtType(
                vrm.firstPerson.lookAtTypeName
            );
            result.firstPerson = std::move(firstPerson);
        }

        return result;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return std::nullopt;
    }
}

}  // namespace wisteria::assets
