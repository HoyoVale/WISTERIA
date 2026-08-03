#include "pch.hpp"
#include "mmd_physics_instance.hpp"
#include "physics_world.hpp"
#include "pose.hpp"
#include "transform.hpp"

#include <array>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace
{
constexpr std::size_t MmdStabilizationSteps = 30U;
constexpr float MmdStabilizationTimeStep = 1.0f / 60.0f;
constexpr float JointWarmupLinearViolation = 0.1f;
constexpr float JointWarmupAngularViolationDegrees = 5.0f;
constexpr float JointFailureLinearViolation = 0.5f;
constexpr float JointFailureAngularViolationDegrees = 45.0f;
constexpr float RecoveryPersistenceSeconds = 0.45f;
constexpr float RecoveryHighVelocityPersistenceSeconds = 0.30f;
constexpr float RecoveryCooldownSeconds = 3.0f;
constexpr float RecoveryFuseWindowSeconds = 12.0f;
constexpr float RecoveryFuseDurationSeconds = 10.0f;
constexpr std::size_t RecoveryFuseLimit = 3U;
constexpr std::size_t RecoveryLocalGraphRadius = 4U;
constexpr std::size_t RecoveryMaximumDynamicBodies = 24U;
constexpr std::size_t RecoveryMaximumTotalBodies = 32U;
constexpr float RecoveryLinearSpeed = 55.0f;
constexpr float RecoveryHardLinearSpeed = 180.0f;
constexpr float RecoveryAngularSpeed = 140.0f;
constexpr float RecoveryHardAngularSpeed = 360.0f;
constexpr float RecoveryJointSeparation = 1.75f;
constexpr float RecoveryHardJointSeparation = 6.0f;
constexpr float RecoveryLinearViolation = 1.0f;
constexpr float RecoveryHardLinearViolation = 3.0f;
constexpr float RecoveryAngularViolationDegrees = 115.0f;
constexpr float RecoveryHardAngularViolationDegrees = 175.0f;
constexpr float RecoveryRunawayDistance = 5.0f;
constexpr float RecoveryNormalizedExtension = 1.35f;
constexpr float RecoveryHardNormalizedExtension = 3.0f;
constexpr float RecoveryExtensionGrowthTolerance = 0.025f;
constexpr float RecoveryRunawaySupportSpeed = 8.0f;
constexpr float RecoverySeverityGrowthTolerance = 0.03f;
constexpr float CollisionNearNeighborProximityFactor = 0.85f;
constexpr float AdaptiveCcdEnableTravelFactor = 0.35f;
constexpr float AdaptiveCcdDisableTravelFactor = 0.15f;
constexpr float AdaptiveCcdDisableDelaySeconds = 0.25f;
constexpr std::size_t MaximumContactDiagnostics = 64U;
constexpr std::size_t SkirtSelfCollisionGraphDistance = 4U;
constexpr float SkirtSelfCollisionProximityFactor = 1.15f;

struct ChainBalanceProfile
{
    float gravityScale = 1.0f;
    float minimumLinearDamping = 0.0f;
    float minimumAngularDamping = 0.0f;
};

float GravityModeScale(MmdPhysicsGravityMode mode) noexcept
{
    switch (mode)
    {
    case MmdPhysicsGravityMode::Original:
    case MmdPhysicsGravityMode::Balanced100:
        return 1.0f;
    case MmdPhysicsGravityMode::Balanced075:
        return 0.75f;
    case MmdPhysicsGravityMode::Balanced050:
        return 0.50f;
    case MmdPhysicsGravityMode::Balanced025:
        return 0.25f;
    case MmdPhysicsGravityMode::Zero:
        return 0.0f;
    }
    return 1.0f;
}

bool GravityModeUsesChainProfiles(MmdPhysicsGravityMode mode) noexcept
{
    return mode != MmdPhysicsGravityMode::Original;
}

const char* ChainKindName(MmdPhysicsChainKind kind) noexcept
{
    switch (kind)
    {
    case MmdPhysicsChainKind::General: return "GENERAL";
    case MmdPhysicsChainKind::Skirt: return "SKIRT";
    case MmdPhysicsChainKind::Hair: return "HAIR";
    case MmdPhysicsChainKind::Tail: return "TAIL";
    case MmdPhysicsChainKind::Accessory: return "ACCESSORY";
    case MmdPhysicsChainKind::DecorativeFallback:
        return "DECORATIVE_FALLBACK";
    }
    return "UNKNOWN";
}

ChainBalanceProfile ProfileForChainKind(MmdPhysicsChainKind kind) noexcept
{
    switch (kind)
    {
    case MmdPhysicsChainKind::Skirt:
        return {0.55f, 0.25f, 0.35f};
    case MmdPhysicsChainKind::Hair:
        return {0.65f, 0.18f, 0.28f};
    case MmdPhysicsChainKind::Tail:
        return {0.70f, 0.15f, 0.25f};
    case MmdPhysicsChainKind::Accessory:
        return {0.50f, 0.22f, 0.32f};
    case MmdPhysicsChainKind::DecorativeFallback:
        return {0.65f, 0.18f, 0.28f};
    case MmdPhysicsChainKind::General:
        return {1.0f, 0.0f, 0.0f};
    }
    return {};
}

std::string AsciiLower(std::string_view value)
{
    std::string result(value);
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char character)
        {
            if (character >= 'A' && character <= 'Z')
                return static_cast<char>(character - 'A' + 'a');
            return static_cast<char>(character);
        }
    );
    return result;
}

bool ContainsAnyNameToken(
    std::string_view name,
    std::initializer_list<std::string_view> tokens
)
{
    const std::string lowered = AsciiLower(name);
    for (std::string_view token : tokens)
    {
        const std::string loweredToken = AsciiLower(token);
        if (lowered.find(loweredToken) != std::string::npos)
            return true;
    }
    return false;
}

struct SkirtSemantic
{
    bool valid = false;
    int section = -1;
    int level = -1;
    bool auxiliary = false;
};

SkirtSemantic ParseSkirtSemantic(std::string_view name)
{
    const std::string lowered = AsciiLower(name);
    const std::size_t skirt = lowered.find("skirt_");
    if (skirt == std::string::npos)
        return {};

    std::size_t cursor = skirt + 6U;
    const auto parseInteger = [&lowered, &cursor]() -> int
    {
        if (cursor >= lowered.size() ||
            lowered[cursor] < '0' || lowered[cursor] > '9')
        {
            return -1;
        }
        int value = 0;
        while (cursor < lowered.size() &&
            lowered[cursor] >= '0' && lowered[cursor] <= '9')
        {
            value = value * 10 + static_cast<int>(lowered[cursor] - '0');
            ++cursor;
        }
        return value;
    };

    const int section = parseInteger();
    if (section < 0 || cursor >= lowered.size() || lowered[cursor] != '_')
        return {};
    ++cursor;
    const int level = parseInteger();
    if (level < 0)
        return {};

    bool auxiliary = false;
    while (cursor < lowered.size())
    {
        if (lowered[cursor] == 'b')
            auxiliary = true;
        ++cursor;
    }
    return SkirtSemantic{true, section, level, auxiliary};
}

MmdPhysicsChainKind ClassifyBodyName(std::string_view name)
{
    if (ContainsAnyNameToken(
            name,
            {"skirt", "dress", "スカート", "裙", "衣摆", "衣襬"}))
    {
        return MmdPhysicsChainKind::Skirt;
    }
    if (ContainsAnyNameToken(
            name,
            {"hair", "髪", "头发", "頭髮", "发束", "髮束"}))
    {
        return MmdPhysicsChainKind::Hair;
    }
    if (ContainsAnyNameToken(
            name,
            {"tail", "尻尾", "しっぽ", "尾巴", "尾"}))
    {
        return MmdPhysicsChainKind::Tail;
    }
    if (ContainsAnyNameToken(
            name,
            {"ribbon", "accessory", "ornament", "cape", "cloth", "belt",
             "リボン", "アクセ", "装飾", "飾", "饰", "袖", "披风", "披風"}))
    {
        return MmdPhysicsChainKind::Accessory;
    }
    return MmdPhysicsChainKind::General;
}

struct RigidTransform
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct EntityFrame
{
    RigidTransform rigid;
    float scale = 1.0f;
};

const char* RecoveryReasonName(MmdPhysicsRecoveryReason reason) noexcept
{
    switch (reason)
    {
    case MmdPhysicsRecoveryReason::None:
        return "none";
    case MmdPhysicsRecoveryReason::NonFinite:
        return "non_finite";
    case MmdPhysicsRecoveryReason::NonFiniteJoint:
        return "non_finite_joint";
    case MmdPhysicsRecoveryReason::ExtremeVelocity:
        return "extreme_velocity";
    case MmdPhysicsRecoveryReason::HighVelocity:
        return "high_velocity";
    case MmdPhysicsRecoveryReason::Runaway:
        return "runaway";
    case MmdPhysicsRecoveryReason::JointViolation:
        return "joint_violation";
    }
    return "unknown";
}

float CharacteristicBodySize(const MmdRigidBodyDefinition& body) noexcept
{
    switch (body.shape)
    {
    case MmdRigidBodyShape::Sphere:
        return std::max(body.size.x, 0.001f);
    case MmdRigidBodyShape::Box:
        return std::max(std::min({body.size.x, body.size.y, body.size.z}), 0.001f);
    case MmdRigidBodyShape::Capsule:
        return std::max(body.size.x, 0.001f);
    }
    return 0.001f;
}

RigidTransform ExtractRigidTransform(const glm::mat4& matrix)
{
    const BoneTransform transform = BoneTransform::FromMatrix(matrix);
    return RigidTransform{
        transform.translation,
        glm::normalize(transform.rotation)
    };
}

glm::mat4 ToMatrix(const RigidTransform& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.position) *
        glm::mat4_cast(glm::normalize(transform.rotation));
}

EntityFrame ExtractEntityFrame(const Transform& transform)
{
    const glm::vec3& scale = transform.Scale();
    constexpr float tolerance = 0.0001f;
    if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f ||
        std::abs(scale.x - scale.y) > tolerance ||
        std::abs(scale.x - scale.z) > tolerance)
    {
        throw std::invalid_argument(
            "MMD physics requires positive uniform Entity scale"
        );
    }
    const BoneTransform decomposed = BoneTransform::FromMatrix(
        transform.Matrix()
    );
    return EntityFrame{
        RigidTransform{
            decomposed.translation,
            glm::normalize(decomposed.rotation)
        },
        scale.x
    };
}

RigidTransform ModelToWorld(
    const glm::mat4& modelTransform,
    const EntityFrame& entity
)
{
    const RigidTransform model = ExtractRigidTransform(modelTransform);
    return RigidTransform{
        entity.rigid.position + entity.rigid.rotation *
            (model.position * entity.scale),
        glm::normalize(entity.rigid.rotation * model.rotation)
    };
}

glm::mat4 WorldToModel(
    const PhysicsBodyState& worldState,
    const EntityFrame& entity
)
{
    const glm::quat inverseRotation = glm::inverse(entity.rigid.rotation);
    const RigidTransform model{
        inverseRotation * (worldState.position - entity.rigid.position) /
            entity.scale,
        glm::normalize(inverseRotation * worldState.rotation)
    };
    return ToMatrix(model);
}

PhysicsConstraintFrame ConstraintFrameFromMatrix(const glm::mat4& matrix)
{
    const RigidTransform transform = ExtractRigidTransform(matrix);
    return PhysicsConstraintFrame{transform.position, transform.rotation};
}

PhysicsShapeDesc MakeShape(
    const MmdRigidBodyDefinition& definition,
    float scale
)
{
    switch (definition.shape)
    {
    case MmdRigidBodyShape::Sphere:
        return PhysicsShapeDesc::Sphere(definition.size.x * scale);
    case MmdRigidBodyShape::Box:
        return PhysicsShapeDesc::Box(definition.size * scale);
    case MmdRigidBodyShape::Capsule:
        return PhysicsShapeDesc::Capsule(
            definition.size.x * scale,
            definition.size.y * scale
        );
    }
    throw std::invalid_argument("Unknown MMD rigid-body shape");
}

PhysicsMotionType MakeMotionType(MmdRigidBodyMode mode)
{
    return mode == MmdRigidBodyMode::FollowBone
        ? PhysicsMotionType::Kinematic
        : PhysicsMotionType::Dynamic;
}

float MinimumShapeFeature(const PhysicsShapeDesc& shape) noexcept
{
    switch (shape.kind)
    {
    case PhysicsShapeKind::Sphere:
    case PhysicsShapeKind::Capsule:
        return shape.dimensions.x;
    case PhysicsShapeKind::Box:
        return std::min({
            shape.dimensions.x,
            shape.dimensions.y,
            shape.dimensions.z
        });
    }
    return 0.0f;
}

float MaximumShapeExtent(const PhysicsShapeDesc& shape) noexcept
{
    switch (shape.kind)
    {
    case PhysicsShapeKind::Sphere:
        return shape.dimensions.x;
    case PhysicsShapeKind::Box:
        return std::max({
            shape.dimensions.x,
            shape.dimensions.y,
            shape.dimensions.z
        });
    case PhysicsShapeKind::Capsule:
        return shape.dimensions.x + shape.dimensions.y * 0.5f;
    }
    return 0.0f;
}

struct AdaptiveCcdCandidate
{
    bool candidate = false;
    float featureSize = 0.0f;
    float maximumExtent = 0.0f;
    float motionThreshold = 0.0f;
    float sweptSphereRadius = 0.0f;
};

AdaptiveCcdCandidate MakeAdaptiveCcdCandidate(
    const PhysicsBodyDesc& description,
    float entityScale
) noexcept
{
    AdaptiveCcdCandidate result;
    if (description.motionType != PhysicsMotionType::Dynamic)
        return result;

    result.featureSize = MinimumShapeFeature(description.shape);
    result.maximumExtent = MaximumShapeExtent(description.shape);
    if (result.featureSize <= 0.0f || result.maximumExtent <= 0.0f)
        return result;

    const float aspectRatio = result.maximumExtent / result.featureSize;
    const bool smallBody = result.featureSize <= 0.35f * entityScale;
    const bool slenderBody = aspectRatio >= 2.5f;
    result.candidate = smallBody || slenderBody;
    if (!result.candidate)
        return result;

    result.motionThreshold = std::max(
        0.001f * entityScale,
        result.featureSize * 0.35f
    );
    result.sweptSphereRadius = std::max(
        0.0005f * entityScale,
        result.featureSize * 0.75f
    );
    return result;
}

float AutomaticBoxMargin(const PhysicsShapeDesc& shape) noexcept
{
    const float minimumHalfExtent = MinimumShapeFeature(shape);
    const float maximumSafeMargin = minimumHalfExtent * 0.2f;
    return std::min(
        maximumSafeMargin,
        std::min(0.04f, std::max(0.0001f, minimumHalfExtent * 0.08f))
    );
}

float ShapeBoundingRadiusModel(
    const MmdRigidBodyDefinition& definition
) noexcept
{
    switch (definition.shape)
    {
    case MmdRigidBodyShape::Sphere:
        return definition.size.x;
    case MmdRigidBodyShape::Box:
        return glm::length(definition.size);
    case MmdRigidBodyShape::Capsule:
        return definition.size.x + definition.size.y * 0.5f;
    }
    return 0.0f;
}

glm::mat4 AnimatedBodyModelTransform(
    const MmdRigidBodyDefinition& definition,
    const Pose& pose
)
{
    if (definition.bone == InvalidBoneIndex)
        return definition.modelBindTransform;
    const Skeleton& skeleton = pose.GetSkeleton();
    const glm::mat4 boneModel = skeleton.InverseRootMatrix() *
        pose.GlobalMatrix(definition.bone);
    return boneModel * definition.boneToBody;
}



float AxisLimitViolation(float value, float lower, float upper) noexcept
{
    // Bullet treats lower > upper as a free degree of freedom.
    if (lower > upper)
        return 0.0f;
    if (value < lower)
        return lower - value;
    if (value > upper)
        return value - upper;
    return 0.0f;
}


bool IsWideTravelHelperJoint(const MmdJointDefinition& joint) noexcept
{
    if (joint.type != MmdJointType::Spring6Dof &&
        joint.type != MmdJointType::SixDof)
    {
        return false;
    }
    const glm::vec3 span = joint.linearUpper - joint.linearLower;
    constexpr float WideTravelSpan = 20.0f;
    constexpr float SpringEpsilon = 0.000001f;
    return (span.x > WideTravelSpan ||
            span.y > WideTravelSpan ||
            span.z > WideTravelSpan) &&
        std::abs(joint.linearSpring.x) <= SpringEpsilon &&
        std::abs(joint.linearSpring.y) <= SpringEpsilon &&
        std::abs(joint.linearSpring.z) <= SpringEpsilon;
}

glm::vec3 EulerXyzFromRotation(const glm::mat4& matrix) noexcept
{
    // Matches btGeneric6DofSpring2Constraint::matrixToEulerXYZ.
    const float r00 = matrix[0][0];
    const float r01 = matrix[1][0];
    const float r02 = matrix[2][0];
    const float r10 = matrix[0][1];
    const float r11 = matrix[1][1];
    const float r12 = matrix[2][1];
    const float r22 = matrix[2][2];
    const float clamped = std::clamp(r02, -1.0f, 1.0f);
    if (clamped > -1.0f && clamped < 1.0f)
    {
        return glm::vec3(
            std::atan2(-r12, r22),
            std::asin(clamped),
            std::atan2(-r01, r00)
        );
    }
    if (clamped <= -1.0f)
    {
        return glm::vec3(
            -std::atan2(r10, r11),
            -glm::half_pi<float>(),
            0.0f
        );
    }
    return glm::vec3(
        std::atan2(r10, r11),
        glm::half_pi<float>(),
        0.0f
    );
}

glm::vec3 LinearLimitViolation(
    const MmdJointDefinition& joint,
    const glm::vec3& relativePosition
) noexcept
{
    glm::vec3 lower{0.0f};
    glm::vec3 upper{0.0f};
    switch (joint.type)
    {
    case MmdJointType::Spring6Dof:
    case MmdJointType::SixDof:
        lower = joint.linearLower;
        upper = joint.linearUpper;
        break;
    case MmdJointType::Slider:
        lower.x = joint.linearLower.x;
        upper.x = joint.linearUpper.x;
        break;
    case MmdJointType::PointToPoint:
    case MmdJointType::ConeTwist:
    case MmdJointType::Hinge:
        break;
    }
    return glm::vec3(
        AxisLimitViolation(relativePosition.x, lower.x, upper.x),
        AxisLimitViolation(relativePosition.y, lower.y, upper.y),
        AxisLimitViolation(relativePosition.z, lower.z, upper.z)
    );
}

glm::vec3 AngularLimitViolation(
    const MmdJointDefinition& joint,
    const glm::vec3& relativeEuler
) noexcept
{
    glm::vec3 lower{-glm::pi<float>()};
    glm::vec3 upper{glm::pi<float>()};
    switch (joint.type)
    {
    case MmdJointType::Spring6Dof:
    case MmdJointType::SixDof:
        lower = joint.angularLower;
        upper = joint.angularUpper;
        break;
    case MmdJointType::Slider:
        lower.x = joint.angularLower.x;
        upper.x = joint.angularUpper.x;
        break;
    case MmdJointType::Hinge:
        lower.z = joint.angularLower.z;
        upper.z = joint.angularUpper.z;
        break;
    case MmdJointType::PointToPoint:
        return glm::vec3(0.0f);
    case MmdJointType::ConeTwist:
        // Cone-twist spans do not map one-to-one to XYZ Euler limits. Keep
        // this diagnostic conservative and let Bullet enforce those spans.
        return glm::vec3(0.0f);
    }
    return glm::vec3(
        AxisLimitViolation(relativeEuler.x, lower.x, upper.x),
        AxisLimitViolation(relativeEuler.y, lower.y, upper.y),
        AxisLimitViolation(relativeEuler.z, lower.z, upper.z)
    );
}

float QuaternionErrorDegrees(const glm::quat& left, const glm::quat& right)
{
    const float cosine = std::clamp(
        std::abs(glm::dot(glm::normalize(left), glm::normalize(right))),
        0.0f,
        1.0f
    );
    return glm::degrees(2.0f * std::acos(cosine));
}

float MatrixMaximumDifference(
    const glm::mat4& left,
    const glm::mat4& right
) noexcept
{
    float result = 0.0f;
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            result = std::max(
                result,
                std::abs(left[column][row] - right[column][row])
            );
        }
    }
    return result;
}

const char* RigidBodyModeName(MmdRigidBodyMode mode) noexcept
{
    switch (mode)
    {
    case MmdRigidBodyMode::FollowBone: return "FollowBone";
    case MmdRigidBodyMode::Physics: return "Physics";
    case MmdRigidBodyMode::PhysicsWithBone: return "PhysicsWithBone";
    }
    return "Unknown";
}

const char* RigidBodyShapeName(MmdRigidBodyShape shape) noexcept
{
    switch (shape)
    {
    case MmdRigidBodyShape::Sphere: return "Sphere";
    case MmdRigidBodyShape::Box: return "Box";
    case MmdRigidBodyShape::Capsule: return "Capsule";
    }
    return "Unknown";
}

const char* OverlayName(MmdPhysicsDebugOverlay overlay) noexcept
{
    switch (overlay)
    {
    case MmdPhysicsDebugOverlay::Off: return "OFF";
    case MmdPhysicsDebugOverlay::BindPose: return "BIND";
    case MmdPhysicsDebugOverlay::ResetPose: return "RESET";
    case MmdPhysicsDebugOverlay::Runtime: return "RUNTIME";
    case MmdPhysicsDebugOverlay::All: return "ALL";
    }
    return "UNKNOWN";
}

const char* FidelityDebugLayerName(
    MmdPhysicsFidelityDebugLayer layer
) noexcept
{
    switch (layer)
    {
    case MmdPhysicsFidelityDebugLayer::Off: return "OFF";
    case MmdPhysicsFidelityDebugLayer::Bone: return "BONE";
    case MmdPhysicsFidelityDebugLayer::Vertex: return "VERTEX";
    case MmdPhysicsFidelityDebugLayer::All: return "ALL";
    }
    return "UNKNOWN";
}

