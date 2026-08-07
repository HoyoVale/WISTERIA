#include "wisteria/mmd/physics/mmd_physics_audit.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace wisteria
{
namespace
{
constexpr float Epsilon = 1.0e-6f;

bool IsFinite(float value) noexcept
{
    return std::isfinite(value) != 0;
}

float CharacteristicSize(const MmdRigidBodyDefinition& body) noexcept
{
    switch (body.shape)
    {
        case MmdRigidBodyShape::Sphere:
            return 2.0f * body.size.x;
        case MmdRigidBodyShape::Capsule:
            return 2.0f * body.size.x + body.size.y;
        case MmdRigidBodyShape::Box:
        default:
            return std::max(
                {body.size.x, body.size.y, body.size.z}
            );
    }
}

MmdPhysicsAuditRange SummarizeRange(
    std::vector<float> values
)
{
    MmdPhysicsAuditRange result;
    result.count = values.size();
    if (values.empty())
        return result;

    std::vector<float> finiteValues;
    finiteValues.reserve(values.size());
    for (const float value : values)
    {
        if (IsFinite(value) && value >= 0.0f)
            finiteValues.push_back(value);
    }
    if (finiteValues.empty())
    {
        result.count = 0U;
        return result;
    }

    std::sort(finiteValues.begin(), finiteValues.end());
    result.available = true;
    result.count = finiteValues.size();
    for (const float value : finiteValues)
    {
        if (value <= Epsilon)
            ++result.zeroCount;
    }
    result.minPositive = 0.0f;
    for (const float value : finiteValues)
    {
        if (value > Epsilon)
        {
            result.minPositive = value;
            break;
        }
    }
    const std::size_t size = finiteValues.size();
    result.median = size == 1U
        ? finiteValues[0U]
        : (finiteValues[size / 2U] + finiteValues[(size - 1U) / 2U]) * 0.5f;
    const std::size_t p95Index = static_cast<std::size_t>(
        std::floor(0.95 * static_cast<double>(size - 1U))
    );
    result.p95 = finiteValues[p95Index];
    result.max = finiteValues.back();
    return result;
}
}  // namespace

MmdPhysicsAuditResult RunMmdPhysicsAudit(
    const MmdPhysicsAsset& asset,
    std::span<const Bone> bones,
    const MmdPhysicsConfiguration& configuration,
    const MmdPhysicsAuditBounds& modelBounds,
    const MmdPhysicsAuditOptions& options
)
{
    MmdPhysicsAuditResult result;
    if (modelBounds.available &&
        IsFinite(modelBounds.min.x) &&
        IsFinite(modelBounds.min.y) &&
        IsFinite(modelBounds.min.z) &&
        IsFinite(modelBounds.max.x) &&
        IsFinite(modelBounds.max.y) &&
        IsFinite(modelBounds.max.z))
    {
        result.modelBounds = modelBounds;
        const float height = modelBounds.max.y - modelBounds.min.y;
        if (height > Epsilon)
        {
            result.modelHeightAvailable = true;
            result.modelHeight = height;
        }
    }

    std::vector<float> boneLengths;
    boneLengths.reserve(bones.size());
    for (const Bone& bone : bones)
    {
        if (bone.parentIndex == InvalidBoneIndex)
            continue;
        const glm::vec3 localTranslation(bone.bindLocalMatrix[3]);
        boneLengths.push_back(glm::length(localTranslation));
    }
    result.boneLength = SummarizeRange(std::move(boneLengths));

    std::vector<float> bodySizes;
    bodySizes.reserve(asset.RigidBodyCount());
    for (const MmdRigidBodyDefinition& body : asset.RigidBodies())
    {
        bodySizes.push_back(CharacteristicSize(body));
    }
    result.rigidBodySize = SummarizeRange(std::move(bodySizes));

    std::vector<float> linearExtents;
    std::vector<float> angularExtentsDeg;
    linearExtents.reserve(asset.JointCount());
    angularExtentsDeg.reserve(asset.JointCount());
    for (const MmdJointDefinition& joint : asset.Joints())
    {
        linearExtents.push_back(glm::length(
            joint.linearUpper - joint.linearLower
        ));
        angularExtentsDeg.push_back(glm::degrees(glm::length(
            joint.angularUpper - joint.angularLower
        )));
    }
    result.jointLinearRange = SummarizeRange(std::move(linearExtents));
    result.jointAngularRangeDeg =
        SummarizeRange(std::move(angularExtentsDeg));

    const glm::vec3 gravity = configuration.runtime.gravity;
    const float gravityScale = configuration.compatibility.gravityScale;
    if (IsFinite(gravity.x) &&
        IsFinite(gravity.y) &&
        IsFinite(gravity.z) &&
        IsFinite(gravityScale) &&
        gravityScale > 0.0f)
    {
        result.gravityAvailable = true;
        result.gravityMagnitude =
            glm::length(gravity) * gravityScale;
        if (result.modelHeightAvailable)
        {
            result.gravityPerModelHeightAvailable = true;
            result.gravityPerModelHeight =
                result.gravityMagnitude / result.modelHeight;
        }
    }

    if (IsFinite(configuration.runtime.fixedTimeStep) &&
        configuration.runtime.fixedTimeStep > 0.0f)
    {
        result.fixedTimeStep = configuration.runtime.fixedTimeStep;
    }

    if (IsFinite(options.collisionMargin) &&
        options.collisionMargin > Epsilon &&
        result.rigidBodySize.available &&
        result.rigidBodySize.median > Epsilon)
    {
        result.shapeMarginRatioAvailable = true;
        result.shapeMarginPerMedianBodySize =
            options.collisionMargin / result.rigidBodySize.median;
    }
    return result;
}
}  // namespace wisteria