const char* PhysicsWithBoneSyncModeName(
    MmdPhysicsWithBoneSyncMode mode
) noexcept
{
    switch (mode)
    {
    case MmdPhysicsWithBoneSyncMode::RotationOnly:
        return "ROTATION_ONLY";
    case MmdPhysicsWithBoneSyncMode::FullBody:
        return "FULL_BODY";
    case MmdPhysicsWithBoneSyncMode::TranslationDelta:
        return "TRANSLATION_DELTA";
    }
    return "UNKNOWN";
}

glm::vec3 AngularVelocityBetween(
    const glm::quat& previous,
    const glm::quat& current,
    float deltaTime
) noexcept
{
    if (deltaTime <= 0.0f)
        return glm::vec3(0.0f);

    glm::quat delta = glm::normalize(current * glm::conjugate(previous));
    if (delta.w < 0.0f)
        delta = -delta;

    const float cosine = std::clamp(delta.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::acos(cosine);
    const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    if (angle <= 0.000001f || sine <= 0.000001f)
        return glm::vec3(0.0f);

    const glm::vec3 axis(delta.x / sine, delta.y / sine, delta.z / sine);
    return axis * (angle / deltaTime);
}

glm::vec3 TransformPoint(
    const RigidTransform& transform,
    const glm::vec3& point
) noexcept
{
    return transform.position + transform.rotation * point;
}

void AppendLine(
    std::vector<PhysicsDebugLine>& lines,
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec3& color
)
{
    lines.push_back(PhysicsDebugLine{from, to, color});
}

void AppendTransformAxes(
    std::vector<PhysicsDebugLine>& lines,
    const glm::mat4& modelTransform,
    const EntityFrame& entity,
    float length
)
{
    const RigidTransform world = ModelToWorld(modelTransform, entity);
    constexpr glm::vec3 AxisXColor{1.0f, 0.30f, 0.0f};
    constexpr glm::vec3 AxisYColor{1.0f, 0.62f, 0.08f};
    constexpr glm::vec3 AxisZColor{1.0f, 0.88f, 0.32f};
    AppendLine(
        lines,
        world.position,
        world.position + world.rotation * glm::vec3(length, 0.0f, 0.0f),
        AxisXColor
    );
    AppendLine(
        lines,
        world.position,
        world.position + world.rotation * glm::vec3(0.0f, length, 0.0f),
        AxisYColor
    );
    AppendLine(
        lines,
        world.position,
        world.position + world.rotation * glm::vec3(0.0f, 0.0f, length),
        AxisZColor
    );
}

void AppendCircle(
    std::vector<PhysicsDebugLine>& lines,
    const RigidTransform& transform,
    const glm::vec3& center,
    const glm::vec3& axisU,
    const glm::vec3& axisV,
    float radiusU,
    float radiusV,
    const glm::vec3& color,
    int segments = 12
)
{
    constexpr float Pi = 3.14159265358979323846f;
    glm::vec3 previous{};
    for (int segment = 0; segment <= segments; ++segment)
    {
        const float angle = 2.0f * Pi *
            static_cast<float>(segment) / static_cast<float>(segments);
        const glm::vec3 local = center +
            axisU * (std::cos(angle) * radiusU) +
            axisV * (std::sin(angle) * radiusV);
        const glm::vec3 current = TransformPoint(transform, local);
        if (segment > 0)
            AppendLine(lines, previous, current, color);
        previous = current;
    }
}

void AppendArc(
    std::vector<PhysicsDebugLine>& lines,
    const RigidTransform& transform,
    const glm::vec3& center,
    const glm::vec3& axisU,
    const glm::vec3& axisV,
    float radius,
    float startAngle,
    float endAngle,
    const glm::vec3& color,
    int segments = 8
)
{
    glm::vec3 previous{};
    for (int segment = 0; segment <= segments; ++segment)
    {
        const float amount = static_cast<float>(segment) /
            static_cast<float>(segments);
        const float angle = std::lerp(startAngle, endAngle, amount);
        const glm::vec3 local = center +
            axisU * (std::cos(angle) * radius) +
            axisV * (std::sin(angle) * radius);
        const glm::vec3 current = TransformPoint(transform, local);
        if (segment > 0)
            AppendLine(lines, previous, current, color);
        previous = current;
    }
}

void AppendBodyWireframe(
    std::vector<PhysicsDebugLine>& lines,
    const MmdRigidBodyDefinition& definition,
    const glm::mat4& modelTransform,
    const EntityFrame& entity,
    const glm::vec3& color,
    float diagnosticScale = 1.0f
)
{
    const RigidTransform world = ModelToWorld(modelTransform, entity);
    const float scale = entity.scale * diagnosticScale;
    switch (definition.shape)
    {
    case MmdRigidBodyShape::Sphere:
    {
        const float radius = definition.size.x * scale;
        AppendCircle(lines, world, {}, {1, 0, 0}, {0, 1, 0}, radius, radius, color);
        AppendCircle(lines, world, {}, {1, 0, 0}, {0, 0, 1}, radius, radius, color);
        AppendCircle(lines, world, {}, {0, 1, 0}, {0, 0, 1}, radius, radius, color);
        break;
    }
    case MmdRigidBodyShape::Box:
    {
        const glm::vec3 half = definition.size * scale;
        std::array<glm::vec3, 8U> corners{};
        std::size_t index = 0U;
        for (int x : {-1, 1})
        {
            for (int y : {-1, 1})
            {
                for (int z : {-1, 1})
                {
                    corners[index++] = TransformPoint(
                        world,
                        glm::vec3(x, y, z) * half
                    );
                }
            }
        }
        constexpr std::array<std::array<std::size_t, 2U>, 12U> Edges{{
            {{0, 1}}, {{0, 2}}, {{0, 4}}, {{1, 3}}, {{1, 5}}, {{2, 3}},
            {{2, 6}}, {{3, 7}}, {{4, 5}}, {{4, 6}}, {{5, 7}}, {{6, 7}}
        }};
        for (const auto& edge : Edges)
            AppendLine(lines, corners[edge[0]], corners[edge[1]], color);
        break;
    }
    case MmdRigidBodyShape::Capsule:
    {
        const float radius = definition.size.x * scale;
        const float halfHeight = definition.size.y * scale * 0.5f;
        AppendCircle(lines, world, {0, halfHeight, 0}, {1, 0, 0}, {0, 0, 1}, radius, radius, color);
        AppendCircle(lines, world, {0, -halfHeight, 0}, {1, 0, 0}, {0, 0, 1}, radius, radius, color);
        for (const glm::vec3& radial : std::array<glm::vec3, 4U>{
                 glm::vec3{radius, 0, 0},
                 glm::vec3{-radius, 0, 0},
                 glm::vec3{0, 0, radius},
                 glm::vec3{0, 0, -radius}})
        {
            AppendLine(
                lines,
                TransformPoint(world, radial + glm::vec3(0, halfHeight, 0)),
                TransformPoint(world, radial - glm::vec3(0, halfHeight, 0)),
                color
            );
        }
        constexpr float Pi = 3.14159265358979323846f;
        AppendArc(
            lines, world, {0, halfHeight, 0},
            {1, 0, 0}, {0, 1, 0}, radius,
            0.0f, Pi, color
        );
        AppendArc(
            lines, world, {0, -halfHeight, 0},
            {1, 0, 0}, {0, 1, 0}, radius,
            Pi, 2.0f * Pi, color
        );
        AppendArc(
            lines, world, {0, halfHeight, 0},
            {0, 0, 1}, {0, 1, 0}, radius,
            0.0f, Pi, color
        );
        AppendArc(
            lines, world, {0, -halfHeight, 0},
            {0, 0, 1}, {0, 1, 0}, radius,
            Pi, 2.0f * Pi, color
        );
        break;
    }
    }
}

std::pair<float, float> TransformError(
    const glm::mat4& left,
    const glm::mat4& right
)
{
    const RigidTransform leftRigid = ExtractRigidTransform(left);
    const RigidTransform rightRigid = ExtractRigidTransform(right);
    return {
        glm::distance(leftRigid.position, rightRigid.position),
        QuaternionErrorDegrees(leftRigid.rotation, rightRigid.rotation)
    };
}
}

MmdPhysicsInstance::MmdPhysicsInstance(
    PhysicsWorld& world,
    const MmdPhysicsAsset& asset,
    Pose& pose,
    Transform& transform,
    const MorphState* morphState
)
    : world(&world),
      asset(&asset),
      pose(&pose),
      transform(&transform),
      morphState(morphState)
{
    const EntityFrame entity = ExtractEntityFrame(transform);
    this->rigidBodies.reserve(asset.RigidBodyCount());
    this->constraints.reserve(asset.JointCount());
    const std::size_t boneCount = pose.GetSkeleton().BoneCount();
    this->drivenRuntimeBodyByBone.assign(
        boneCount,
        std::numeric_limits<std::size_t>::max()
    );
    this->drivenBoneModes.assign(boneCount, 0U);
    this->localMatrixScratch.resize(boneCount);
    this->globalMatrixScratch.resize(boneCount);

    std::vector<std::size_t> jointDegree(asset.RigidBodyCount(), 0U);
    for (const MmdJointDefinition& joint : asset.Joints())
    {
        if (joint.bodyA != InvalidRigidBodyIndex &&
            static_cast<std::size_t>(joint.bodyA) < jointDegree.size())
        {
            ++jointDegree[joint.bodyA];
        }
        if (joint.bodyB != InvalidRigidBodyIndex &&
            static_cast<std::size_t>(joint.bodyB) < jointDegree.size())
        {
            ++jointDegree[joint.bodyB];
        }
    }

    try
    {
        for (const MmdRigidBodyDefinition& definition : asset.RigidBodies())
        {
            const std::size_t sourceBodyIndex = this->rigidBodies.size();
            if (definition.mode != MmdRigidBodyMode::FollowBone &&
                definition.mass <= 0.0f)
            {
                throw std::invalid_argument(
                    "Dynamic MMD rigid body must have positive mass: " +
                    definition.name
                );
            }

            const RigidTransform initial = ModelToWorld(
                definition.modelBindTransform,
                entity
            );
            PhysicsBodyDesc description;
            description.shape = MakeShape(definition, entity.scale);
            description.motionType = MakeMotionType(definition.mode);
            description.position = initial.position;
            description.rotation = initial.rotation;
            description.mass = definition.mass;
            description.linearDamping = std::clamp(
                definition.linearDamping,
                0.0f,
                1.0f
            );
            description.angularDamping = std::clamp(
                definition.angularDamping,
                0.0f,
                1.0f
            );
            const bool denseDynamicBox =
                description.motionType == PhysicsMotionType::Dynamic &&
                description.shape.kind == PhysicsShapeKind::Box &&
                sourceBodyIndex < jointDegree.size() &&
                jointDegree[sourceBodyIndex] >= 2U &&
                MinimumShapeFeature(description.shape) <= 0.75f * entity.scale;
            description.restitution = std::clamp(
                definition.restitution,
                0.0f,
                denseDynamicBox ? 0.05f : 0.2f
            );
            description.friction = definition.friction;
            description.collisionGroup = static_cast<std::uint16_t>(
                1U << definition.collisionGroup
            );
            description.collisionMask = static_cast<std::uint16_t>(
                ~definition.nonCollisionMask
            );
            if (denseDynamicBox)
            {
                const float feature = MinimumShapeFeature(description.shape);
                description.collisionMargin = std::min(
                    AutomaticBoxMargin(description.shape),
                    std::max(0.0001f, feature * 0.035f)
                );
            }
            const AdaptiveCcdCandidate ccd = MakeAdaptiveCcdCandidate(
                description,
                entity.scale
            );
            // MMD CCD is activated dynamically from actual per-tick travel;
            // candidates start in discrete mode instead of enabling CCD on
            // most small decorative bodies for their entire lifetime.
            description.enableCcd = false;
            const std::size_t runtimeIndex = this->rigidBodies.size();
            RuntimeBody runtime;
            runtime.definition = &definition;
            runtime.handle = world.CreateBody(description);
            if (this->runtimeBodyByWorldHandle.size() <= runtime.handle.index)
            {
                this->runtimeBodyByWorldHandle.resize(
                    static_cast<std::size_t>(runtime.handle.index) + 1U,
                    std::numeric_limits<std::size_t>::max()
                );
            }
            this->runtimeBodyByWorldHandle[runtime.handle.index] = runtimeIndex;
            runtime.lastAnimatedPosition = initial.position;
            runtime.lastAnimatedRotation = initial.rotation;
            runtime.hasAnimatedTransform = true;
            runtime.createdBulletBindModelTransform = WorldToModel(
                world.State(runtime.handle),
                entity
            );
            runtime.resetTargetModelTransform =
                definition.modelBindTransform;
            runtime.postResetBulletModelTransform =
                runtime.createdBulletBindModelTransform;
            runtime.prePhysicsAnimatedModelTransform =
                definition.modelBindTransform;
            runtime.ccdCandidate = ccd.candidate;
            runtime.ccdFeatureSize = ccd.featureSize;
            runtime.ccdMaximumExtent = ccd.maximumExtent;
            runtime.ccdMotionThreshold = ccd.motionThreshold;
            runtime.ccdSweptSphereRadius = ccd.sweptSphereRadius;
            runtime.denseMarginAdjusted = denseDynamicBox;
            runtime.baseLinearDamping = description.linearDamping;
            runtime.baseAngularDamping = description.angularDamping;
            runtime.appliedLinearDamping = description.linearDamping;
            runtime.appliedAngularDamping = description.angularDamping;
            if (runtime.ccdCandidate)
                ++this->collisionStatistics.ccdCandidateCount;
            if (runtime.denseMarginAdjusted)
                ++this->collisionStatistics.denseMarginBodyCount;
            this->rigidBodies.push_back(std::move(runtime));
            if (definition.mode != MmdRigidBodyMode::FollowBone &&
                definition.bone != InvalidBoneIndex)
            {
                this->drivenRuntimeBodyByBone[definition.bone] = runtimeIndex;
                this->drivenBoneModes[definition.bone] =
                    static_cast<std::uint8_t>(definition.mode) + 1U;
            }
        }

        for (const MmdJointDefinition& joint : asset.Joints())
        {
            if (joint.bodyA != InvalidRigidBodyIndex &&
                joint.bodyA == joint.bodyB)
            {
                continue;
            }

            PhysicsBodyHandle bodyA{};
            PhysicsBodyHandle bodyB{};
            if (joint.bodyA != InvalidRigidBodyIndex)
                bodyA = this->BodyHandleAt(joint.bodyA);
            if (joint.bodyB != InvalidRigidBodyIndex)
                bodyB = this->BodyHandleAt(joint.bodyB);

            const RigidTransform jointWorld = ModelToWorld(
                joint.modelBindTransform,
                entity
            );
            const glm::mat4 jointWorldMatrix = ToMatrix(jointWorld);
            PhysicsConstraintFrame frameA{};
            PhysicsConstraintFrame frameB{};
            if (joint.bodyA != InvalidRigidBodyIndex)
            {
                const RigidTransform bodyWorld = ModelToWorld(
                    asset.RigidBodyAt(joint.bodyA).modelBindTransform,
                    entity
                );
                frameA = ConstraintFrameFromMatrix(
                    glm::inverse(ToMatrix(bodyWorld)) * jointWorldMatrix
                );
            }
            if (joint.bodyB != InvalidRigidBodyIndex)
            {
                const RigidTransform bodyWorld = ModelToWorld(
                    asset.RigidBodyAt(joint.bodyB).modelBindTransform,
                    entity
                );
                frameB = ConstraintFrameFromMatrix(
                    glm::inverse(ToMatrix(bodyWorld)) * jointWorldMatrix
                );
            }

            PhysicsConstraintHandle handle{};
            switch (joint.type)
            {
            case MmdJointType::Spring6Dof:
            {
                PhysicsSpring6DofDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                description.linearLower = joint.linearLower * entity.scale;
                description.linearUpper = joint.linearUpper * entity.scale;
                description.angularLower = joint.angularLower;
                description.angularUpper = joint.angularUpper;
                description.linearStiffness = joint.linearSpring;
                description.angularStiffness = joint.angularSpring;
                handle = world.CreateSpring6DofConstraint(description);
                break;
            }
            case MmdJointType::SixDof:
            {
                PhysicsSixDofDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                description.linearLower = joint.linearLower * entity.scale;
                description.linearUpper = joint.linearUpper * entity.scale;
                description.angularLower = joint.angularLower;
                description.angularUpper = joint.angularUpper;
                handle = world.CreateSixDofConstraint(description);
                break;
            }
            case MmdJointType::PointToPoint:
            {
                PhysicsPointToPointDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.pivotA = frameA.position;
                description.pivotB = frameB.position;
                handle = world.CreatePointToPointConstraint(description);
                break;
            }
            case MmdJointType::ConeTwist:
            {
                const auto span = [](float lower, float upper)
                {
                    return std::max(std::abs(lower), std::abs(upper));
                };
                PhysicsConeTwistDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                description.twistSpan = span(
                    joint.angularLower.x,
                    joint.angularUpper.x
                );
                description.swingSpan1 = span(
                    joint.angularLower.y,
                    joint.angularUpper.y
                );
                description.swingSpan2 = span(
                    joint.angularLower.z,
                    joint.angularUpper.z
                );
                handle = world.CreateConeTwistConstraint(description);
                break;
            }
            case MmdJointType::Slider:
            {
                PhysicsSliderDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                // Bullet's slider axis is local X.
                description.linearLower = joint.linearLower.x * entity.scale;
                description.linearUpper = joint.linearUpper.x * entity.scale;
                description.angularLower = joint.angularLower.x;
                description.angularUpper = joint.angularUpper.x;
                handle = world.CreateSliderConstraint(description);
                break;
            }
            case MmdJointType::Hinge:
            {
                PhysicsHingeDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                // btHingeConstraint uses the frame's local Z axis.
                description.lowerAngle = joint.angularLower.z;
                description.upperAngle = joint.angularUpper.z;
                handle = world.CreateHingeConstraint(description);
                break;
            }
            }
            this->constraints.push_back(handle);
        }
        this->BuildRecoveryChains();
        this->ConfigureGravityBalanceProfiles();
        this->ApplyGravityBalanceSettings(true);
        this->ConfigureCollisionTopology();
        this->createdJointSnapshot = this->CaptureJointSnapshot("created");
        this->ResetToPose(transform);
        this->BuildAlignmentDiagnostics();
        constexpr float AutomaticLogTolerance = 0.0001f;
        if (this->alignmentSummary.bodyCount >= 32U ||
            this->alignmentSummary.maximumSkinningBindError >
                AutomaticLogTolerance ||
            this->alignmentSummary.maximumBindPositionError >
                AutomaticLogTolerance ||
            this->alignmentSummary.maximumBulletPositionError >
                AutomaticLogTolerance)
        {
            this->LogAlignmentSummary();
        }
    }
    catch (...)
    {
        this->DestroyRuntime();
        throw;
    }
}

MmdPhysicsInstance::~MmdPhysicsInstance()
{
    this->DestroyRuntime();
}

void MmdPhysicsInstance::PrepareSimulation(float deltaTime)
{
    this->ApplyGravityBalanceSettings();
    this->PrePhysicsUpdate(*this->transform, deltaTime);
    if (!this->suppressImpulseMorphOnce &&
        this->morphState != nullptr &&
        this->morphState->GetMorphSet().HasKind(MorphKind::Impulse))
    {
        this->ApplyImpulseMorphs(*this->morphState);
    }
    this->suppressImpulseMorphOnce = false;
}

void MmdPhysicsInstance::PrepareSimulationSubstep(
    float alpha,
    float fixedTimeStep
)
{
    if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f)
    {
        throw std::invalid_argument(
            "MMD physics substep alpha must be finite and normalized"
        );
    }
    if (!std::isfinite(fixedTimeStep) || fixedTimeStep <= 0.0f)
    {
        throw std::invalid_argument(
            "MMD physics substep must be finite and positive"
        );
    }

    for (RuntimeBody& runtime : this->rigidBodies)
    {
        const bool driveBody = this->stabilizationFailed ||
            runtime.definition->mode == MmdRigidBodyMode::FollowBone;
        if (!driveBody || !runtime.hasAnimatedTransform)
            continue;

        const glm::vec3 position = glm::mix(
            runtime.frameStartAnimatedPosition,
            runtime.frameTargetAnimatedPosition,
            alpha
        );
        const glm::quat rotation = glm::normalize(glm::slerp(
            runtime.frameStartAnimatedRotation,
            runtime.frameTargetAnimatedRotation,
            alpha
        ));
        const glm::vec3 linearVelocity =
            (position - runtime.lastAnimatedPosition) / fixedTimeStep;
        const glm::vec3 angularVelocity = AngularVelocityBetween(
            runtime.lastAnimatedRotation,
            rotation,
            fixedTimeStep
        );

        if (this->stabilizationFailed)
        {
            this->world->SetTransform(
                runtime.handle,
                position,
                rotation,
                true
            );
        }
        else
        {
            this->world->SetTransform(
                runtime.handle,
                position,
                rotation,
                false
            );
            this->world->SetLinearVelocity(
                runtime.handle,
                linearVelocity
            );
            this->world->SetAngularVelocity(
                runtime.handle,
                angularVelocity
            );
        }

        runtime.lastAnimatedPosition = position;
        runtime.lastAnimatedRotation = rotation;
    }
    this->UpdateAdaptiveCcd(fixedTimeStep);
}

void MmdPhysicsInstance::ObserveSimulationSubstep(float fixedTimeStep)
{
    if (!std::isfinite(fixedTimeStep) || fixedTimeStep <= 0.0f)
    {
        throw std::invalid_argument(
            "MMD recovery time step must be finite and positive"
        );
    }
    this->UpdateCollisionDiagnostics();
    this->UpdateGravityBalanceStatistics();
    if (!this->stabilizationFailed)
        this->RecoverAbnormalChains(fixedTimeStep);
}

void MmdPhysicsInstance::FinishSimulation()
{
    if (!this->stabilizationFailed)
        this->PostPhysicsUpdate(*this->transform);
}

void MmdPhysicsInstance::ResetSimulation()
{
    this->ResetToPose(*this->transform);
}

PhysicsStabilizationRequest
MmdPhysicsInstance::StabilizationRequest() const noexcept
{
    return PhysicsStabilizationRequest{
        this->pendingStabilizationSteps,
        MmdStabilizationTimeStep
    };
}

void MmdPhysicsInstance::PrepareStabilizationStep(float fixedTimeStep)
{
    if (!std::isfinite(fixedTimeStep) || fixedTimeStep <= 0.0f)
    {
        throw std::invalid_argument(
            "MMD stabilization time step must be finite and positive"
        );
    }
    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    for (RuntimeBody& runtime : this->rigidBodies)
    {
        if (runtime.definition->mode != MmdRigidBodyMode::FollowBone)
            continue;
        const RigidTransform target = ModelToWorld(
            runtime.resetTargetModelTransform,
            entity
        );
        this->world->SetTransform(
            runtime.handle,
            target.position,
            target.rotation,
            true
        );
    }
}

void MmdPhysicsInstance::ObserveStabilizationStep(
    std::size_t completedSteps
)
{
    if (completedSteps != 1U &&
        completedSteps != 10U &&
        completedSteps != this->pendingStabilizationSteps)
    {
        return;
    }
    const char* stage = completedSteps == 1U
        ? "warmup-1"
        : (completedSteps == 10U ? "warmup-10" : "warmup-final");
    JointSnapshot snapshot = this->CaptureJointSnapshot(
        stage,
        completedSteps
    );
    this->jointSnapshots.push_back(snapshot);
    if (this->rigidBodies.size() >= 32U ||
        snapshot.jointsOverFailureThreshold > 0U ||
        !snapshot.finite)
    {
        this->LogJointSnapshot(snapshot);
    }
}

void MmdPhysicsInstance::CompleteStabilization()
{
    if (this->pendingStabilizationSteps == 0U)
        return;
    const JointSnapshot finalSnapshot = this->CaptureJointSnapshot(
        "warmup-complete",
        this->pendingStabilizationSteps
    );
    if (this->jointSnapshots.empty() ||
        this->jointSnapshots.back().completedSteps !=
            finalSnapshot.completedSteps)
    {
        this->jointSnapshots.push_back(finalSnapshot);
        if (this->rigidBodies.size() >= 32U ||
            finalSnapshot.jointsOverFailureThreshold > 0U ||
            !finalSnapshot.finite)
        {
            this->LogJointSnapshot(finalSnapshot);
        }
    }

    const bool failed = !finalSnapshot.finite ||
        finalSnapshot.maximumStabilizationLinearViolation >
            JointFailureLinearViolation ||
        finalSnapshot.maximumStabilizationAngularViolationDegrees >
            JointFailureAngularViolationDegrees;
    this->pendingStabilizationSteps = 0U;
    this->SetFailureFreeze(failed);
    if (this->rigidBodies.size() >= 32U || failed)
    {
        std::cout << "[MMD INIT] stabilization="
                  << (failed ? "FAILED_SAFE_FREEZE" : "converged")
                  << " maxJointPos="
                  << finalSnapshot.maximumPositionSeparation
                  << " maxJointRotDeg="
                  << finalSnapshot.maximumRotationErrorDegrees
                  << " maxLinearViolation="
                  << finalSnapshot.maximumStabilizationLinearViolation
                  << " maxAngularViolationDeg="
                  << finalSnapshot.maximumStabilizationAngularViolationDegrees
                  << " severeJoints="
                  << finalSnapshot.jointsOverFailureThreshold
                  << std::endl;
    }
}

void MmdPhysicsInstance::AppendDebugLines(
    std::vector<PhysicsDebugLine>& lines
) const
{
    const bool showFidelityBones =
        this->fidelityDebugLayer == MmdPhysicsFidelityDebugLayer::Bone ||
        this->fidelityDebugLayer == MmdPhysicsFidelityDebugLayer::All;
    if (this->debugOverlay == MmdPhysicsDebugOverlay::Off &&
        !showFidelityBones)
    {
        return;
    }

    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    constexpr glm::vec3 SourceBindColor{0.0f, 0.95f, 1.0f};
    constexpr glm::vec3 CreatedBulletColor{0.15f, 0.45f, 1.0f};
    constexpr glm::vec3 ResetTargetColor{1.0f, 0.75f, 0.0f};
    constexpr glm::vec3 PostResetColor{0.95f, 0.15f, 0.95f};
    constexpr glm::vec3 PrePhysicsTargetColor{0.15f, 1.0f, 0.25f};
    constexpr glm::vec3 CurrentBulletColor{1.0f, 1.0f, 1.0f};
    constexpr glm::vec3 ErrorColor{1.0f, 0.2f, 0.0f};

    const bool showBind =
        this->debugOverlay == MmdPhysicsDebugOverlay::BindPose ||
        this->debugOverlay == MmdPhysicsDebugOverlay::All;
    const bool showReset =
        this->debugOverlay == MmdPhysicsDebugOverlay::ResetPose ||
        this->debugOverlay == MmdPhysicsDebugOverlay::All;
    const bool showRuntime =
        this->debugOverlay == MmdPhysicsDebugOverlay::Runtime ||
        this->debugOverlay == MmdPhysicsDebugOverlay::All;

    for (const RuntimeBody& runtime : this->rigidBodies)
    {
        const MmdRigidBodyDefinition& definition = *runtime.definition;
        if (showBind)
        {
            AppendBodyWireframe(
                lines,
                definition,
                definition.modelBindTransform,
                entity,
                SourceBindColor,
                1.03f
            );
            AppendBodyWireframe(
                lines,
                definition,
                runtime.createdBulletBindModelTransform,
                entity,
                CreatedBulletColor,
                0.97f
            );
            const RigidTransform source = ModelToWorld(
                definition.modelBindTransform,
                entity
            );
            const RigidTransform created = ModelToWorld(
                runtime.createdBulletBindModelTransform,
                entity
            );
            if (glm::distance(source.position, created.position) > 0.0005f)
                AppendLine(lines, source.position, created.position, ErrorColor);
        }

        if (showReset)
        {
            AppendBodyWireframe(
                lines,
                definition,
                runtime.resetTargetModelTransform,
                entity,
                ResetTargetColor,
                1.03f
            );
            AppendBodyWireframe(
                lines,
                definition,
                runtime.postResetBulletModelTransform,
                entity,
                PostResetColor,
                0.97f
            );
            const RigidTransform target = ModelToWorld(
                runtime.resetTargetModelTransform,
                entity
            );
            const RigidTransform reset = ModelToWorld(
                runtime.postResetBulletModelTransform,
                entity
            );
            if (glm::distance(target.position, reset.position) > 0.0005f)
                AppendLine(lines, target.position, reset.position, ErrorColor);
        }

        if (showRuntime)
        {
            const glm::mat4 currentModel = WorldToModel(
                this->world->State(runtime.handle),
                entity
            );
            AppendBodyWireframe(
                lines,
                definition,
                runtime.prePhysicsAnimatedModelTransform,
                entity,
                PrePhysicsTargetColor,
                1.03f
            );
            AppendBodyWireframe(
                lines,
                definition,
                currentModel,
                entity,
                CurrentBulletColor,
                0.97f
            );
            const RigidTransform target = ModelToWorld(
                runtime.prePhysicsAnimatedModelTransform,
                entity
            );
            const RigidTransform current = ModelToWorld(currentModel, entity);
            if (glm::distance(target.position, current.position) > 0.0005f)
                AppendLine(lines, target.position, current.position, ErrorColor);
        }
    }

    if (showRuntime)
    {
        constexpr std::size_t MaximumDebugContacts = 32U;
        const std::size_t count = std::min(
            MaximumDebugContacts,
            this->contactDiagnostics.size()
        );
        for (std::size_t index = 0U; index < count; ++index)
        {
            const MmdPhysicsContactDiagnostic& contact =
                this->contactDiagnostics[index];
            const bool penetrating = contact.maximumPenetrationDepth >
                0.0025f * entity.scale;
            const glm::vec3 color = penetrating
                ? glm::vec3(1.0f, 0.05f, 0.05f)
                : glm::vec3(0.0f, 0.9f, 1.0f);
            const float markerSize = std::max(
                0.01f * entity.scale,
                contact.maximumPenetrationDepth
            );
            const glm::vec3 point = contact.deepestPointOnB;
            AppendLine(
                lines,
                point - glm::vec3(markerSize, 0.0f, 0.0f),
                point + glm::vec3(markerSize, 0.0f, 0.0f),
                color
            );
            AppendLine(
                lines,
                point - glm::vec3(0.0f, markerSize, 0.0f),
                point + glm::vec3(0.0f, markerSize, 0.0f),
                color
            );
            AppendLine(
                lines,
                point,
                point + contact.deepestNormalOnB *
                    std::max(markerSize * 2.0f, 0.03f * entity.scale),
                color
            );
        }
    }

    if (showFidelityBones)
    {
        const Skeleton& skeleton = this->pose->GetSkeleton();
        const std::span<const glm::mat4> finalGlobals =
            this->pose->GlobalMatrices();
        constexpr glm::vec3 BonePositionErrorColor{1.0f, 0.0f, 0.65f};
        constexpr std::size_t MaximumBoneAxes = 160U;
        const std::size_t invalidRuntimeIndex =
            std::numeric_limits<std::size_t>::max();
        const std::size_t drivenCount = static_cast<std::size_t>(std::count_if(
            this->drivenRuntimeBodyByBone.begin(),
            this->drivenRuntimeBodyByBone.end(),
            [invalidRuntimeIndex](std::size_t runtimeIndex)
            {
                return runtimeIndex != invalidRuntimeIndex;
            }
        ));
        const std::size_t stride = std::max<std::size_t>(
            1U,
            (drivenCount + MaximumBoneAxes - 1U) / MaximumBoneAxes
        );
        std::size_t drivenOrdinal = 0U;
        for (std::size_t boneIndex = 0U;
             boneIndex < this->drivenRuntimeBodyByBone.size();
             ++boneIndex)
        {
            const std::size_t runtimeIndex =
                this->drivenRuntimeBodyByBone[boneIndex];
            if (runtimeIndex == invalidRuntimeIndex)
                continue;
            const bool selected = drivenOrdinal % stride == 0U;
            ++drivenOrdinal;
            if (!selected)
                continue;

            const RuntimeBody& runtime = this->rigidBodies[runtimeIndex];
            const MmdRigidBodyDefinition& definition = *runtime.definition;
            const glm::mat4 finalBoneModel =
                skeleton.InverseRootMatrix() * finalGlobals[boneIndex];
            const float maximumSize = std::max({
                definition.size.x,
                definition.size.y,
                definition.size.z
            });
            const float axisLength = std::max(
                0.035f * entity.scale,
                0.18f * maximumSize * entity.scale
            );
            AppendTransformAxes(
                lines,
                finalBoneModel,
                entity,
                axisLength
            );

            const glm::mat4 currentBodyModel = WorldToModel(
                this->world->State(runtime.handle),
                entity
            );
            const glm::mat4 expectedBoneModel =
                currentBodyModel * definition.bodyToBone;
            const RigidTransform expected = ModelToWorld(
                expectedBoneModel,
                entity
            );
            const RigidTransform actual = ModelToWorld(
                finalBoneModel,
                entity
            );
            if (glm::distance(expected.position, actual.position) > 0.0005f)
            {
                AppendLine(
                    lines,
                    expected.position,
                    actual.position,
                    BonePositionErrorColor
                );
            }
        }
    }
}

void MmdPhysicsInstance::SetDebugOverlay(
    MmdPhysicsDebugOverlay overlay
) noexcept
{
    this->debugOverlay = overlay;
}

MmdPhysicsDebugOverlay MmdPhysicsInstance::DebugOverlay() const noexcept
{
    return this->debugOverlay;
}

MmdPhysicsDebugOverlay MmdPhysicsInstance::CycleDebugOverlay() noexcept
{
    switch (this->debugOverlay)
    {
    case MmdPhysicsDebugOverlay::Off:
        this->debugOverlay = MmdPhysicsDebugOverlay::BindPose;
        break;
    case MmdPhysicsDebugOverlay::BindPose:
        this->debugOverlay = MmdPhysicsDebugOverlay::ResetPose;
        break;
    case MmdPhysicsDebugOverlay::ResetPose:
        this->debugOverlay = MmdPhysicsDebugOverlay::Runtime;
        break;
    case MmdPhysicsDebugOverlay::Runtime:
        this->debugOverlay = MmdPhysicsDebugOverlay::All;
        break;
    case MmdPhysicsDebugOverlay::All:
        this->debugOverlay = MmdPhysicsDebugOverlay::Off;
        break;
    }
    return this->debugOverlay;
}

const char* MmdPhysicsInstance::DebugOverlayName() const noexcept
{
    return OverlayName(this->debugOverlay);
}

const MmdPhysicsAlignmentSummary&
MmdPhysicsInstance::AlignmentSummary() const noexcept
{
    return this->alignmentSummary;
}

const MmdPhysicsRecoveryStatistics&
MmdPhysicsInstance::RecoveryStatistics() const noexcept
{
    return this->recoveryStatistics;
}

void MmdPhysicsInstance::SetFidelityDebugLayer(
    MmdPhysicsFidelityDebugLayer layer
) noexcept
{
    this->fidelityDebugLayer = layer;
}

MmdPhysicsFidelityDebugLayer
MmdPhysicsInstance::FidelityDebugLayer() const noexcept
{
    return this->fidelityDebugLayer;
}

MmdPhysicsFidelityDebugLayer
MmdPhysicsInstance::CycleFidelityDebugLayer() noexcept
{
    switch (this->fidelityDebugLayer)
    {
    case MmdPhysicsFidelityDebugLayer::Off:
        this->fidelityDebugLayer = MmdPhysicsFidelityDebugLayer::Bone;
        break;
    case MmdPhysicsFidelityDebugLayer::Bone:
        this->fidelityDebugLayer = MmdPhysicsFidelityDebugLayer::Vertex;
        break;
    case MmdPhysicsFidelityDebugLayer::Vertex:
        this->fidelityDebugLayer = MmdPhysicsFidelityDebugLayer::All;
        break;
    case MmdPhysicsFidelityDebugLayer::All:
        this->fidelityDebugLayer = MmdPhysicsFidelityDebugLayer::Off;
        break;
    }
    return this->fidelityDebugLayer;
}

const char* MmdPhysicsInstance::FidelityDebugLayerName() const noexcept
{
    return ::FidelityDebugLayerName(this->fidelityDebugLayer);
}

void MmdPhysicsInstance::SetPhysicsWithBoneSyncMode(
    MmdPhysicsWithBoneSyncMode mode
) noexcept
{
    this->physicsWithBoneSyncMode = mode;
}

MmdPhysicsWithBoneSyncMode
MmdPhysicsInstance::PhysicsWithBoneSyncMode() const noexcept
{
    return this->physicsWithBoneSyncMode;
}

MmdPhysicsWithBoneSyncMode
MmdPhysicsInstance::CyclePhysicsWithBoneSyncMode() noexcept
{
    switch (this->physicsWithBoneSyncMode)
    {
    case MmdPhysicsWithBoneSyncMode::RotationOnly:
        this->physicsWithBoneSyncMode =
            MmdPhysicsWithBoneSyncMode::FullBody;
        break;
    case MmdPhysicsWithBoneSyncMode::FullBody:
        this->physicsWithBoneSyncMode =
            MmdPhysicsWithBoneSyncMode::TranslationDelta;
        break;
    case MmdPhysicsWithBoneSyncMode::TranslationDelta:
        this->physicsWithBoneSyncMode =
            MmdPhysicsWithBoneSyncMode::RotationOnly;
        break;
    }
    return this->physicsWithBoneSyncMode;
}

const char* MmdPhysicsInstance::PhysicsWithBoneSyncModeName() const noexcept
{
    return ::PhysicsWithBoneSyncModeName(this->physicsWithBoneSyncMode);
}

const MmdPhysicsFidelityStatistics&
MmdPhysicsInstance::FidelityStatistics() const noexcept
{
    return this->fidelityStatistics;
}

const MmdPhysicsCollisionStatistics&
MmdPhysicsInstance::CollisionStatistics() const noexcept
{
    return this->collisionStatistics;
}

std::span<const MmdPhysicsContactDiagnostic>
MmdPhysicsInstance::ContactDiagnostics() const noexcept
{
    return this->contactDiagnostics;
}

void MmdPhysicsInstance::SetGravityMode(MmdPhysicsGravityMode mode)
{
    switch (mode)
    {
    case MmdPhysicsGravityMode::Original:
    case MmdPhysicsGravityMode::Balanced100:
    case MmdPhysicsGravityMode::Balanced075:
    case MmdPhysicsGravityMode::Balanced050:
    case MmdPhysicsGravityMode::Balanced025:
    case MmdPhysicsGravityMode::Zero:
        break;
    default:
        throw std::invalid_argument("Unknown MMD gravity mode");
    }
    this->gravityMode = mode;
    this->ApplyGravityBalanceSettings(true);
}

MmdPhysicsGravityMode MmdPhysicsInstance::GravityMode() const noexcept
{
    return this->gravityMode;
}

MmdPhysicsGravityMode MmdPhysicsInstance::CycleGravityMode()
{
    switch (this->gravityMode)
    {
    case MmdPhysicsGravityMode::Original:
        this->SetGravityMode(MmdPhysicsGravityMode::Balanced100);
        break;
    case MmdPhysicsGravityMode::Balanced100:
        this->SetGravityMode(MmdPhysicsGravityMode::Balanced075);
        break;
    case MmdPhysicsGravityMode::Balanced075:
        this->SetGravityMode(MmdPhysicsGravityMode::Balanced050);
        break;
    case MmdPhysicsGravityMode::Balanced050:
        this->SetGravityMode(MmdPhysicsGravityMode::Balanced025);
        break;
    case MmdPhysicsGravityMode::Balanced025:
        this->SetGravityMode(MmdPhysicsGravityMode::Zero);
        break;
    case MmdPhysicsGravityMode::Zero:
        this->SetGravityMode(MmdPhysicsGravityMode::Original);
        break;
    }
    return this->gravityMode;
}

const char* MmdPhysicsInstance::GravityModeName() const noexcept
{
    switch (this->gravityMode)
    {
    case MmdPhysicsGravityMode::Original: return "ORIGINAL_1.00G";
    case MmdPhysicsGravityMode::Balanced100: return "BALANCED_1.00G";
    case MmdPhysicsGravityMode::Balanced075: return "BALANCED_0.75G";
    case MmdPhysicsGravityMode::Balanced050: return "BALANCED_0.50G";
    case MmdPhysicsGravityMode::Balanced025: return "BALANCED_0.25G";
    case MmdPhysicsGravityMode::Zero: return "ZERO_G";
    }
    return "UNKNOWN";
}

const MmdPhysicsGravityStatistics&
MmdPhysicsInstance::GravityStatistics() const noexcept
{
    return this->gravityStatistics;
}

std::span<const MmdPhysicsChainBalanceStatistics>
MmdPhysicsInstance::ChainBalanceStatistics() const noexcept
{
    return this->chainBalanceStatistics;
}

void MmdPhysicsInstance::LogGravityReport() const
{
    const MmdPhysicsGravityStatistics& summary = this->gravityStatistics;
    std::cout << "[MMD GRAVITY SUMMARY] mode=" << this->GravityModeName()
              << " globalScale=" << summary.globalGravityScale
              << " profiles="
              << (summary.chainProfilesEnabled ? "true" : "false")
              << " chains=" << summary.chainCount
              << " dynamicBodies=" << summary.dynamicBodyCount
              << " effectiveScaleMin=" << summary.minimumEffectiveGravityScale
              << " effectiveScaleAvg=" << summary.averageEffectiveGravityScale
              << " effectiveScaleMax=" << summary.maximumEffectiveGravityScale
              << " downAvg=" << summary.averageDownwardDisplacement
              << " downMax=" << summary.maximumDownwardDisplacement
              << " speedAvg=" << summary.averageSpeed
              << " speedMax=" << summary.maximumSpeed
              << " contactImpulse=" << summary.totalContactImpulse
              << " constraintImpulse=" << summary.totalConstraintImpulse
              << " constraintImpulseMax="
              << summary.maximumConstraintImpulse
              << " extensionMax=" << summary.maximumNormalizedExtension
              << " anchorSpeedMax=" << summary.maximumAnchorLinearSpeed
              << " mode2DeltaMax=" << summary.maximumMode2TranslationDelta
              << " skirtLayerIgnoredPairs="
              << summary.skirtLayerIgnoredPairCount
              << " skirtSemanticIgnoredPairs="
              << summary.skirtSemanticIgnoredPairCount
              << std::endl;

    std::vector<MmdPhysicsChainBalanceStatistics> ordered(
        this->chainBalanceStatistics.begin(),
        this->chainBalanceStatistics.end()
    );
    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const MmdPhysicsChainBalanceStatistics& left,
           const MmdPhysicsChainBalanceStatistics& right)
        {
            const float leftActivity = std::max(
                left.contactImpulse,
                left.totalConstraintImpulse
            );
            const float rightActivity = std::max(
                right.contactImpulse,
                right.totalConstraintImpulse
            );
            if (leftActivity != rightActivity)
                return leftActivity > rightActivity;
            if (left.maximumNormalizedExtension !=
                right.maximumNormalizedExtension)
            {
                return left.maximumNormalizedExtension >
                    right.maximumNormalizedExtension;
            }
            return left.maximumDownwardDisplacement >
                right.maximumDownwardDisplacement;
        }
    );
    for (const MmdPhysicsChainBalanceStatistics& chain : ordered)
    {
        std::cout << "[MMD GRAVITY CHAIN] chain=" << chain.chainIndex
                  << " kind=" << ChainKindName(chain.kind)
                  << " bodies=" << chain.bodyCount
                  << " dynamic=" << chain.dynamicBodyCount
                  << " mass=" << chain.totalMass
                  << " gravityScale=" << chain.gravityScale
                  << " effectiveScale=" << chain.effectiveGravityScale
                  << " damping=" << chain.minimumLinearDamping << '/'
                  << chain.minimumAngularDamping
                  << " downAvg=" << chain.averageDownwardDisplacement
                  << " downMax=" << chain.maximumDownwardDisplacement
                  << " speedAvg=" << chain.averageSpeed
                  << " speedMax=" << chain.maximumSpeed
                  << " pairs=" << chain.contactPairCount
                  << " impulse=" << chain.contactImpulse
                  << " constraintImpulse="
                  << chain.totalConstraintImpulse
                  << " constraintImpulseMax="
                  << chain.maximumConstraintImpulse
                  << " anchorBody=" << chain.anchorBodyIndex
                  << " anchorSpeed=" << chain.anchorLinearSpeed
                  << " anchorAngularSpeed=" << chain.anchorAngularSpeed
                  << " anchorDistanceAvg=" << chain.averageAnchorDistance
                  << " anchorDistanceMax=" << chain.maximumAnchorDistance
                  << " extensionAvg=" << chain.averageNormalizedExtension
                  << " extensionMax=" << chain.maximumNormalizedExtension
                  << " mode2DeltaMax="
                  << chain.maximumMode2TranslationDelta
                  << std::endl;
    }
}

void MmdPhysicsInstance::LogCollisionReport(
    std::size_t maximumEntries
) const
{
    std::cout << "[MMD COLLISION SUMMARY] linkedPairs="
              << this->collisionStatistics.linkedJointPairCount
              << " ignoredNearPairs="
              << this->collisionStatistics.ignoredNearNeighborPairCount
              << " denseMarginBodies="
              << this->collisionStatistics.denseMarginBodyCount
              << " ccdCandidates="
              << this->collisionStatistics.ccdCandidateCount
              << " ccdActive="
              << this->collisionStatistics.activeCcdBodyCount
              << " pairs=" << this->collisionStatistics.contactPairCount
              << " sameChainPairs="
              << this->collisionStatistics.sameChainContactPairCount
              << " crossChainPairs="
              << this->collisionStatistics.crossChainContactPairCount
              << " points=" << this->collisionStatistics.contactPointCount
              << " maxPenetration="
              << this->collisionStatistics.maximumPenetrationDepth
              << " totalImpulse="
              << this->collisionStatistics.totalAppliedImpulse
              << " maxPairImpulse="
              << this->collisionStatistics.maximumPairImpulse
              << std::endl;

    struct ChainPairSummary
    {
        std::size_t chainA = std::numeric_limits<std::size_t>::max();
        std::size_t chainB = std::numeric_limits<std::size_t>::max();
        std::size_t pairCount = 0U;
        std::size_t pointCount = 0U;
        float impulse = 0.0f;
        float penetration = 0.0f;
    };
    std::vector<ChainPairSummary> matrix;
    for (const MmdPhysicsContactDiagnostic& contact : this->contactDiagnostics)
    {
        const std::size_t lower = std::min(
            contact.chainAIndex,
            contact.chainBIndex
        );
        const std::size_t upper = std::max(
            contact.chainAIndex,
            contact.chainBIndex
        );
        auto iterator = std::find_if(
            matrix.begin(),
            matrix.end(),
            [lower, upper](const ChainPairSummary& entry)
            {
                return entry.chainA == lower && entry.chainB == upper;
            }
        );
        if (iterator == matrix.end())
        {
            matrix.push_back(ChainPairSummary{lower, upper});
            iterator = std::prev(matrix.end());
        }
        ++iterator->pairCount;
        iterator->pointCount += contact.contactPointCount;
        iterator->impulse += contact.totalAppliedImpulse;
        iterator->penetration = std::max(
            iterator->penetration,
            contact.maximumPenetrationDepth
        );
    }
    std::sort(
        matrix.begin(),
        matrix.end(),
        [](const ChainPairSummary& left, const ChainPairSummary& right)
        {
            return left.impulse > right.impulse;
        }
    );
    for (const ChainPairSummary& entry : matrix)
    {
        std::cout << "[MMD COLLISION MATRIX] chainA=" << entry.chainA
                  << " chainB=" << entry.chainB
                  << " pairs=" << entry.pairCount
                  << " points=" << entry.pointCount
                  << " impulse=" << entry.impulse
                  << " maxPenetration=" << entry.penetration
                  << std::endl;
    }

    const std::size_t count = std::min(
        maximumEntries,
        this->contactDiagnostics.size()
    );
    for (std::size_t rank = 0U; rank < count; ++rank)
    {
        const MmdPhysicsContactDiagnostic& contact =
            this->contactDiagnostics[rank];
        const MmdRigidBodyDefinition& bodyA =
            this->asset->RigidBodyAt(contact.bodyAIndex);
        const MmdRigidBodyDefinition& bodyB =
            this->asset->RigidBodyAt(contact.bodyBIndex);
        const PhysicsBodyRuntimeSettings settingsA = this->world->RuntimeSettings(
            this->rigidBodies[contact.bodyAIndex].handle
        );
        const PhysicsBodyRuntimeSettings settingsB = this->world->RuntimeSettings(
            this->rigidBodies[contact.bodyBIndex].handle
        );
        const bool linked = std::any_of(
            this->recoveryAdjacency[contact.bodyAIndex].begin(),
            this->recoveryAdjacency[contact.bodyAIndex].end(),
            [&contact](const RecoveryEdge& edge)
            {
                return edge.bodyIndex == contact.bodyBIndex;
            }
        );
        std::cout << "[MMD CONTACT] rank=" << rank
                  << " bodyA=" << contact.bodyAIndex << ":\""
                  << bodyA.name << "\""
                  << " bodyB=" << contact.bodyBIndex << ":\""
                  << bodyB.name << "\""
                  << " chainA=" << contact.chainAIndex
                  << " chainB=" << contact.chainBIndex
                  << " linked=" << (linked ? "true" : "false")
                  << " groupA=0x" << std::hex << settingsA.collisionGroup
                  << " maskA=0x" << settingsA.collisionMask
                  << " groupB=0x" << settingsB.collisionGroup
                  << " maskB=0x" << settingsB.collisionMask << std::dec
                  << " points=" << contact.contactPointCount
                  << " penetration=" << contact.maximumPenetrationDepth
                  << " totalImpulse=" << contact.totalAppliedImpulse
                  << " maxImpulse=" << contact.maximumAppliedImpulse
                  << std::endl;
    }
}

std::span<const std::uint8_t>
MmdPhysicsInstance::DrivenBoneModes() const noexcept
{
    return this->drivenBoneModes;
}

void MmdPhysicsInstance::SetSampledVertexCount(std::size_t count) noexcept
{
    this->fidelityStatistics.sampledVertexCount = count;
}

bool MmdPhysicsInstance::StabilizationFailed() const noexcept
{
    return this->stabilizationFailed;
}

std::size_t MmdPhysicsInstance::PendingStabilizationSteps() const noexcept
{
    return this->pendingStabilizationSteps;
}

void MmdPhysicsInstance::BuildAlignmentDiagnostics()
{
    this->alignmentRecords.clear();
    this->alignmentRecords.reserve(this->rigidBodies.size());
    this->alignmentSummary = {};
    this->alignmentSummary.bodyCount = this->rigidBodies.size();
    this->alignmentSummary.jointCount = this->asset->JointCount();

    const Skeleton& skeleton = this->pose->GetSkeleton();
    const glm::mat4 identity(1.0f);
    this->alignmentSummary.nonIdentityBindSpace =
        MatrixMaximumDifference(
            skeleton.InverseRootMatrix(),
            identity
        ) > 0.0001f;

    for (std::size_t index = 0U; index < skeleton.BoneCount(); ++index)
    {
        const Bone& bone = skeleton.BoneAt(
            static_cast<BoneIndex>(index)
        );
        const glm::mat4 skinningBind = skeleton.InverseRootMatrix() *
            skeleton.BindGlobalMatrices()[index] * bone.inverseBindMatrix;
        this->alignmentSummary.maximumSkinningBindError = std::max(
            this->alignmentSummary.maximumSkinningBindError,
            MatrixMaximumDifference(skinningBind, identity)
        );
    }

    for (std::size_t index = 0U; index < this->rigidBodies.size(); ++index)
    {
        const RuntimeBody& runtime = this->rigidBodies[index];
        const MmdRigidBodyDefinition& definition = *runtime.definition;
        AlignmentRecord record;
        record.bodyIndex = index;
        record.sourceBindModel = definition.modelBindTransform;
        record.skeletonBindModel = definition.modelBindTransform;
        if (definition.bone != InvalidBoneIndex)
        {
            record.skeletonBindModel = skeleton.InverseRootMatrix() *
                skeleton.BindGlobalMatrices()[definition.bone] *
                definition.boneToBody;
        }
        record.createdBulletBindModel =
            runtime.createdBulletBindModelTransform;
        record.resetTargetModel = runtime.resetTargetModelTransform;
        record.postResetBulletModel =
            runtime.postResetBulletModelTransform;
        std::tie(
            record.bindPositionError,
            record.bindRotationErrorDegrees
        ) = TransformError(
            record.sourceBindModel,
            record.skeletonBindModel
        );
        std::tie(
            record.bulletPositionError,
            record.bulletRotationErrorDegrees
        ) = TransformError(
            record.sourceBindModel,
            record.createdBulletBindModel
        );
        std::tie(
            record.postResetPositionError,
            record.postResetRotationErrorDegrees
        ) = TransformError(
            record.resetTargetModel,
            record.postResetBulletModel
        );
        this->alignmentSummary.maximumBindPositionError = std::max(
            this->alignmentSummary.maximumBindPositionError,
            record.bindPositionError
        );
        this->alignmentSummary.maximumBindRotationErrorDegrees = std::max(
            this->alignmentSummary.maximumBindRotationErrorDegrees,
            record.bindRotationErrorDegrees
        );
        this->alignmentSummary.maximumBulletPositionError = std::max(
            this->alignmentSummary.maximumBulletPositionError,
            record.bulletPositionError
        );
        this->alignmentSummary.maximumBulletRotationErrorDegrees = std::max(
            this->alignmentSummary.maximumBulletRotationErrorDegrees,
            record.bulletRotationErrorDegrees
        );
        this->alignmentSummary.maximumPostResetPositionError = std::max(
            this->alignmentSummary.maximumPostResetPositionError,
            record.postResetPositionError
        );
        this->alignmentSummary.maximumPostResetRotationErrorDegrees = std::max(
            this->alignmentSummary.maximumPostResetRotationErrorDegrees,
            record.postResetRotationErrorDegrees
        );
        this->alignmentRecords.push_back(record);
    }
}

void MmdPhysicsInstance::LogAlignmentSummary() const
{
    std::cout
        << "[MMD ALIGN] bodies=" << this->alignmentSummary.bodyCount
        << " joints=" << this->alignmentSummary.jointCount
        << " bindSpace="
        << (this->alignmentSummary.nonIdentityBindSpace
            ? "non-identity" : "identity")
        << " skinBindMax="
        << this->alignmentSummary.maximumSkinningBindError
        << " bindPosMax="
        << this->alignmentSummary.maximumBindPositionError
        << " bindRotMaxDeg="
        << this->alignmentSummary.maximumBindRotationErrorDegrees
        << " createBulletPosMax="
        << this->alignmentSummary.maximumBulletPositionError
        << " createBulletRotMaxDeg="
        << this->alignmentSummary.maximumBulletRotationErrorDegrees
        << " postResetPosMax="
        << this->alignmentSummary.maximumPostResetPositionError
        << " postResetRotMaxDeg="
        << this->alignmentSummary.maximumPostResetRotationErrorDegrees
        << std::endl;
}

void MmdPhysicsInstance::LogAlignmentReport(
    std::size_t maximumEntries
) const
{
    const Skeleton& skeleton = this->pose->GetSkeleton();
    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    const std::size_t bodyCount = this->rigidBodies.size();
    std::vector<float> mappingPositionErrors(bodyCount, 0.0f);
    std::vector<float> mappingRotationErrors(bodyCount, 0.0f);
    std::vector<float> runtimeTargetPositionErrors(bodyCount, 0.0f);
    std::vector<float> runtimeTargetRotationErrors(bodyCount, 0.0f);
    std::vector<glm::mat4> currentBodyModels;
    currentBodyModels.reserve(bodyCount);

    std::array<float, 3U> maximumRuntimePositionByMode{};
    std::array<float, 3U> maximumRuntimeRotationByMode{};
    std::array<std::size_t, 3U> runtimeDriftCountByMode{};
    constexpr float RuntimeDriftThreshold = 0.05f;

    for (std::size_t index = 0U; index < bodyCount; ++index)
    {
        const RuntimeBody& runtime = this->rigidBodies[index];
        const MmdRigidBodyDefinition& definition = *runtime.definition;
        const glm::mat4 currentBodyModel = WorldToModel(
            this->world->State(runtime.handle),
            entity
        );
        currentBodyModels.push_back(currentBodyModel);

        if (definition.bone != InvalidBoneIndex)
        {
            const glm::mat4& bindGlobal =
                skeleton.BindGlobalMatrices()[definition.bone];
            const glm::mat4 legacyOffset = glm::inverse(bindGlobal) *
                definition.modelBindTransform;
            const glm::mat4 legacyTarget =
                this->pose->GlobalMatrix(definition.bone) * legacyOffset;
            const glm::mat4 modelTarget = AnimatedBodyModelTransform(
                definition,
                *this->pose
            );
            std::tie(
                mappingPositionErrors[index],
                mappingRotationErrors[index]
            ) = TransformError(legacyTarget, modelTarget);
        }
        std::tie(
            runtimeTargetPositionErrors[index],
            runtimeTargetRotationErrors[index]
        ) = TransformError(
            currentBodyModel,
            runtime.prePhysicsAnimatedModelTransform
        );

        const std::size_t mode = static_cast<std::size_t>(definition.mode);
        maximumRuntimePositionByMode[mode] = std::max(
            maximumRuntimePositionByMode[mode],
            runtimeTargetPositionErrors[index]
        );
        maximumRuntimeRotationByMode[mode] = std::max(
            maximumRuntimeRotationByMode[mode],
            runtimeTargetRotationErrors[index]
        );
        if (runtimeTargetPositionErrors[index] > RuntimeDriftThreshold)
            ++runtimeDriftCountByMode[mode];
    }

    std::vector<std::size_t> order(bodyCount);
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(
        order.begin(),
        order.end(),
        [this,
         &mappingPositionErrors,
         &mappingRotationErrors,
         &runtimeTargetPositionErrors,
         &runtimeTargetRotationErrors](
            std::size_t left,
            std::size_t right
        )
        {
            const AlignmentRecord& leftRecord = this->alignmentRecords[left];
            const AlignmentRecord& rightRecord = this->alignmentRecords[right];
            const float leftScore = std::max({
                leftRecord.bindPositionError,
                leftRecord.bulletPositionError,
                leftRecord.postResetPositionError,
                mappingPositionErrors[left],
                mappingRotationErrors[left] * 0.01f,
                runtimeTargetPositionErrors[left],
                runtimeTargetRotationErrors[left] * 0.01f
            });
            const float rightScore = std::max({
                rightRecord.bindPositionError,
                rightRecord.bulletPositionError,
                rightRecord.postResetPositionError,
                mappingPositionErrors[right],
                mappingRotationErrors[right] * 0.01f,
                runtimeTargetPositionErrors[right],
                runtimeTargetRotationErrors[right] * 0.01f
            });
            return leftScore > rightScore;
        }
    );

    const JointSnapshot currentJoint = this->CaptureJointSnapshot("current");
    const std::ios::fmtflags previousFlags = std::cout.flags();
    const std::streamsize previousPrecision = std::cout.precision();
    std::cout << std::fixed << std::setprecision(5);
    std::cout << "[MMD ALIGN REPORT] overlay="
              << this->DebugOverlayName()
              << " inverseRoot=";
    const RigidTransform inverseRoot = ExtractRigidTransform(
        skeleton.InverseRootMatrix()
    );
    std::cout << "pos(" << inverseRoot.position.x << ", "
              << inverseRoot.position.y << ", "
              << inverseRoot.position.z << ") rot("
              << inverseRoot.rotation.w << ", "
              << inverseRoot.rotation.x << ", "
              << inverseRoot.rotation.y << ", "
              << inverseRoot.rotation.z << ")\n";
    this->LogAlignmentSummary();
    for (const JointSnapshot& snapshot : this->jointSnapshots)
        this->LogJointSnapshot(snapshot);
    this->LogJointSnapshot(currentJoint);
    std::cout << "[MMD ALIGN REPORT] stabilization="
              << (this->stabilizationFailed ? "FAILED_SAFE_FREEZE" : "active")
              << '\n';
    for (std::size_t mode = 0U; mode < maximumRuntimePositionByMode.size(); ++mode)
    {
        std::cout << "[MMD ALIGN MODE] mode="
                  << RigidBodyModeName(static_cast<MmdRigidBodyMode>(mode))
                  << " currentVsPrePhysicsPosMax="
                  << maximumRuntimePositionByMode[mode]
                  << " currentVsPrePhysicsRotDegMax="
                  << maximumRuntimeRotationByMode[mode]
                  << " bodiesOver" << RuntimeDriftThreshold << "="
                  << runtimeDriftCountByMode[mode]
                  << '\n';
    }
    std::cout << "[MMD ALIGN REPORT] colors: "
                 "cyan=PMX source bind, blue=CreateBody bind, "
                 "yellow=reset target, magenta=post-reset Bullet, "
                 "green=pre-physics target, white=current Bullet, "
                 "red=position discrepancy\n";

    const std::size_t count = std::min(maximumEntries, order.size());
    for (std::size_t rank = 0U; rank < count; ++rank)
    {
        const std::size_t index = order[rank];
        const MmdRigidBodyDefinition& definition =
            *this->rigidBodies[index].definition;
        const AlignmentRecord& record = this->alignmentRecords[index];
        const RigidTransform source = ExtractRigidTransform(
            definition.modelBindTransform
        );
        const RigidTransform current = ExtractRigidTransform(
            currentBodyModels[index]
        );
        std::cout << "[MMD ALIGN BODY] rank=" << rank
                  << " index=" << index
                  << " name=\"" << definition.name << "\""
                  << " mode=" << RigidBodyModeName(definition.mode)
                  << " shape=" << RigidBodyShapeName(definition.shape)
                  << " size=(" << definition.size.x << ','
                  << definition.size.y << ',' << definition.size.z << ')'
                  << " bone=";
        if (definition.bone == InvalidBoneIndex)
            std::cout << "<none>";
        else
            std::cout << '\"' << skeleton.BoneAt(definition.bone).name << '\"';
        std::cout << " sourcePos=(" << source.position.x << ','
                  << source.position.y << ',' << source.position.z << ')'
                  << " currentPos=(" << current.position.x << ','
                  << current.position.y << ',' << current.position.z << ')'
                  << " bindPos=" << record.bindPositionError
                  << " bindRotDeg=" << record.bindRotationErrorDegrees
                  << " createBulletPos=" << record.bulletPositionError
                  << " createBulletRotDeg="
                  << record.bulletRotationErrorDegrees
                  << " postResetPos=" << record.postResetPositionError
                  << " postResetRotDeg="
                  << record.postResetRotationErrorDegrees
                  << " legacyVsModelPos=" << mappingPositionErrors[index]
                  << " legacyVsModelRotDeg=" << mappingRotationErrors[index]
                  << " currentVsPrePhysicsPos="
                  << runtimeTargetPositionErrors[index]
                  << " currentVsPrePhysicsRotDeg="
                  << runtimeTargetRotationErrors[index]
                  << '\n';
    }
    std::cout.flags(previousFlags);
    std::cout.precision(previousPrecision);
}

std::size_t MmdPhysicsInstance::RigidBodyCount() const noexcept
{
    return this->rigidBodies.size();
}

std::size_t MmdPhysicsInstance::ConstraintCount() const noexcept
{
    return this->constraints.size();
}

PhysicsBodyHandle MmdPhysicsInstance::BodyHandleAt(
    RigidBodyIndex index
) const
{
    if (static_cast<std::size_t>(index) >= this->rigidBodies.size())
        throw std::out_of_range("MMD runtime rigid-body index is out of range");
    return this->rigidBodies[index].handle;
}

PhysicsBodyState MmdPhysicsInstance::BodyStateAt(RigidBodyIndex index) const
{
    return this->world->State(this->BodyHandleAt(index));
}

void MmdPhysicsInstance::ApplyCentralImpulse(
    RigidBodyIndex index,
    const glm::vec3& impulse
)
{
    this->world->ApplyCentralImpulse(this->BodyHandleAt(index), impulse);
}

void MmdPhysicsInstance::ApplyTorqueImpulse(
    RigidBodyIndex index,
    const glm::vec3& impulse
)
{
    this->world->ApplyTorqueImpulse(this->BodyHandleAt(index), impulse);
}

void MmdPhysicsInstance::ApplyImpulseMorphs(const MorphState& morphState)
{
    morphState.EvaluateImpulseMorphs(this->impulseScratch);

    // PMX reset controls are processed first so an impulse in the same frame
    // starts from a deterministic zero-velocity state.
    for (const MmdRigidBodyImpulse& command : this->impulseScratch)
    {
        if (command.reset)
            this->world->ClearDynamics(this->BodyHandleAt(command.rigidBodyIndex));
    }

    for (const MmdRigidBodyImpulse& command : this->impulseScratch)
    {
        const PhysicsBodyHandle body = this->BodyHandleAt(
            command.rigidBodyIndex
        );
        const PhysicsBodyState state = this->world->State(body);
        const glm::vec3 linearImpulse = command.globalLinearImpulse +
            state.rotation * command.localLinearImpulse;
        const glm::vec3 torqueImpulse = command.globalTorqueImpulse +
            state.rotation * command.localTorqueImpulse;
        if (linearImpulse != glm::vec3(0.0f))
            this->world->ApplyCentralImpulse(body, linearImpulse);
        if (torqueImpulse != glm::vec3(0.0f))
            this->world->ApplyTorqueImpulse(body, torqueImpulse);
    }
}

void MmdPhysicsInstance::PrePhysicsUpdate(
    const Transform& transform,
    float deltaTime
)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
    {
        throw std::invalid_argument(
            "MMD physics delta time must be finite and non-negative"
        );
    }
    (void)deltaTime;
    const EntityFrame entity = ExtractEntityFrame(transform);

    for (RuntimeBody& runtime : this->rigidBodies)
    {
        runtime.prePhysicsAnimatedModelTransform =
            AnimatedBodyModelTransform(*runtime.definition, *this->pose);
    }

    // A model may be instantiated in bind pose and receive its first VMD clip
    // before the first Scene update. Refresh the true animation targets and
    // rebuild a constraint-preserving reset pose immediately before hidden
    // settling, rather than warming stale constructor-time data or teleporting
    // every dynamic body independently.
    if (this->resetTargetRefreshPending)
    {
        this->CaptureConstraintPreservingResetTargets();
        this->ApplyResetTargets(transform);
        if (!this->jointSnapshots.empty() &&
            std::string_view(this->jointSnapshots.back().stage) ==
                "after-reset")
        {
            this->jointSnapshots.pop_back();
        }
        const JointSnapshot refreshed = this->CaptureJointSnapshot(
            "pre-warmup-reset"
        );
        this->jointSnapshots.push_back(refreshed);
        this->resetTargetRefreshPending = false;
        const bool needsWarmup = !refreshed.finite ||
            refreshed.maximumStabilizationLinearViolation >
                JointWarmupLinearViolation ||
            refreshed.maximumStabilizationAngularViolationDegrees >
                JointWarmupAngularViolationDegrees;
        this->pendingStabilizationSteps = needsWarmup
            ? MmdStabilizationSteps
            : 0U;
        this->BuildAlignmentDiagnostics();
        if (this->rigidBodies.size() >= 32U)
        {
            this->LogJointSnapshot(refreshed);
            std::cout << "[MMD INIT] warmup="
                      << (needsWarmup ? "requested" : "not_required")
                      << " maxLinearViolation="
                      << refreshed.maximumStabilizationLinearViolation
                      << " maxAngularViolationDeg="
                      << refreshed.maximumStabilizationAngularViolationDegrees
                      << std::endl;
        }
        return;
    }

    for (RuntimeBody& runtime : this->rigidBodies)
    {
        const MmdRigidBodyDefinition& definition = *runtime.definition;
        const RigidTransform animated = ModelToWorld(
            runtime.prePhysicsAnimatedModelTransform,
            entity
        );

        const bool driveBody = this->stabilizationFailed ||
            definition.mode == MmdRigidBodyMode::FollowBone;
        if (!driveBody)
            continue;

        if (!runtime.hasAnimatedTransform)
        {
            runtime.lastAnimatedPosition = animated.position;
            runtime.lastAnimatedRotation = animated.rotation;
            runtime.frameStartAnimatedPosition = animated.position;
            runtime.frameStartAnimatedRotation = animated.rotation;
            runtime.frameTargetAnimatedPosition = animated.position;
            runtime.frameTargetAnimatedRotation = animated.rotation;
            runtime.hasAnimatedTransform = true;
        }
        runtime.frameStartAnimatedPosition =
            runtime.frameTargetAnimatedPosition;
        runtime.frameStartAnimatedRotation =
            runtime.frameTargetAnimatedRotation;
        runtime.frameTargetAnimatedPosition = animated.position;
        runtime.frameTargetAnimatedRotation = animated.rotation;
    }
}

void MmdPhysicsInstance::PostPhysicsUpdate(const Transform& transform)
{
    if (this->stabilizationFailed)
        return;
    const EntityFrame entity = ExtractEntityFrame(transform);
    const Skeleton& skeleton = this->pose->GetSkeleton();
    const std::span<const glm::mat4> currentLocals =
        this->pose->LocalMatrices();
    const std::span<const glm::mat4> currentGlobals =
        this->pose->GlobalMatrices();
    std::copy(
        currentLocals.begin(),
        currentLocals.end(),
        this->localMatrixScratch.begin()
    );
    std::copy(
        currentGlobals.begin(),
        currentGlobals.end(),
        this->globalMatrixScratch.begin()
    );

    const std::size_t invalidRuntimeIndex =
        std::numeric_limits<std::size_t>::max();
    for (BoneIndex boneIndex : skeleton.EvaluationOrder())
    {
        const Bone& bone = skeleton.BoneAt(boneIndex);
        const glm::mat4 parentGlobal = bone.parentIndex == InvalidBoneIndex
            ? glm::mat4(1.0f)
            : this->globalMatrixScratch[bone.parentIndex];
        const std::size_t runtimeIndex =
            this->drivenRuntimeBodyByBone[boneIndex];
        if (runtimeIndex == invalidRuntimeIndex)
        {
            this->globalMatrixScratch[boneIndex] =
                parentGlobal * this->localMatrixScratch[boneIndex];
            continue;
        }

        const RuntimeBody& runtime = this->rigidBodies[runtimeIndex];
        const MmdRigidBodyDefinition& definition = *runtime.definition;
        const glm::mat4 bodyModel = WorldToModel(
            this->world->State(runtime.handle),
            entity
        );
        const glm::mat4 drivenModel = bodyModel * definition.bodyToBone;
        // Pose globals are stored in skeleton-root space. Convert the MMD
        // model-space bone result back before writing it into the Pose.
        const glm::mat4 drivenGlobal =
            glm::inverse(skeleton.InverseRootMatrix()) * drivenModel;
        if (definition.mode == MmdRigidBodyMode::PhysicsWithBone)
        {
            // PMX mode 2 is intentionally selectable because real-world PMX
            // files rely on different interpretations of "physics with bone".
            // The Bullet body always remains fully dynamic; only the transform
            // written back into Pose changes here.
            const BoneTransform animatedBone = BoneTransform::FromMatrix(
                currentGlobals[boneIndex]
            );
            const BoneTransform physicsBone = BoneTransform::FromMatrix(
                drivenGlobal
            );

            switch (this->physicsWithBoneSyncMode)
            {
            case MmdPhysicsWithBoneSyncMode::RotationOnly:
            {
                BoneTransform result = animatedBone;
                result.rotation = physicsBone.rotation;
                this->globalMatrixScratch[boneIndex] = result.Matrix();
                break;
            }
            case MmdPhysicsWithBoneSyncMode::FullBody:
                this->globalMatrixScratch[boneIndex] = drivenGlobal;
                break;
            case MmdPhysicsWithBoneSyncMode::TranslationDelta:
            {
                // Apply only Bullet's displacement relative to the animation
                // target submitted for this body. This preserves the authored
                // animation/append-transform base while allowing hair and
                // clothing vertices to follow the body's actual translation.
                const glm::vec3 bodyTranslationDeltaModel =
                    glm::vec3(bodyModel[3]) -
                    glm::vec3(runtime.prePhysicsAnimatedModelTransform[3]);
                const glm::mat3 modelToSkeletonRoot(
                    glm::inverse(skeleton.InverseRootMatrix())
                );
                BoneTransform result = animatedBone;
                result.translation +=
                    modelToSkeletonRoot * bodyTranslationDeltaModel;
                result.rotation = physicsBone.rotation;
                this->globalMatrixScratch[boneIndex] = result.Matrix();
                break;
            }
            }
        }
        else
        {
            this->globalMatrixScratch[boneIndex] = drivenGlobal;
        }
        this->localMatrixScratch[boneIndex] =
            glm::inverse(parentGlobal) * this->globalMatrixScratch[boneIndex];
    }

    this->pose->SetLocalMatrices(this->localMatrixScratch);
    this->UpdateFidelityStatistics();
}

void MmdPhysicsInstance::UpdateFidelityStatistics()
{
    const std::size_t sampledVertexCount =
        this->fidelityStatistics.sampledVertexCount;
    this->fidelityStatistics = {};
    this->fidelityStatistics.sampledVertexCount = sampledVertexCount;

    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    const Skeleton& skeleton = this->pose->GetSkeleton();
    const std::span<const glm::mat4> finalGlobals =
        this->pose->GlobalMatrices();

    float positionErrorSum = 0.0f;
    float rotationErrorSum = 0.0f;
    float translationDeltaSum = 0.0f;
    std::size_t translationDeltaCount = 0U;

    const std::size_t invalidRuntimeIndex =
        std::numeric_limits<std::size_t>::max();
    for (std::size_t boneIndex = 0U;
         boneIndex < this->drivenRuntimeBodyByBone.size();
         ++boneIndex)
    {
        const std::size_t runtimeIndex =
            this->drivenRuntimeBodyByBone[boneIndex];
        if (runtimeIndex == invalidRuntimeIndex)
            continue;
        const RuntimeBody& runtime = this->rigidBodies[runtimeIndex];
        const MmdRigidBodyDefinition& definition = *runtime.definition;

        const glm::mat4 bodyModel = WorldToModel(
            this->world->State(runtime.handle),
            entity
        );
        const glm::mat4 expectedBoneModel =
            bodyModel * definition.bodyToBone;
        const glm::mat4 expectedBoneGlobal =
            glm::inverse(skeleton.InverseRootMatrix()) * expectedBoneModel;
        const glm::mat4 actualBoneGlobal = finalGlobals[boneIndex];
        const auto [positionError, rotationError] = TransformError(
            expectedBoneGlobal,
            actualBoneGlobal
        );

        ++this->fidelityStatistics.drivenBoneCount;
        positionErrorSum += positionError;
        rotationErrorSum += rotationError;
        this->fidelityStatistics.maximumBulletToBonePositionError = std::max(
            this->fidelityStatistics.maximumBulletToBonePositionError,
            positionError
        );
        this->fidelityStatistics.maximumBulletToBoneRotationErrorDegrees =
            std::max(
                this->fidelityStatistics.maximumBulletToBoneRotationErrorDegrees,
                rotationError
            );

        if (definition.mode == MmdRigidBodyMode::PhysicsWithBone)
        {
            ++this->fidelityStatistics.physicsWithBoneCount;
            const float delta = glm::distance(
                glm::vec3(bodyModel[3]),
                glm::vec3(runtime.prePhysicsAnimatedModelTransform[3])
            );
            translationDeltaSum += delta;
            ++translationDeltaCount;
            this->fidelityStatistics.maximumMode2TranslationDelta = std::max(
                this->fidelityStatistics.maximumMode2TranslationDelta,
                delta
            );
        }
    }

    if (this->fidelityStatistics.drivenBoneCount > 0U)
    {
        const float inverseCount = 1.0f /
            static_cast<float>(this->fidelityStatistics.drivenBoneCount);
        this->fidelityStatistics.averageBulletToBonePositionError =
            positionErrorSum * inverseCount;
        this->fidelityStatistics.averageBulletToBoneRotationErrorDegrees =
            rotationErrorSum * inverseCount;
    }
    if (translationDeltaCount > 0U)
    {
        this->fidelityStatistics.averageMode2TranslationDelta =
            translationDeltaSum / static_cast<float>(translationDeltaCount);
    }
}

void MmdPhysicsInstance::CaptureConstraintPreservingResetTargets()
{
    const std::size_t bodyCount = this->rigidBodies.size();
    std::vector<std::vector<std::size_t>> adjacency(bodyCount);
    for (const MmdJointDefinition& joint : this->asset->Joints())
    {
        if (joint.bodyA == InvalidRigidBodyIndex ||
            joint.bodyB == InvalidRigidBodyIndex ||
            joint.bodyA == joint.bodyB ||
            static_cast<std::size_t>(joint.bodyA) >= bodyCount ||
            static_cast<std::size_t>(joint.bodyB) >= bodyCount)
        {
            continue;
        }
        if (!IsWideTravelHelperJoint(joint))
        {
            adjacency[joint.bodyA].push_back(joint.bodyB);
            adjacency[joint.bodyB].push_back(joint.bodyA);
        }
    }

    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    std::vector<bool> visited(bodyCount, false);
    std::vector<std::size_t> owner(bodyCount, invalidIndex);
    for (std::size_t start = 0U; start < bodyCount; ++start)
    {
        if (visited[start])
            continue;
        std::vector<std::size_t> component;
        std::vector<std::size_t> animatedAnchors;
        std::queue<std::size_t> discover;
        discover.push(start);
        visited[start] = true;
        while (!discover.empty())
        {
            const std::size_t index = discover.front();
            discover.pop();
            component.push_back(index);
            if (this->rigidBodies[index].definition->mode ==
                MmdRigidBodyMode::FollowBone)
            {
                animatedAnchors.push_back(index);
            }
            for (const std::size_t neighbor : adjacency[index])
            {
                if (!visited[neighbor])
                {
                    visited[neighbor] = true;
                    discover.push(neighbor);
                }
            }
        }

        if (animatedAnchors.empty())
        {
            for (const std::size_t index : component)
                owner[index] = start;
        }
        else
        {
            // Multi-source BFS assigns every dynamic body to its nearest
            // animation-driven collision body. This preserves each attachment
            // chain around its local head/waist/arm anchor instead of applying
            // one global delta to a component that may span both sides of a
            // character through wide-range helper joints.
            std::queue<std::size_t> propagate;
            for (const std::size_t anchor : animatedAnchors)
            {
                owner[anchor] = anchor;
                propagate.push(anchor);
            }
            while (!propagate.empty())
            {
                const std::size_t index = propagate.front();
                propagate.pop();
                for (const std::size_t neighbor : adjacency[index])
                {
                    if (owner[neighbor] == invalidIndex)
                    {
                        owner[neighbor] = owner[index];
                        propagate.push(neighbor);
                    }
                }
            }
        }

        for (const std::size_t index : component)
        {
            RuntimeBody& runtime = this->rigidBodies[index];
            if (runtime.definition->mode == MmdRigidBodyMode::FollowBone)
            {
                runtime.resetTargetModelTransform =
                    runtime.prePhysicsAnimatedModelTransform;
                continue;
            }
            const std::size_t anchor = owner[index] == invalidIndex
                ? start
                : owner[index];
            const glm::mat4 componentDelta =
                this->rigidBodies[anchor]
                    .prePhysicsAnimatedModelTransform *
                glm::inverse(
                    this->rigidBodies[anchor]
                        .definition->modelBindTransform
                );
            runtime.resetTargetModelTransform = componentDelta *
                runtime.definition->modelBindTransform;
        }
    }
}

void MmdPhysicsInstance::ApplyResetTargets(const Transform& transform)
{
    const EntityFrame entity = ExtractEntityFrame(transform);
    for (RuntimeBody& runtime : this->rigidBodies)
    {
        const RigidTransform target = ModelToWorld(
            runtime.resetTargetModelTransform,
            entity
        );
        this->world->SetTransform(
            runtime.handle,
            target.position,
            target.rotation,
            true
        );
        runtime.postResetBulletModelTransform = WorldToModel(
            this->world->State(runtime.handle),
            entity
        );
        runtime.lastAnimatedPosition = target.position;
        runtime.lastAnimatedRotation = target.rotation;
        runtime.frameStartAnimatedPosition = target.position;
        runtime.frameStartAnimatedRotation = target.rotation;
        runtime.frameTargetAnimatedPosition = target.position;
        runtime.frameTargetAnimatedRotation = target.rotation;
        runtime.hasAnimatedTransform = true;
    }
}

void MmdPhysicsInstance::ResetToPose(const Transform& transform)
{
    this->SetFailureFreeze(false);
    this->jointSnapshots.clear();
    this->jointSnapshots.push_back(this->createdJointSnapshot);
    const JointSnapshot beforeReset = this->CaptureJointSnapshot(
        "before-reset"
    );
    this->jointSnapshots.push_back(beforeReset);

    for (RuntimeBody& runtime : this->rigidBodies)
    {
        runtime.prePhysicsAnimatedModelTransform = AnimatedBodyModelTransform(
            *runtime.definition,
            *this->pose
        );
    }
    this->CaptureConstraintPreservingResetTargets();
    this->ApplyResetTargets(transform);
    for (RuntimeBody& runtime : this->rigidBodies)
    {
        if (runtime.ccdActive)
        {
            this->world->ConfigureCcd(runtime.handle, false, 0.0f, 0.0f);
            runtime.ccdActive = false;
        }
        runtime.ccdIdleSeconds = 0.0f;
    }
    this->collisionStatistics.activeCcdBodyCount = 0U;
    this->contactDiagnostics.clear();

    const JointSnapshot afterReset = this->CaptureJointSnapshot(
        "after-reset"
    );
    this->jointSnapshots.push_back(afterReset);
    this->pendingStabilizationSteps = 0U;
    this->resetTargetRefreshPending = true;
    this->suppressImpulseMorphOnce = true;
    for (RecoveryChain& chain : this->recoveryChains)
    {
        chain.abnormalSeconds = 0.0f;
        chain.cooldownSeconds = 0.0f;
        chain.fuseWindowSeconds = 0.0f;
        chain.fuseRemainingSeconds = 0.0f;
        chain.recoveriesInWindow = 0U;
        chain.fuseSuppressionLatched = false;
        chain.pendingTrigger = {};
    }
    std::fill(
        this->recoveryJointSeverityHistory.begin(),
        this->recoveryJointSeverityHistory.end(),
        0.0f
    );
    std::fill(
        this->recoveryPreviousNormalizedExtension.begin(),
        this->recoveryPreviousNormalizedExtension.end(),
        1.0f
    );
    this->UpdateRecoveryStatistics();
    this->BuildAlignmentDiagnostics();

    if (this->rigidBodies.size() >= 32U)
    {
        this->LogJointSnapshot(beforeReset);
        this->LogJointSnapshot(afterReset);
    }
}


void MmdPhysicsInstance::BuildRecoveryChains()
{
    const std::size_t bodyCount = this->rigidBodies.size();
    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    this->recoveryChains.clear();
    this->recoveryChainByBody.assign(bodyCount, invalidIndex);
    this->recoveryAdjacency.assign(bodyCount, {});

    std::vector<std::vector<std::size_t>> adjacency(bodyCount);
    const std::span<const MmdJointDefinition> joints = this->asset->Joints();
    for (std::size_t jointIndex = 0U; jointIndex < joints.size(); ++jointIndex)
    {
        const MmdJointDefinition& joint = joints[jointIndex];
        if (joint.bodyA == InvalidRigidBodyIndex ||
            joint.bodyB == InvalidRigidBodyIndex ||
            joint.bodyA == joint.bodyB ||
            static_cast<std::size_t>(joint.bodyA) >= bodyCount ||
            static_cast<std::size_t>(joint.bodyB) >= bodyCount ||
            IsWideTravelHelperJoint(joint))
        {
            continue;
        }
        adjacency[joint.bodyA].push_back(joint.bodyB);
        adjacency[joint.bodyB].push_back(joint.bodyA);
        this->recoveryAdjacency[joint.bodyA].push_back(RecoveryEdge{
            static_cast<std::size_t>(joint.bodyB), jointIndex
        });
        this->recoveryAdjacency[joint.bodyB].push_back(RecoveryEdge{
            static_cast<std::size_t>(joint.bodyA), jointIndex
        });
    }

    std::vector<bool> visited(bodyCount, false);
    std::vector<std::size_t> owner(bodyCount, invalidIndex);
    for (std::size_t start = 0U; start < bodyCount; ++start)
    {
        if (visited[start])
            continue;

        std::vector<std::size_t> component;
        std::vector<std::size_t> anchors;
        std::queue<std::size_t> discover;
        discover.push(start);
        visited[start] = true;
        while (!discover.empty())
        {
            const std::size_t index = discover.front();
            discover.pop();
            component.push_back(index);
            if (this->rigidBodies[index].definition->mode ==
                MmdRigidBodyMode::FollowBone)
            {
                anchors.push_back(index);
            }
            for (const std::size_t neighbor : adjacency[index])
            {
                if (!visited[neighbor])
                {
                    visited[neighbor] = true;
                    discover.push(neighbor);
                }
            }
        }

        std::queue<std::size_t> propagate;
        if (anchors.empty())
        {
            owner[start] = start;
            propagate.push(start);
        }
        else
        {
            for (const std::size_t anchor : anchors)
            {
                owner[anchor] = anchor;
                propagate.push(anchor);
            }
        }
        while (!propagate.empty())
        {
            const std::size_t index = propagate.front();
            propagate.pop();
            for (const std::size_t neighbor : adjacency[index])
            {
                if (owner[neighbor] == invalidIndex)
                {
                    owner[neighbor] = owner[index];
                    propagate.push(neighbor);
                }
            }
        }
        for (const std::size_t index : component)
        {
            if (owner[index] == invalidIndex)
                owner[index] = start;
        }

        for (const std::size_t candidateOwner : component)
        {
            const bool alreadyCreated = std::any_of(
                this->recoveryChains.begin(),
                this->recoveryChains.end(),
                [candidateOwner](const RecoveryChain& chain)
                {
                    return chain.anchorBodyIndex == candidateOwner;
                }
            );
            if (alreadyCreated)
                continue;

            bool hasDynamicBody = false;
            for (const std::size_t index : component)
            {
                if (owner[index] == candidateOwner &&
                    this->rigidBodies[index].definition->mode !=
                        MmdRigidBodyMode::FollowBone)
                {
                    hasDynamicBody = true;
                    break;
                }
            }
            if (!hasDynamicBody)
                continue;

            RecoveryChain chain;
            chain.anchorBodyIndex = candidateOwner;
            const std::size_t chainIndex = this->recoveryChains.size();
            for (const std::size_t index : component)
            {
                if (owner[index] == candidateOwner)
                {
                    chain.bodyIndices.push_back(index);
                    this->recoveryChainByBody[index] = chainIndex;
                }
            }
            this->recoveryChains.push_back(std::move(chain));
        }
    }

    for (std::size_t jointIndex = 0U; jointIndex < joints.size(); ++jointIndex)
    {
        const MmdJointDefinition& joint = joints[jointIndex];
        if (joint.bodyA == InvalidRigidBodyIndex ||
            joint.bodyB == InvalidRigidBodyIndex ||
            joint.bodyA == joint.bodyB ||
            static_cast<std::size_t>(joint.bodyA) >= bodyCount ||
            static_cast<std::size_t>(joint.bodyB) >= bodyCount ||
            IsWideTravelHelperJoint(joint))
        {
            continue;
        }

        const std::size_t chainA = this->recoveryChainByBody[joint.bodyA];
        const std::size_t chainB = this->recoveryChainByBody[joint.bodyB];
        const auto appendJoint = [this, jointIndex, invalidIndex](
            std::size_t chainIndex
        )
        {
            if (chainIndex == invalidIndex ||
                chainIndex >= this->recoveryChains.size())
            {
                return;
            }
            std::vector<std::size_t>& indices =
                this->recoveryChains[chainIndex].jointIndices;
            if (std::find(indices.begin(), indices.end(), jointIndex) ==
                indices.end())
            {
                indices.push_back(jointIndex);
            }
        };
        appendJoint(chainA);
        appendJoint(chainB);
    }

    this->recoveryBindPathLengthByBody.assign(
        bodyCount,
        std::numeric_limits<float>::infinity()
    );
    this->recoveryPreviousNormalizedExtension.assign(bodyCount, 1.0f);
    for (std::size_t chainIndex = 0U;
         chainIndex < this->recoveryChains.size();
         ++chainIndex)
    {
        const RecoveryChain& chain = this->recoveryChains[chainIndex];
        if (chain.anchorBodyIndex >= bodyCount)
            continue;
        using DistanceNode = std::pair<float, std::size_t>;
        std::priority_queue<
            DistanceNode,
            std::vector<DistanceNode>,
            std::greater<DistanceNode>
        > pending;
        this->recoveryBindPathLengthByBody[chain.anchorBodyIndex] = 0.0f;
        pending.emplace(0.0f, chain.anchorBodyIndex);
        while (!pending.empty())
        {
            const auto [distance, bodyIndex] = pending.top();
            pending.pop();
            if (distance > this->recoveryBindPathLengthByBody[bodyIndex] +
                0.000001f)
            {
                continue;
            }
            const glm::vec3 position = glm::vec3(
                this->rigidBodies[bodyIndex].definition->modelBindTransform[3]
            );
            for (const RecoveryEdge& edge :
                 this->recoveryAdjacency[bodyIndex])
            {
                if (edge.bodyIndex >= bodyCount ||
                    this->recoveryChainByBody[edge.bodyIndex] != chainIndex)
                {
                    continue;
                }
                const glm::vec3 neighborPosition = glm::vec3(
                    this->rigidBodies[edge.bodyIndex]
                        .definition->modelBindTransform[3]
                );
                const float edgeLength = std::max(
                    glm::distance(position, neighborPosition),
                    0.001f
                );
                const float candidate = distance + edgeLength;
                if (candidate + 0.000001f <
                    this->recoveryBindPathLengthByBody[edge.bodyIndex])
                {
                    this->recoveryBindPathLengthByBody[edge.bodyIndex] =
                        candidate;
                    pending.emplace(candidate, edge.bodyIndex);
                }
            }
        }
    }

    this->recoveryJointSeverityHistory.assign(joints.size(), 0.0f);
    this->recoveryStatistics = {};
    this->recoveryStatistics.chainCount = this->recoveryChains.size();
}

void MmdPhysicsInstance::ConfigureGravityBalanceProfiles()
{
    const std::size_t bodyCount = this->rigidBodies.size();
    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    this->gravityChains.clear();
    this->gravityChainByBody.assign(bodyCount, invalidIndex);

    std::vector<MmdPhysicsChainKind> bodyKinds(
        bodyCount,
        MmdPhysicsChainKind::General
    );
    for (std::size_t bodyIndex = 0U; bodyIndex < bodyCount; ++bodyIndex)
    {
        bodyKinds[bodyIndex] = ClassifyBodyName(
            this->rigidBodies[bodyIndex].definition->name
        );
    }

    // PMX models frequently name only the first body of a decorative branch.
    // Propagate a unique neighboring category through unnamed continuation
    // bodies without merging two different categories at a junction.
    for (std::size_t pass = 0U; pass < 4U; ++pass)
    {
        bool changed = false;
        std::vector<MmdPhysicsChainKind> nextKinds = bodyKinds;
        for (std::size_t bodyIndex = 0U; bodyIndex < bodyCount; ++bodyIndex)
        {
            if (bodyKinds[bodyIndex] != MmdPhysicsChainKind::General)
                continue;
            MmdPhysicsChainKind candidate = MmdPhysicsChainKind::General;
            bool conflict = false;
            for (const RecoveryEdge& edge : this->recoveryAdjacency[bodyIndex])
            {
                if (edge.bodyIndex >= bodyCount)
                    continue;
                const MmdPhysicsChainKind neighborKind = bodyKinds[edge.bodyIndex];
                if (neighborKind == MmdPhysicsChainKind::General)
                    continue;
                if (candidate == MmdPhysicsChainKind::General)
                    candidate = neighborKind;
                else if (candidate != neighborKind)
                    conflict = true;
            }
            if (!conflict && candidate != MmdPhysicsChainKind::General)
            {
                nextKinds[bodyIndex] = candidate;
                changed = true;
            }
        }
        bodyKinds.swap(nextKinds);
        if (!changed)
            break;
    }

    std::vector<bool> visited(bodyCount, false);
    for (std::size_t start = 0U; start < bodyCount; ++start)
    {
        if (visited[start])
            continue;
        const MmdPhysicsChainKind kind = bodyKinds[start];
        std::queue<std::size_t> pending;
        std::vector<std::size_t> component;
        pending.push(start);
        visited[start] = true;
        while (!pending.empty())
        {
            const std::size_t bodyIndex = pending.front();
            pending.pop();
            component.push_back(bodyIndex);
            for (const RecoveryEdge& edge : this->recoveryAdjacency[bodyIndex])
            {
                if (edge.bodyIndex >= bodyCount || visited[edge.bodyIndex] ||
                    bodyKinds[edge.bodyIndex] != kind)
                {
                    continue;
                }
                visited[edge.bodyIndex] = true;
                pending.push(edge.bodyIndex);
            }
        }

        std::size_t dynamicCount = 0U;
        std::size_t boxCount = 0U;
        std::size_t anchorCount = 0U;
        std::size_t maximumDegree = 0U;
        std::size_t anchorBodyIndex = invalidIndex;
        for (const std::size_t bodyIndex : component)
        {
            const MmdRigidBodyDefinition& definition =
                *this->rigidBodies[bodyIndex].definition;
            maximumDegree = std::max(
                maximumDegree,
                this->recoveryAdjacency[bodyIndex].size()
            );
            if (definition.mode == MmdRigidBodyMode::FollowBone)
            {
                ++anchorCount;
                if (anchorBodyIndex == invalidIndex)
                    anchorBodyIndex = bodyIndex;
            }
            else
            {
                ++dynamicCount;
                if (definition.shape == MmdRigidBodyShape::Box)
                    ++boxCount;
            }
        }
        if (dynamicCount == 0U)
            continue;

        MmdPhysicsChainKind resolvedKind = kind;
        if (resolvedKind == MmdPhysicsChainKind::General &&
            dynamicCount >= 32U && boxCount * 5U >= dynamicCount * 3U)
        {
            resolvedKind = MmdPhysicsChainKind::Skirt;
        }
        else if (resolvedKind == MmdPhysicsChainKind::General &&
            dynamicCount >= 12U && anchorCount >= 1U &&
            anchorCount <= 2U && maximumDegree <= 3U)
        {
            // Names from non-UTF8 PMX files can be unavailable after import.
            // A long, low-branching component with one animated anchor is a
            // decorative chain rather than a torso island. Give it a safe
            // profile instead of GENERAL + zero damping.
            resolvedKind = MmdPhysicsChainKind::DecorativeFallback;
        }
        const ChainBalanceProfile profile = ProfileForChainKind(resolvedKind);
        GravityChain chain;
        chain.bodyIndices = std::move(component);
        chain.kind = resolvedKind;
        chain.gravityScale = profile.gravityScale;
        chain.minimumLinearDamping = profile.minimumLinearDamping;
        chain.minimumAngularDamping = profile.minimumAngularDamping;
        chain.anchorBodyIndex = anchorBodyIndex;
        const std::size_t chainIndex = this->gravityChains.size();
        for (const std::size_t bodyIndex : chain.bodyIndices)
            this->gravityChainByBody[bodyIndex] = chainIndex;
        this->gravityChains.push_back(std::move(chain));
    }

    this->chainBalanceStatistics.resize(this->gravityChains.size());
    const std::size_t skirtLayerIgnoredPairCount =
        this->gravityStatistics.skirtLayerIgnoredPairCount;
    const std::size_t skirtSemanticIgnoredPairCount =
        this->gravityStatistics.skirtSemanticIgnoredPairCount;
    this->gravityStatistics = {};
    this->gravityStatistics.skirtLayerIgnoredPairCount =
        skirtLayerIgnoredPairCount;
    this->gravityStatistics.skirtSemanticIgnoredPairCount =
        skirtSemanticIgnoredPairCount;
    this->gravityStatistics.mode = this->gravityMode;
    this->gravityStatistics.chainCount = this->gravityChains.size();
}

void MmdPhysicsInstance::ApplyGravityBalanceSettings(bool force)
{
    const glm::vec3 worldGravity = this->world->Gravity();
    const bool gravityChanged = !std::isfinite(this->lastAppliedWorldGravity.x) ||
        glm::distance(worldGravity, this->lastAppliedWorldGravity) > 0.000001f;
    if (!force && !gravityChanged)
        return;

    const bool profilesEnabled = GravityModeUsesChainProfiles(this->gravityMode);
    const float globalScale = GravityModeScale(this->gravityMode);
    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    for (std::size_t bodyIndex = 0U;
         bodyIndex < this->rigidBodies.size();
         ++bodyIndex)
    {
        RuntimeBody& runtime = this->rigidBodies[bodyIndex];
        if (runtime.definition->mode == MmdRigidBodyMode::FollowBone)
            continue;

        float chainScale = 1.0f;
        float minimumLinearDamping = 0.0f;
        float minimumAngularDamping = 0.0f;
        const std::size_t chainIndex = bodyIndex < this->gravityChainByBody.size()
            ? this->gravityChainByBody[bodyIndex]
            : invalidIndex;
        if (profilesEnabled && chainIndex < this->gravityChains.size())
        {
            const GravityChain& chain = this->gravityChains[chainIndex];
            chainScale = chain.gravityScale;
            minimumLinearDamping = chain.minimumLinearDamping;
            minimumAngularDamping = chain.minimumAngularDamping;
        }

        const float effectiveScale = globalScale * chainScale;
        const float linearDamping = std::clamp(
            std::max(runtime.baseLinearDamping, minimumLinearDamping),
            0.0f,
            0.95f
        );
        const float angularDamping = std::clamp(
            std::max(runtime.baseAngularDamping, minimumAngularDamping),
            0.0f,
            0.95f
        );
        this->world->ConfigureGravity(
            runtime.handle,
            profilesEnabled,
            worldGravity * effectiveScale
        );
        this->world->SetDamping(
            runtime.handle,
            profilesEnabled ? linearDamping : runtime.baseLinearDamping,
            profilesEnabled ? angularDamping : runtime.baseAngularDamping
        );
        runtime.appliedGravityScale = profilesEnabled ? effectiveScale : 1.0f;
        runtime.appliedLinearDamping = profilesEnabled
            ? linearDamping
            : runtime.baseLinearDamping;
        runtime.appliedAngularDamping = profilesEnabled
            ? angularDamping
            : runtime.baseAngularDamping;
    }
    this->lastAppliedWorldGravity = worldGravity;
    this->UpdateGravityBalanceStatistics();
}

void MmdPhysicsInstance::UpdateGravityBalanceStatistics()
{
    const std::size_t skirtLayerIgnoredPairCount =
        this->gravityStatistics.skirtLayerIgnoredPairCount;
    const std::size_t skirtSemanticIgnoredPairCount =
        this->gravityStatistics.skirtSemanticIgnoredPairCount;
    this->gravityStatistics = {};
    this->gravityStatistics.skirtLayerIgnoredPairCount =
        skirtLayerIgnoredPairCount;
    this->gravityStatistics.skirtSemanticIgnoredPairCount =
        skirtSemanticIgnoredPairCount;
    this->gravityStatistics.mode = this->gravityMode;
    this->gravityStatistics.globalGravityScale = GravityModeScale(this->gravityMode);
    this->gravityStatistics.chainProfilesEnabled =
        GravityModeUsesChainProfiles(this->gravityMode);
    this->gravityStatistics.chainCount = this->gravityChains.size();
    this->chainBalanceStatistics.assign(
        this->gravityChains.size(),
        MmdPhysicsChainBalanceStatistics{}
    );

    const glm::vec3 gravity = this->world->Gravity();
    const glm::vec3 downDirection = glm::length(gravity) > 0.000001f
        ? glm::normalize(gravity)
        : glm::vec3(0.0f, -1.0f, 0.0f);
    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    float totalGravityScale = 0.0f;
    float totalDownwardDisplacement = 0.0f;
    float totalSpeed = 0.0f;
    std::size_t totalDynamicCount = 0U;
    float minimumScale = std::numeric_limits<float>::max();
    float maximumScale = 0.0f;

    for (std::size_t chainIndex = 0U;
         chainIndex < this->gravityChains.size();
         ++chainIndex)
    {
        const GravityChain& chain = this->gravityChains[chainIndex];
        MmdPhysicsChainBalanceStatistics& statistics =
            this->chainBalanceStatistics[chainIndex];
        statistics.chainIndex = chainIndex;
        statistics.kind = chain.kind;
        statistics.bodyCount = chain.bodyIndices.size();
        statistics.gravityScale = chain.gravityScale;
        statistics.effectiveGravityScale = 0.0f;
        statistics.minimumLinearDamping = chain.minimumLinearDamping;
        statistics.minimumAngularDamping = chain.minimumAngularDamping;
        statistics.anchorBodyIndex = chain.anchorBodyIndex;
        float chainDownward = 0.0f;
        float chainSpeed = 0.0f;
        float chainAnchorDistance = 0.0f;
        float chainNormalizedExtension = 0.0f;
        std::size_t extensionCount = 0U;
        std::optional<PhysicsBodyState> anchorState;
        if (chain.anchorBodyIndex < this->rigidBodies.size())
        {
            anchorState = this->world->State(
                this->rigidBodies[chain.anchorBodyIndex].handle
            );
            statistics.anchorLinearSpeed = glm::length(
                anchorState->linearVelocity
            );
            statistics.anchorAngularSpeed = glm::length(
                anchorState->angularVelocity
            );
        }

        for (const std::size_t bodyIndex : chain.bodyIndices)
        {
            if (bodyIndex >= this->rigidBodies.size())
                continue;
            const RuntimeBody& runtime = this->rigidBodies[bodyIndex];
            const MmdRigidBodyDefinition& definition = *runtime.definition;
            if (definition.mode == MmdRigidBodyMode::FollowBone)
                continue;
            const PhysicsBodyState state = this->world->State(runtime.handle);
            const RigidTransform target = ModelToWorld(
                runtime.prePhysicsAnimatedModelTransform,
                entity
            );
            const float downward = std::max(
                0.0f,
                glm::dot(state.position - target.position, downDirection)
            );
            const float speed = glm::length(state.linearVelocity);
            ++statistics.dynamicBodyCount;
            statistics.totalMass += definition.mass;
            statistics.effectiveGravityScale += runtime.appliedGravityScale;
            chainDownward += downward;
            chainSpeed += speed;
            statistics.maximumDownwardDisplacement = std::max(
                statistics.maximumDownwardDisplacement,
                downward
            );
            statistics.maximumSpeed = std::max(statistics.maximumSpeed, speed);
            if (definition.mode == MmdRigidBodyMode::PhysicsWithBone)
            {
                statistics.maximumMode2TranslationDelta = std::max(
                    statistics.maximumMode2TranslationDelta,
                    glm::distance(state.position, target.position)
                );
            }
            if (anchorState.has_value() &&
                bodyIndex != chain.anchorBodyIndex &&
                bodyIndex < this->recoveryBindPathLengthByBody.size())
            {
                const float bindLength =
                    this->recoveryBindPathLengthByBody[bodyIndex] * entity.scale;
                if (std::isfinite(bindLength) && bindLength > 0.0001f)
                {
                    const float anchorDistance = glm::distance(
                        state.position,
                        anchorState->position
                    );
                    const float normalizedExtension =
                        anchorDistance / bindLength;
                    chainAnchorDistance += anchorDistance;
                    chainNormalizedExtension += normalizedExtension;
                    ++extensionCount;
                    statistics.maximumAnchorDistance = std::max(
                        statistics.maximumAnchorDistance,
                        anchorDistance
                    );
                    statistics.maximumNormalizedExtension = std::max(
                        statistics.maximumNormalizedExtension,
                        normalizedExtension
                    );
                }
            }
            totalGravityScale += runtime.appliedGravityScale;
            totalDownwardDisplacement += downward;
            totalSpeed += speed;
            ++totalDynamicCount;
            minimumScale = std::min(minimumScale, runtime.appliedGravityScale);
            maximumScale = std::max(maximumScale, runtime.appliedGravityScale);
        }
        if (statistics.dynamicBodyCount > 0U)
        {
            const float inverseCount = 1.0f /
                static_cast<float>(statistics.dynamicBodyCount);
            statistics.effectiveGravityScale *= inverseCount;
            statistics.averageDownwardDisplacement = chainDownward * inverseCount;
            statistics.averageSpeed = chainSpeed * inverseCount;
        }
        if (extensionCount > 0U)
        {
            const float inverseExtensionCount = 1.0f /
                static_cast<float>(extensionCount);
            statistics.averageAnchorDistance =
                chainAnchorDistance * inverseExtensionCount;
            statistics.averageNormalizedExtension =
                chainNormalizedExtension * inverseExtensionCount;
        }
    }

    for (const MmdPhysicsContactDiagnostic& contact : this->contactDiagnostics)
    {
        const auto accumulateContact = [this, &contact](std::size_t chainIndex)
        {
            if (chainIndex >= this->chainBalanceStatistics.size())
                return;
            MmdPhysicsChainBalanceStatistics& statistics =
                this->chainBalanceStatistics[chainIndex];
            ++statistics.contactPairCount;
            statistics.contactImpulse += contact.totalAppliedImpulse;
        };
        accumulateContact(contact.chainAIndex);
        if (contact.chainBIndex != contact.chainAIndex)
            accumulateContact(contact.chainBIndex);
    }

    const std::span<const MmdJointDefinition> joints = this->asset->Joints();
    const std::size_t constraintCount = std::min(
        joints.size(),
        this->constraints.size()
    );
    for (std::size_t jointIndex = 0U; jointIndex < constraintCount; ++jointIndex)
    {
        const MmdJointDefinition& joint = joints[jointIndex];
        if (joint.bodyA == InvalidRigidBodyIndex ||
            joint.bodyB == InvalidRigidBodyIndex ||
            static_cast<std::size_t>(joint.bodyA) >=
                this->gravityChainByBody.size() ||
            static_cast<std::size_t>(joint.bodyB) >=
                this->gravityChainByBody.size())
        {
            continue;
        }
        const float impulse = std::abs(
            this->world->ConstraintState(this->constraints[jointIndex])
                .appliedImpulse
        );
        const std::size_t chainA = this->gravityChainByBody[joint.bodyA];
        const std::size_t chainB = this->gravityChainByBody[joint.bodyB];
        const auto accumulateConstraint = [this, impulse](
            std::size_t chainIndex
        )
        {
            if (chainIndex >= this->chainBalanceStatistics.size())
                return;
            MmdPhysicsChainBalanceStatistics& statistics =
                this->chainBalanceStatistics[chainIndex];
            statistics.totalConstraintImpulse += impulse;
            statistics.maximumConstraintImpulse = std::max(
                statistics.maximumConstraintImpulse,
                impulse
            );
        };
        accumulateConstraint(chainA);
        if (chainB != chainA)
            accumulateConstraint(chainB);
    }

    this->gravityStatistics.dynamicBodyCount = totalDynamicCount;
    this->gravityStatistics.totalContactImpulse =
        this->collisionStatistics.totalAppliedImpulse;
    if (totalDynamicCount > 0U)
    {
        const float inverseCount = 1.0f / static_cast<float>(totalDynamicCount);
        this->gravityStatistics.minimumEffectiveGravityScale = minimumScale;
        this->gravityStatistics.maximumEffectiveGravityScale = maximumScale;
        this->gravityStatistics.averageEffectiveGravityScale =
            totalGravityScale * inverseCount;
        this->gravityStatistics.averageDownwardDisplacement =
            totalDownwardDisplacement * inverseCount;
        this->gravityStatistics.averageSpeed = totalSpeed * inverseCount;
    }
    else
    {
        this->gravityStatistics.minimumEffectiveGravityScale = 0.0f;
        this->gravityStatistics.maximumEffectiveGravityScale = 0.0f;
        this->gravityStatistics.averageEffectiveGravityScale = 0.0f;
    }
    for (const MmdPhysicsChainBalanceStatistics& statistics :
         this->chainBalanceStatistics)
    {
        this->gravityStatistics.maximumDownwardDisplacement = std::max(
            this->gravityStatistics.maximumDownwardDisplacement,
            statistics.maximumDownwardDisplacement
        );
        this->gravityStatistics.maximumSpeed = std::max(
            this->gravityStatistics.maximumSpeed,
            statistics.maximumSpeed
        );
        this->gravityStatistics.totalConstraintImpulse +=
            statistics.totalConstraintImpulse;
        this->gravityStatistics.maximumConstraintImpulse = std::max(
            this->gravityStatistics.maximumConstraintImpulse,
            statistics.maximumConstraintImpulse
        );
        this->gravityStatistics.maximumNormalizedExtension = std::max(
            this->gravityStatistics.maximumNormalizedExtension,
            statistics.maximumNormalizedExtension
        );
        this->gravityStatistics.maximumAnchorLinearSpeed = std::max(
            this->gravityStatistics.maximumAnchorLinearSpeed,
            statistics.anchorLinearSpeed
        );
        this->gravityStatistics.maximumMode2TranslationDelta = std::max(
            this->gravityStatistics.maximumMode2TranslationDelta,
            statistics.maximumMode2TranslationDelta
        );
    }
}

void MmdPhysicsInstance::ConfigureCollisionTopology()
{
    const std::size_t bodyCount = this->rigidBodies.size();
    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    const auto pairKey = [](std::size_t left, std::size_t right)
    {
        const std::uint32_t lower = static_cast<std::uint32_t>(
            std::min(left, right)
        );
        const std::uint32_t upper = static_cast<std::uint32_t>(
            std::max(left, right)
        );
        return (static_cast<std::uint64_t>(lower) << 32U) |
            static_cast<std::uint64_t>(upper);
    };

    std::unordered_set<std::uint64_t> linkedPairs;
    for (const MmdJointDefinition& joint : this->asset->Joints())
    {
        if (joint.bodyA == InvalidRigidBodyIndex ||
            joint.bodyB == InvalidRigidBodyIndex ||
            joint.bodyA == joint.bodyB ||
            static_cast<std::size_t>(joint.bodyA) >= bodyCount ||
            static_cast<std::size_t>(joint.bodyB) >= bodyCount)
        {
            continue;
        }
        linkedPairs.insert(pairKey(joint.bodyA, joint.bodyB));
    }
    this->collisionStatistics.linkedJointPairCount = linkedPairs.size();

    std::unordered_set<std::uint64_t> ignoredPairs;
    std::size_t skirtLayerIgnoredPairs = 0U;
    std::size_t skirtSemanticIgnoredPairs = 0U;
    for (std::size_t bodyAIndex = 0U;
         bodyAIndex < bodyCount;
         ++bodyAIndex)
    {
        if (this->rigidBodies[bodyAIndex].definition->mode ==
            MmdRigidBodyMode::FollowBone)
        {
            continue;
        }
        const std::size_t gravityChain =
            bodyAIndex < this->gravityChainByBody.size()
                ? this->gravityChainByBody[bodyAIndex]
                : invalidIndex;
        if (gravityChain == invalidIndex ||
            gravityChain >= this->gravityChains.size())
        {
            continue;
        }
        const bool skirtChain = this->gravityChains[gravityChain].kind ==
            MmdPhysicsChainKind::Skirt;
        const std::size_t maximumDepth = skirtChain
            ? SkirtSelfCollisionGraphDistance
            : 2U;

        std::queue<std::pair<std::size_t, std::size_t>> pending;
        std::vector<std::size_t> bestDepth(bodyCount, invalidIndex);
        pending.emplace(bodyAIndex, 0U);
        bestDepth[bodyAIndex] = 0U;
        while (!pending.empty())
        {
            const auto [current, depth] = pending.front();
            pending.pop();
            if (depth >= maximumDepth)
                continue;
            for (const RecoveryEdge& edge : this->recoveryAdjacency[current])
            {
                const std::size_t neighbor = edge.bodyIndex;
                const std::size_t nextDepth = depth + 1U;
                if (neighbor >= bodyCount ||
                    nextDepth >= bestDepth[neighbor])
                {
                    continue;
                }
                bestDepth[neighbor] = nextDepth;
                pending.emplace(neighbor, nextDepth);
            }
        }

        for (std::size_t bodyBIndex = bodyAIndex + 1U;
             bodyBIndex < bodyCount;
             ++bodyBIndex)
        {
            if (this->rigidBodies[bodyBIndex].definition->mode ==
                    MmdRigidBodyMode::FollowBone ||
                bodyBIndex >= this->gravityChainByBody.size() ||
                this->gravityChainByBody[bodyBIndex] != gravityChain)
            {
                continue;
            }
            const std::uint64_t key = pairKey(bodyAIndex, bodyBIndex);
            if (linkedPairs.contains(key) || ignoredPairs.contains(key))
                continue;

            const MmdRigidBodyDefinition& bodyA =
                *this->rigidBodies[bodyAIndex].definition;
            const MmdRigidBodyDefinition& bodyB =
                *this->rigidBodies[bodyBIndex].definition;
            const glm::vec3 positionA = glm::vec3(bodyA.modelBindTransform[3]);
            const glm::vec3 positionB = glm::vec3(bodyB.modelBindTransform[3]);
            const float boundingSum = ShapeBoundingRadiusModel(bodyA) +
                ShapeBoundingRadiusModel(bodyB);
            if (boundingSum <= 0.0f)
                continue;

            const std::size_t graphDistance = bestDepth[bodyBIndex];
            const bool graphCandidate = graphDistance >= 2U &&
                graphDistance <= maximumDepth;
            bool semanticConflict = false;
            if (skirtChain &&
                bodyA.shape == MmdRigidBodyShape::Box &&
                bodyB.shape == MmdRigidBodyShape::Box &&
                bodyA.collisionGroup == bodyB.collisionGroup)
            {
                const SkirtSemantic semanticA = ParseSkirtSemantic(bodyA.name);
                const SkirtSemantic semanticB = ParseSkirtSemantic(bodyB.name);
                if (semanticA.valid && semanticB.valid &&
                    semanticA.level == semanticB.level)
                {
                    const int sectionDistance = std::abs(
                        semanticA.section - semanticB.section
                    );
                    const bool mainAuxiliaryConflict =
                        semanticA.auxiliary != semanticB.auxiliary &&
                        sectionDistance <= 4;
                    const bool sameRingNearNeighbor =
                        !semanticA.auxiliary && !semanticB.auxiliary &&
                        sectionDistance > 0 && sectionDistance <= 3;
                    semanticConflict =
                        (mainAuxiliaryConflict || sameRingNearNeighbor) &&
                        glm::distance(positionA, positionB) <=
                            boundingSum * 1.35f;
                }
            }
            if (!graphCandidate && !semanticConflict)
                continue;

            if (graphCandidate && skirtChain && graphDistance > 2U)
            {
                const bool skirtProxyPair =
                    bodyA.shape == MmdRigidBodyShape::Box &&
                    bodyB.shape == MmdRigidBodyShape::Box &&
                    bodyA.collisionGroup == bodyB.collisionGroup;
                if (!skirtProxyPair && !semanticConflict)
                    continue;
            }

            const float proximityFactor = skirtChain
                ? SkirtSelfCollisionProximityFactor
                : CollisionNearNeighborProximityFactor;
            if (!semanticConflict &&
                glm::distance(positionA, positionB) >
                    boundingSum * proximityFactor)
            {
                continue;
            }

            this->world->SetCollisionPairIgnored(
                this->rigidBodies[bodyAIndex].handle,
                this->rigidBodies[bodyBIndex].handle,
                true
            );
            ignoredPairs.insert(key);
            if (semanticConflict)
                ++skirtSemanticIgnoredPairs;
            else if (skirtChain && graphDistance > 2U)
                ++skirtLayerIgnoredPairs;
        }
    }
    this->collisionStatistics.ignoredNearNeighborPairCount =
        ignoredPairs.size();
    this->gravityStatistics.skirtLayerIgnoredPairCount =
        skirtLayerIgnoredPairs;
    this->gravityStatistics.skirtSemanticIgnoredPairCount =
        skirtSemanticIgnoredPairs;
}

void MmdPhysicsInstance::UpdateAdaptiveCcd(float fixedTimeStep)
{
    this->collisionStatistics.activeCcdBodyCount = 0U;
    for (RuntimeBody& runtime : this->rigidBodies)
    {
        if (!runtime.ccdCandidate)
            continue;

        const PhysicsBodyState state = this->world->State(runtime.handle);
        const float linearTravel = glm::length(state.linearVelocity) *
            fixedTimeStep;
        const float angularTravel = glm::length(state.angularVelocity) *
            runtime.ccdMaximumExtent * fixedTimeStep;
        const float predictedTravel = linearTravel + angularTravel;
        const float enableTravel = std::max(
            runtime.ccdMotionThreshold,
            runtime.ccdFeatureSize * AdaptiveCcdEnableTravelFactor
        );
        const float disableTravel = runtime.ccdFeatureSize *
            AdaptiveCcdDisableTravelFactor;

        if (!runtime.ccdActive && predictedTravel >= enableTravel)
        {
            this->world->ConfigureCcd(
                runtime.handle,
                true,
                runtime.ccdMotionThreshold,
                runtime.ccdSweptSphereRadius
            );
            runtime.ccdActive = true;
            runtime.ccdIdleSeconds = 0.0f;
            ++this->collisionStatistics.ccdActivationCount;
        }
        else if (runtime.ccdActive)
        {
            if (predictedTravel <= disableTravel)
                runtime.ccdIdleSeconds += fixedTimeStep;
            else
                runtime.ccdIdleSeconds = 0.0f;

            if (runtime.ccdIdleSeconds >= AdaptiveCcdDisableDelaySeconds)
            {
                this->world->ConfigureCcd(
                    runtime.handle,
                    false,
                    0.0f,
                    0.0f
                );
                runtime.ccdActive = false;
                runtime.ccdIdleSeconds = 0.0f;
                ++this->collisionStatistics.ccdDeactivationCount;
            }
        }

        if (runtime.ccdActive)
            ++this->collisionStatistics.activeCcdBodyCount;
    }
}

void MmdPhysicsInstance::UpdateCollisionDiagnostics()
{
    this->contactDiagnostics.clear();
    this->collisionStatistics.contactPairCount = 0U;
    this->collisionStatistics.sameChainContactPairCount = 0U;
    this->collisionStatistics.crossChainContactPairCount = 0U;
    this->collisionStatistics.contactPointCount = 0U;
    this->collisionStatistics.maximumPenetrationDepth = 0.0f;
    this->collisionStatistics.totalAppliedImpulse = 0.0f;
    this->collisionStatistics.maximumPairImpulse = 0.0f;

    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    const auto resolveRuntimeIndex = [this, invalidIndex](
        PhysicsBodyHandle handle
    )
    {
        if (handle.index >= this->runtimeBodyByWorldHandle.size())
            return invalidIndex;
        const std::size_t runtimeIndex =
            this->runtimeBodyByWorldHandle[handle.index];
        if (runtimeIndex >= this->rigidBodies.size() ||
            this->rigidBodies[runtimeIndex].handle != handle)
        {
            return invalidIndex;
        }
        return runtimeIndex;
    };

    for (const PhysicsContactPair& pair : this->world->ContactPairs())
    {
        const std::size_t bodyAIndex = resolveRuntimeIndex(pair.bodyA);
        const std::size_t bodyBIndex = resolveRuntimeIndex(pair.bodyB);
        if (bodyAIndex == invalidIndex || bodyBIndex == invalidIndex)
            continue;

        MmdPhysicsContactDiagnostic diagnostic;
        diagnostic.bodyAIndex = bodyAIndex;
        diagnostic.bodyBIndex = bodyBIndex;
        diagnostic.chainAIndex = bodyAIndex < this->gravityChainByBody.size()
            ? this->gravityChainByBody[bodyAIndex]
            : invalidIndex;
        diagnostic.chainBIndex = bodyBIndex < this->gravityChainByBody.size()
            ? this->gravityChainByBody[bodyBIndex]
            : invalidIndex;
        diagnostic.contactPointCount = pair.contactPointCount;
        diagnostic.maximumPenetrationDepth = pair.maximumPenetrationDepth;
        diagnostic.totalAppliedImpulse = pair.totalAppliedImpulse;
        diagnostic.maximumAppliedImpulse = pair.maximumAppliedImpulse;
        diagnostic.deepestPointOnB = pair.deepestPointOnB;
        diagnostic.deepestNormalOnB = pair.deepestNormalOnB;

        ++this->collisionStatistics.contactPairCount;
        this->collisionStatistics.contactPointCount +=
            diagnostic.contactPointCount;
        const bool sameChain = diagnostic.chainAIndex != invalidIndex &&
            diagnostic.chainAIndex == diagnostic.chainBIndex;
        if (sameChain)
            ++this->collisionStatistics.sameChainContactPairCount;
        else
            ++this->collisionStatistics.crossChainContactPairCount;
        this->collisionStatistics.maximumPenetrationDepth = std::max(
            this->collisionStatistics.maximumPenetrationDepth,
            diagnostic.maximumPenetrationDepth
        );
        this->collisionStatistics.totalAppliedImpulse +=
            diagnostic.totalAppliedImpulse;
        this->collisionStatistics.maximumPairImpulse = std::max(
            this->collisionStatistics.maximumPairImpulse,
            diagnostic.totalAppliedImpulse
        );

        if (this->contactDiagnostics.size() < MaximumContactDiagnostics)
            this->contactDiagnostics.push_back(diagnostic);
    }
}

void MmdPhysicsInstance::RecoverAbnormalChains(float fixedTimeStep)
{
    if (this->recoveryChains.empty())
        return;

    ++this->recoveryStatistics.physicsTickCount;
    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    const float scale = entity.scale;

    for (RecoveryChain& chain : this->recoveryChains)
    {
        chain.cooldownSeconds = std::max(
            0.0f,
            chain.cooldownSeconds - fixedTimeStep
        );
        const bool fuseWasActive = chain.fuseRemainingSeconds > 0.0f;
        chain.fuseRemainingSeconds = std::max(
            0.0f,
            chain.fuseRemainingSeconds - fixedTimeStep
        );
        chain.fuseWindowSeconds = std::max(
            0.0f,
            chain.fuseWindowSeconds - fixedTimeStep
        );
        if (fuseWasActive && chain.fuseRemainingSeconds <= 0.0f)
        {
            chain.recoveriesInWindow = 0U;
            chain.fuseWindowSeconds = 0.0f;
            chain.fuseSuppressionLatched = false;
        }
        else if (chain.fuseRemainingSeconds <= 0.0f &&
            chain.fuseWindowSeconds <= 0.0f)
        {
            chain.recoveriesInWindow = 0U;
            chain.fuseSuppressionLatched = false;
        }
    }

    std::vector<PhysicsBodyState> states;
    std::vector<glm::mat4> bodyModels;
    std::vector<bool> finite;
    states.reserve(this->rigidBodies.size());
    bodyModels.reserve(this->rigidBodies.size());
    finite.reserve(this->rigidBodies.size());
    std::vector<RecoveryTrigger> triggers(this->recoveryChains.size());

    const auto mark = [this, invalidIndex, &triggers](
        std::size_t chainIndex,
        const RecoveryTrigger& trigger
    )
    {
        if (chainIndex == invalidIndex ||
            chainIndex >= this->recoveryChains.size())
        {
            return;
        }
        RecoveryTrigger& current = triggers[chainIndex];
        const bool replace = current.reason == MmdPhysicsRecoveryReason::None ||
            (trigger.immediate && !current.immediate) ||
            (trigger.immediate == current.immediate &&
                trigger.score > current.score);
        if (replace)
            current = trigger;
    };

    for (std::size_t index = 0U; index < this->rigidBodies.size(); ++index)
    {
        const RuntimeBody& runtime = this->rigidBodies[index];
        const PhysicsBodyState state = this->world->State(runtime.handle);
        states.push_back(state);
        const bool stateFinite =
            std::isfinite(state.position.x) &&
            std::isfinite(state.position.y) &&
            std::isfinite(state.position.z) &&
            std::isfinite(state.rotation.w) &&
            std::isfinite(state.rotation.x) &&
            std::isfinite(state.rotation.y) &&
            std::isfinite(state.rotation.z) &&
            std::isfinite(state.linearVelocity.x) &&
            std::isfinite(state.linearVelocity.y) &&
            std::isfinite(state.linearVelocity.z) &&
            std::isfinite(state.angularVelocity.x) &&
            std::isfinite(state.angularVelocity.y) &&
            std::isfinite(state.angularVelocity.z);
        finite.push_back(stateFinite);
        bodyModels.push_back(
            stateFinite ? WorldToModel(state, entity) : glm::mat4(1.0f)
        );

        const std::size_t chainIndex = this->recoveryChainByBody[index];
        if (!stateFinite)
        {
            RecoveryTrigger trigger;
            trigger.reason = MmdPhysicsRecoveryReason::NonFinite;
            trigger.seedBodyIndex = index;
            trigger.immediate = true;
            trigger.score = 1000.0f;
            mark(chainIndex, trigger);
            continue;
        }
        if (runtime.definition->mode == MmdRigidBodyMode::FollowBone)
            continue;

        const float linearSpeed = glm::length(state.linearVelocity);
        const float angularSpeed = glm::length(state.angularVelocity);
        if (linearSpeed > RecoveryHardLinearSpeed * scale ||
            angularSpeed > RecoveryHardAngularSpeed)
        {
            RecoveryTrigger trigger;
            trigger.reason = MmdPhysicsRecoveryReason::ExtremeVelocity;
            trigger.seedBodyIndex = index;
            trigger.immediate = true;
            trigger.linearSpeed = linearSpeed;
            trigger.angularSpeed = angularSpeed;
            trigger.score = 900.0f + std::max(
                linearSpeed / (RecoveryHardLinearSpeed * scale),
                angularSpeed / RecoveryHardAngularSpeed
            );
            mark(chainIndex, trigger);
        }
        else if (linearSpeed > RecoveryLinearSpeed * scale ||
            angularSpeed > RecoveryAngularSpeed)
        {
            RecoveryTrigger trigger;
            trigger.reason = MmdPhysicsRecoveryReason::HighVelocity;
            trigger.seedBodyIndex = index;
            trigger.linearSpeed = linearSpeed;
            trigger.angularSpeed = angularSpeed;
            trigger.score = 300.0f + std::max(
                linearSpeed / (RecoveryLinearSpeed * scale),
                angularSpeed / RecoveryAngularSpeed
            );
            mark(chainIndex, trigger);
        }

        const RigidTransform animated = ModelToWorld(
            runtime.prePhysicsAnimatedModelTransform,
            entity
        );
        const float bodySize = CharacteristicBodySize(*runtime.definition) *
            scale;
        const float runawayDistance = std::max(
            RecoveryRunawayDistance * scale,
            bodySize * 12.0f
        );
        const float distanceFromAnimation = glm::distance(
            state.position,
            animated.position
        );

        float normalizedExtension = 1.0f;
        float anchorDistance = 0.0f;
        float bindChainLength = 0.0f;
        bool anchorMetricValid = false;
        if (chainIndex < this->recoveryChains.size() &&
            index < this->recoveryBindPathLengthByBody.size())
        {
            const RecoveryChain& chain = this->recoveryChains[chainIndex];
            bindChainLength = this->recoveryBindPathLengthByBody[index] * scale;
            if (chain.anchorBodyIndex < this->rigidBodies.size() &&
                chain.anchorBodyIndex != index &&
                std::isfinite(bindChainLength) &&
                bindChainLength > std::max(bodySize * 0.25f, 0.0001f))
            {
                const PhysicsBodyState anchorState = this->world->State(
                    this->rigidBodies[chain.anchorBodyIndex].handle
                );
                anchorDistance = glm::distance(
                    state.position,
                    anchorState.position
                );
                normalizedExtension = anchorDistance / bindChainLength;
                anchorMetricValid = std::isfinite(normalizedExtension);
            }
        }

        const float previousExtension =
            index < this->recoveryPreviousNormalizedExtension.size()
                ? this->recoveryPreviousNormalizedExtension[index]
                : 1.0f;
        if (index < this->recoveryPreviousNormalizedExtension.size() &&
            anchorMetricValid)
        {
            this->recoveryPreviousNormalizedExtension[index] =
                normalizedExtension;
        }
        const bool extensionGrowing = anchorMetricValid &&
            normalizedExtension >
                previousExtension + RecoveryExtensionGrowthTolerance;
        const bool supportedByMotion =
            linearSpeed > RecoveryRunawaySupportSpeed * scale;
        const bool hardExtension = anchorMetricValid &&
            normalizedExtension > RecoveryHardNormalizedExtension;
        const bool sustainedExtension = anchorMetricValid &&
            normalizedExtension > RecoveryNormalizedExtension &&
            distanceFromAnimation > runawayDistance &&
            (extensionGrowing || supportedByMotion);
        if (hardExtension || sustainedExtension)
        {
            RecoveryTrigger trigger;
            trigger.reason = MmdPhysicsRecoveryReason::Runaway;
            trigger.seedBodyIndex = index;
            // Only physically impossible chain stretch is immediate. A body
            // that is merely far from the pure-animation target must show
            // growth or motion over real physics time.
            trigger.immediate = normalizedExtension >
                RecoveryHardNormalizedExtension * 2.0f;
            trigger.positionError = distanceFromAnimation;
            trigger.linearSpeed = linearSpeed;
            trigger.angularSpeed = angularSpeed;
            trigger.anchorDistance = anchorDistance;
            trigger.bindChainLength = bindChainLength;
            trigger.normalizedExtension = normalizedExtension;
            trigger.score = (trigger.immediate ? 800.0f : 400.0f) +
                normalizedExtension;
            mark(chainIndex, trigger);
        }
    }

    const std::span<const MmdJointDefinition> joints = this->asset->Joints();
    for (std::size_t jointIndex = 0U; jointIndex < joints.size(); ++jointIndex)
    {
        const MmdJointDefinition& joint = joints[jointIndex];
        if (joint.bodyA == InvalidRigidBodyIndex ||
            joint.bodyB == InvalidRigidBodyIndex ||
            joint.bodyA == joint.bodyB ||
            static_cast<std::size_t>(joint.bodyA) >= bodyModels.size() ||
            static_cast<std::size_t>(joint.bodyB) >= bodyModels.size() ||
            IsWideTravelHelperJoint(joint))
        {
            continue;
        }

        const std::size_t chainA = this->recoveryChainByBody[joint.bodyA];
        const std::size_t chainB = this->recoveryChainByBody[joint.bodyB];
        if (!finite[joint.bodyA] || !finite[joint.bodyB])
        {
            RecoveryTrigger triggerA;
            triggerA.reason = MmdPhysicsRecoveryReason::NonFiniteJoint;
            triggerA.seedBodyIndex = joint.bodyA;
            triggerA.jointIndex = jointIndex;
            triggerA.immediate = true;
            triggerA.score = 950.0f;
            RecoveryTrigger triggerB = triggerA;
            triggerB.seedBodyIndex = joint.bodyB;
            mark(chainA, triggerA);
            mark(chainB, triggerB);
            continue;
        }

        const glm::mat4 frameA = glm::inverse(
            this->asset->RigidBodyAt(joint.bodyA).modelBindTransform
        ) * joint.modelBindTransform;
        const glm::mat4 frameB = glm::inverse(
            this->asset->RigidBodyAt(joint.bodyB).modelBindTransform
        ) * joint.modelBindTransform;
        const glm::mat4 anchorA = bodyModels[joint.bodyA] * frameA;
        const glm::mat4 anchorB = bodyModels[joint.bodyB] * frameB;
        const auto [positionError, rotationError] =
            TransformError(anchorA, anchorB);
        (void)rotationError;
        const glm::mat4 relative = glm::inverse(anchorA) * anchorB;
        const glm::vec3 linearViolation = LinearLimitViolation(
            joint,
            glm::vec3(relative[3])
        );
        const glm::vec3 angularViolation = AngularLimitViolation(
            joint,
            EulerXyzFromRotation(relative)
        );
        const float maximumLinearViolation = std::max({
            linearViolation.x,
            linearViolation.y,
            linearViolation.z
        });
        const float maximumAngularViolationDegrees = glm::degrees(std::max({
            angularViolation.x,
            angularViolation.y,
            angularViolation.z
        }));
        const float severity = std::max({
            positionError / (RecoveryJointSeparation * scale),
            maximumLinearViolation / (RecoveryLinearViolation * scale),
            maximumAngularViolationDegrees /
                RecoveryAngularViolationDegrees
        });
        const float previousSeverity =
            this->recoveryJointSeverityHistory[jointIndex];
        this->recoveryJointSeverityHistory[jointIndex] = severity;
        const bool hard =
            positionError > RecoveryHardJointSeparation * scale ||
            maximumLinearViolation >
                RecoveryHardLinearViolation * scale ||
            maximumAngularViolationDegrees >
                RecoveryHardAngularViolationDegrees;
        const bool sustainedCandidate = severity > 1.0f &&
            (severity >= 1.5f ||
                previousSeverity >= 1.0f ||
                severity > previousSeverity + RecoverySeverityGrowthTolerance);
        if (!hard && !sustainedCandidate)
            continue;

        RecoveryTrigger triggerA;
        triggerA.reason = MmdPhysicsRecoveryReason::JointViolation;
        triggerA.seedBodyIndex = joint.bodyA;
        triggerA.jointIndex = jointIndex;
        // Joint solver error is deliberately never an immediate-reset reason.
        // Even large finite errors must persist in physical time; this avoids
        // mistaking one iterative solver excursion for a broken chain.
        triggerA.immediate = false;
        triggerA.score = (hard ? 600.0f : 200.0f) + severity;
        triggerA.positionError = positionError;
        triggerA.linearViolation = maximumLinearViolation;
        triggerA.angularViolationDegrees = maximumAngularViolationDegrees;
        RecoveryTrigger triggerB = triggerA;
        triggerB.seedBodyIndex = joint.bodyB;
        mark(chainA, triggerA);
        mark(chainB, triggerB);
    }

    for (std::size_t chainIndex = 0U;
         chainIndex < this->recoveryChains.size();
         ++chainIndex)
    {
        RecoveryChain& chain = this->recoveryChains[chainIndex];
        const RecoveryTrigger& trigger = triggers[chainIndex];
        if (trigger.reason == MmdPhysicsRecoveryReason::None)
        {
            chain.abnormalSeconds = 0.0f;
            chain.pendingTrigger = {};
            if (chain.fuseRemainingSeconds <= 0.0f)
                chain.fuseSuppressionLatched = false;
            continue;
        }

        const bool sameEpisode =
            chain.pendingTrigger.reason == trigger.reason &&
            chain.pendingTrigger.seedBodyIndex == trigger.seedBodyIndex &&
            chain.pendingTrigger.jointIndex == trigger.jointIndex;
        if (sameEpisode)
        {
            chain.abnormalSeconds += fixedTimeStep;
            if (trigger.score > chain.pendingTrigger.score)
                chain.pendingTrigger = trigger;
        }
        else
        {
            chain.abnormalSeconds = fixedTimeStep;
            chain.pendingTrigger = trigger;
            chain.fuseSuppressionLatched = false;
        }

        const float persistence =
            trigger.reason == MmdPhysicsRecoveryReason::HighVelocity
                ? RecoveryHighVelocityPersistenceSeconds
                : RecoveryPersistenceSeconds;
        const bool shouldRecover = trigger.immediate ||
            chain.abnormalSeconds >= persistence;
        if (!shouldRecover)
            continue;

        const bool emergencyNonFinite =
            trigger.reason == MmdPhysicsRecoveryReason::NonFinite ||
            trigger.reason == MmdPhysicsRecoveryReason::NonFiniteJoint;
        if (chain.fuseRemainingSeconds > 0.0f && !emergencyNonFinite)
        {
            if (!chain.fuseSuppressionLatched)
            {
                chain.fuseSuppressionLatched = true;
                ++this->recoveryStatistics.suppressedRecoveryCount;
                std::cout << "[MMD RECOVERY FUSE] chain=" << chainIndex
                          << " reason="
                          << RecoveryReasonName(trigger.reason)
                          << " remainingMs="
                          << chain.fuseRemainingSeconds * 1000.0f
                          << " suppressed="
                          << this->recoveryStatistics.suppressedRecoveryCount
                          << std::endl;
            }
            continue;
        }

        if (!trigger.immediate && chain.cooldownSeconds > 0.0f)
            continue;

        this->RecoverChain(chainIndex, chain.pendingTrigger);
        chain.abnormalSeconds = 0.0f;
        chain.pendingTrigger = {};
        chain.cooldownSeconds = RecoveryCooldownSeconds;
        chain.fuseSuppressionLatched = false;

        if (chain.fuseWindowSeconds <= 0.0f)
        {
            chain.fuseWindowSeconds = RecoveryFuseWindowSeconds;
            chain.recoveriesInWindow = 0U;
        }
        ++chain.recoveriesInWindow;
        if (chain.recoveriesInWindow >= RecoveryFuseLimit)
        {
            chain.fuseRemainingSeconds = RecoveryFuseDurationSeconds;
            ++this->recoveryStatistics.totalFuseTrips;
            std::cout << "[MMD RECOVERY FUSE] chain=" << chainIndex
                      << " tripped=true durationMs="
                      << RecoveryFuseDurationSeconds * 1000.0f
                      << " recoveriesInWindow="
                      << chain.recoveriesInWindow
                      << " totalTrips="
                      << this->recoveryStatistics.totalFuseTrips
                      << std::endl;
        }
    }

    this->UpdateRecoveryStatistics();
}

std::vector<std::size_t> MmdPhysicsInstance::CollectRecoveryRegion(
    std::size_t chainIndex,
    const RecoveryTrigger& trigger
) const
{
    std::vector<std::size_t> region;
    if (chainIndex >= this->recoveryChains.size())
        return region;

    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    std::vector<bool> visited(this->rigidBodies.size(), false);
    std::queue<std::pair<std::size_t, std::size_t>> pending;
    const auto enqueue = [&](std::size_t bodyIndex, std::size_t depth)
    {
        if (bodyIndex >= this->rigidBodies.size() || visited[bodyIndex] ||
            this->recoveryChainByBody[bodyIndex] != chainIndex)
        {
            return;
        }
        visited[bodyIndex] = true;
        pending.emplace(bodyIndex, depth);
    };

    if (trigger.jointIndex != invalidIndex &&
        trigger.jointIndex < this->asset->JointCount())
    {
        const MmdJointDefinition& joint =
            this->asset->JointAt(trigger.jointIndex);
        if (joint.bodyA != InvalidRigidBodyIndex)
            enqueue(joint.bodyA, 0U);
        if (joint.bodyB != InvalidRigidBodyIndex)
            enqueue(joint.bodyB, 0U);
    }
    enqueue(trigger.seedBodyIndex, 0U);
    if (pending.empty())
    {
        for (const std::size_t bodyIndex :
             this->recoveryChains[chainIndex].bodyIndices)
        {
            if (this->rigidBodies[bodyIndex].definition->mode !=
                MmdRigidBodyMode::FollowBone)
            {
                enqueue(bodyIndex, 0U);
                break;
            }
        }
    }

    std::size_t dynamicBodyCount = 0U;
    while (!pending.empty() && region.size() < RecoveryMaximumTotalBodies)
    {
        const auto [bodyIndex, depth] = pending.front();
        pending.pop();
        const bool dynamic = this->rigidBodies[bodyIndex].definition->mode !=
            MmdRigidBodyMode::FollowBone;
        if (dynamic && dynamicBodyCount >= RecoveryMaximumDynamicBodies)
            continue;

        region.push_back(bodyIndex);
        if (dynamic)
            ++dynamicBodyCount;
        if (depth >= RecoveryLocalGraphRadius)
            continue;
        if (!dynamic && depth > 0U)
            continue;

        for (const RecoveryEdge& edge : this->recoveryAdjacency[bodyIndex])
            enqueue(edge.bodyIndex, depth + 1U);
    }

    std::sort(region.begin(), region.end());
    region.erase(std::unique(region.begin(), region.end()), region.end());
    return region;
}

void MmdPhysicsInstance::RecoverChain(
    std::size_t chainIndex,
    const RecoveryTrigger& trigger
)
{
    if (chainIndex >= this->recoveryChains.size())
        return;
    RecoveryChain& chain = this->recoveryChains[chainIndex];
    const std::vector<std::size_t> region = this->CollectRecoveryRegion(
        chainIndex,
        trigger
    );
    if (region.empty())
        return;

    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    std::size_t anchorIndex = chain.anchorBodyIndex;
    if (anchorIndex >= this->rigidBodies.size())
        anchorIndex = region.front();

    const RuntimeBody& anchor = this->rigidBodies[anchorIndex];
    const glm::mat4 componentDelta =
        anchor.prePhysicsAnimatedModelTransform *
        glm::inverse(anchor.definition->modelBindTransform);

    for (const std::size_t bodyIndex : region)
    {
        RuntimeBody& runtime = this->rigidBodies[bodyIndex];
        const glm::mat4 targetModel =
            runtime.definition->mode == MmdRigidBodyMode::FollowBone
                ? runtime.prePhysicsAnimatedModelTransform
                : componentDelta * runtime.definition->modelBindTransform;
        const RigidTransform target = ModelToWorld(targetModel, entity);
        this->world->SetTransform(
            runtime.handle,
            target.position,
            target.rotation,
            true
        );
        runtime.lastAnimatedPosition = target.position;
        runtime.lastAnimatedRotation = target.rotation;
        runtime.frameStartAnimatedPosition = target.position;
        runtime.frameStartAnimatedRotation = target.rotation;
        runtime.frameTargetAnimatedPosition = target.position;
        runtime.frameTargetAnimatedRotation = target.rotation;
        runtime.hasAnimatedTransform = true;
    }

    ++this->recoveryStatistics.totalRecoveries;
    this->recoveryStatistics.recoveredBodyCount += region.size();
    this->recoveryStatistics.largestRecoveryRegion = std::max(
        this->recoveryStatistics.largestRecoveryRegion,
        region.size()
    );
    this->recoveryStatistics.lastRecoveredChain = chainIndex;
    this->recoveryStatistics.lastRecoveredBodyCount = region.size();
    this->recoveryStatistics.lastSeedBodyIndex = trigger.seedBodyIndex;
    this->recoveryStatistics.lastJointIndex = trigger.jointIndex;
    this->recoveryStatistics.lastReason = trigger.reason;
    this->recoveryStatistics.lastAbnormalSeconds = chain.abnormalSeconds;
    this->recoveryStatistics.lastPositionError = trigger.positionError;
    this->recoveryStatistics.lastLinearViolation = trigger.linearViolation;
    this->recoveryStatistics.lastAngularViolationDegrees =
        trigger.angularViolationDegrees;
    this->recoveryStatistics.lastLinearSpeed = trigger.linearSpeed;
    this->recoveryStatistics.lastAngularSpeed = trigger.angularSpeed;
    this->recoveryStatistics.lastAnchorDistance = trigger.anchorDistance;
    this->recoveryStatistics.lastBindChainLength = trigger.bindChainLength;
    this->recoveryStatistics.lastNormalizedExtension =
        trigger.normalizedExtension;

    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    std::cout << "[MMD RECOVERY] chain=" << chainIndex
              << " localBodies=" << region.size()
              << " chainBodies=" << chain.bodyIndices.size()
              << " reason=" << RecoveryReasonName(trigger.reason)
              << " seedBody=" << trigger.seedBodyIndex;
    if (trigger.seedBodyIndex < this->rigidBodies.size())
    {
        std::cout << ":\""
                  << this->rigidBodies[trigger.seedBodyIndex].definition->name
                  << "\"";
    }
    std::cout << " joint=";
    if (trigger.jointIndex == invalidIndex ||
        trigger.jointIndex >= this->asset->JointCount())
    {
        std::cout << "none";
    }
    else
    {
        std::cout << trigger.jointIndex << ":\""
                  << this->asset->JointAt(trigger.jointIndex).name
                  << "\"";
    }
    std::cout << " abnormalMs=" << chain.abnormalSeconds * 1000.0f
              << " positionError=" << trigger.positionError
              << " linearViolation=" << trigger.linearViolation
              << " angularViolationDeg="
              << trigger.angularViolationDegrees
              << " linearSpeed=" << trigger.linearSpeed
              << " angularSpeed=" << trigger.angularSpeed
              << " anchorDistance=" << trigger.anchorDistance
              << " bindChainLength=" << trigger.bindChainLength
              << " normalizedExtension=" << trigger.normalizedExtension
              << " total=" << this->recoveryStatistics.totalRecoveries
              << std::endl;
}

void MmdPhysicsInstance::UpdateRecoveryStatistics() noexcept
{
    this->recoveryStatistics.pendingAbnormalChainCount = 0U;
    this->recoveryStatistics.cooldownChainCount = 0U;
    this->recoveryStatistics.fusedChainCount = 0U;
    for (const RecoveryChain& chain : this->recoveryChains)
    {
        if (chain.abnormalSeconds > 0.0f)
            ++this->recoveryStatistics.pendingAbnormalChainCount;
        if (chain.cooldownSeconds > 0.0f)
            ++this->recoveryStatistics.cooldownChainCount;
        if (chain.fuseRemainingSeconds > 0.0f)
            ++this->recoveryStatistics.fusedChainCount;
    }
}

MmdPhysicsInstance::JointSnapshot
MmdPhysicsInstance::CaptureJointSnapshot(
    const char* stage,
    std::size_t completedSteps
) const
{
    JointSnapshot snapshot;
    snapshot.stage = stage;
    snapshot.completedSteps = completedSteps;
    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    std::vector<glm::mat4> bodyModels;
    bodyModels.reserve(this->rigidBodies.size());
    for (const RuntimeBody& runtime : this->rigidBodies)
    {
        const PhysicsBodyState state = this->world->State(runtime.handle);
        const bool finite = std::isfinite(state.position.x) &&
            std::isfinite(state.position.y) &&
            std::isfinite(state.position.z) &&
            std::isfinite(state.rotation.w) &&
            std::isfinite(state.rotation.x) &&
            std::isfinite(state.rotation.y) &&
            std::isfinite(state.rotation.z);
        if (!finite)
        {
            snapshot.finite = false;
            bodyModels.push_back(glm::mat4(1.0f));
            continue;
        }
        bodyModels.push_back(WorldToModel(state, entity));
    }

    const std::span<const MmdJointDefinition> joints = this->asset->Joints();
    for (std::size_t index = 0U; index < joints.size(); ++index)
    {
        const MmdJointDefinition& joint = joints[index];
        if (joint.bodyA == InvalidRigidBodyIndex ||
            joint.bodyB == InvalidRigidBodyIndex ||
            joint.bodyA == joint.bodyB ||
            static_cast<std::size_t>(joint.bodyA) >= bodyModels.size() ||
            static_cast<std::size_t>(joint.bodyB) >= bodyModels.size())
        {
            continue;
        }
        const glm::mat4 frameA = glm::inverse(
            this->asset->RigidBodyAt(joint.bodyA).modelBindTransform
        ) * joint.modelBindTransform;
        const glm::mat4 frameB = glm::inverse(
            this->asset->RigidBodyAt(joint.bodyB).modelBindTransform
        ) * joint.modelBindTransform;
        const glm::mat4 anchorA = bodyModels[joint.bodyA] * frameA;
        const glm::mat4 anchorB = bodyModels[joint.bodyB] * frameB;
        const auto [positionError, rotationError] = TransformError(
            anchorA,
            anchorB
        );
        const glm::mat4 relative = glm::inverse(anchorA) * anchorB;
        const glm::vec3 relativePosition = glm::vec3(relative[3]);
        const glm::vec3 relativeEuler = EulerXyzFromRotation(relative);
        const glm::vec3 linearViolation = LinearLimitViolation(
            joint,
            relativePosition
        );
        const bool wideTravelHelper = IsWideTravelHelperJoint(joint);
        const glm::vec3 angularViolation = AngularLimitViolation(
            joint,
            relativeEuler
        );
        const float maximumLinearViolation = std::max({
            linearViolation.x,
            linearViolation.y,
            linearViolation.z
        });
        const float maximumAngularViolationDegrees = glm::degrees(std::max({
            angularViolation.x,
            angularViolation.y,
            angularViolation.z
        }));
        if (!std::isfinite(positionError) ||
            !std::isfinite(rotationError) ||
            !std::isfinite(maximumLinearViolation) ||
            !std::isfinite(maximumAngularViolationDegrees))
        {
            snapshot.finite = false;
            ++snapshot.jointsOverFailureThreshold;
            continue;
        }
        if (positionError > snapshot.maximumPositionSeparation)
        {
            snapshot.maximumPositionSeparation = positionError;
            snapshot.maximumJointIndex = index;
        }
        snapshot.maximumRotationErrorDegrees = std::max(
            snapshot.maximumRotationErrorDegrees,
            rotationError
        );
        if (maximumLinearViolation > snapshot.maximumLinearLimitViolation)
        {
            snapshot.maximumLinearLimitViolation = maximumLinearViolation;
            snapshot.maximumLinearViolationJointIndex = index;
        }
        if (maximumAngularViolationDegrees >
            snapshot.maximumAngularLimitViolationDegrees)
        {
            snapshot.maximumAngularLimitViolationDegrees =
                maximumAngularViolationDegrees;
            snapshot.maximumAngularViolationJointIndex = index;
        }
        if (wideTravelHelper)
        {
            ++snapshot.wideTravelHelperJoints;
            continue;
        }
        snapshot.maximumStabilizationLinearViolation = std::max(
            snapshot.maximumStabilizationLinearViolation,
            maximumLinearViolation
        );
        snapshot.maximumStabilizationAngularViolationDegrees = std::max(
            snapshot.maximumStabilizationAngularViolationDegrees,
            maximumAngularViolationDegrees
        );
        if (maximumLinearViolation > JointFailureLinearViolation ||
            maximumAngularViolationDegrees >
                JointFailureAngularViolationDegrees)
        {
            ++snapshot.jointsOverFailureThreshold;
        }
    }
    return snapshot;
}

void MmdPhysicsInstance::LogJointSnapshot(
    const JointSnapshot& snapshot
) const
{
    std::cout << "[MMD INIT STAGE] stage=" << snapshot.stage
              << " steps=" << snapshot.completedSteps
              << " finite=" << (snapshot.finite ? "true" : "false")
              << " maxJointPos=" << snapshot.maximumPositionSeparation
              << " maxJointRotDeg="
              << snapshot.maximumRotationErrorDegrees
              << " maxLinearViolation="
              << snapshot.maximumLinearLimitViolation
              << " maxAngularViolationDeg="
              << snapshot.maximumAngularLimitViolationDegrees
              << " stabilizationLinearViolation="
              << snapshot.maximumStabilizationLinearViolation
              << " stabilizationAngularViolationDeg="
              << snapshot.maximumStabilizationAngularViolationDegrees
              << " ignoredWideTravelJoints="
              << snapshot.wideTravelHelperJoints
              << " severeJoints="
              << snapshot.jointsOverFailureThreshold;
    if (snapshot.maximumJointIndex < this->asset->JointCount())
    {
        const MmdJointDefinition& joint = this->asset->JointAt(
            snapshot.maximumJointIndex
        );
        std::cout << " jointIndex=" << snapshot.maximumJointIndex
                  << " joint=\"" << joint.name << "\"";
        if (joint.bodyA != InvalidRigidBodyIndex)
        {
            std::cout << " bodyA=\""
                      << this->asset->RigidBodyAt(joint.bodyA).name
                      << "\"";
        }
        if (joint.bodyB != InvalidRigidBodyIndex)
        {
            std::cout << " bodyB=\""
                      << this->asset->RigidBodyAt(joint.bodyB).name
                      << "\"";
        }
    }
    const auto logViolationJoint = [this](
        const char* label,
        std::size_t index
    )
    {
        if (index >= this->asset->JointCount())
            return;
        const MmdJointDefinition& joint = this->asset->JointAt(index);
        std::cout << ' ' << label << "Index=" << index
                  << ' ' << label << "=\"" << joint.name << "\"";
    };
    logViolationJoint(
        "linearViolationJoint",
        snapshot.maximumLinearViolationJointIndex
    );
    logViolationJoint(
        "angularViolationJoint",
        snapshot.maximumAngularViolationJointIndex
    );
    std::cout << std::endl;
}

void MmdPhysicsInstance::SetFailureFreeze(bool frozen)
{
    this->stabilizationFailed = frozen;
    const EntityFrame entity = ExtractEntityFrame(*this->transform);
    for (RuntimeBody& runtime : this->rigidBodies)
    {
        if (runtime.definition->mode == MmdRigidBodyMode::FollowBone)
            continue;
        this->world->SetLinearFactor(
            runtime.handle,
            frozen ? glm::vec3(0.0f) : glm::vec3(1.0f)
        );
        this->world->SetAngularFactor(
            runtime.handle,
            frozen ? glm::vec3(0.0f) : glm::vec3(1.0f)
        );
        if (frozen)
        {
            const RigidTransform target = ModelToWorld(
                runtime.prePhysicsAnimatedModelTransform,
                entity
            );
            this->world->SetTransform(
                runtime.handle,
                target.position,
                target.rotation,
                true
            );
        }
        else
        {
            this->world->Activate(runtime.handle);
        }
    }
}

void MmdPhysicsInstance::DestroyRuntime() noexcept
{
    if (this->world == nullptr)
        return;
    for (auto iterator = this->constraints.rbegin();
         iterator != this->constraints.rend();
         ++iterator)
    {
        this->world->DestroyConstraint(*iterator);
    }
    this->constraints.clear();
    for (auto iterator = this->rigidBodies.rbegin();
         iterator != this->rigidBodies.rend();
         ++iterator)
    {
        this->world->DestroyBody(iterator->handle);
    }
    this->rigidBodies.clear();
    this->runtimeBodyByWorldHandle.clear();
    this->contactDiagnostics.clear();
}
