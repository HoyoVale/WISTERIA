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
#include <string_view>

namespace
{
constexpr std::size_t MmdStabilizationSteps = 30U;
constexpr float MmdStabilizationTimeStep = 1.0f / 60.0f;
constexpr float JointWarmupLinearViolation = 0.1f;
constexpr float JointWarmupAngularViolationDegrees = 5.0f;
constexpr float JointFailureLinearViolation = 0.5f;
constexpr float JointFailureAngularViolationDegrees = 45.0f;

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
    this->localMatrixScratch.resize(boneCount);
    this->globalMatrixScratch.resize(boneCount);

    try
    {
        for (const MmdRigidBodyDefinition& definition : asset.RigidBodies())
        {
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
            description.restitution = definition.restitution;
            description.friction = definition.friction;
            description.collisionGroup = static_cast<std::uint16_t>(
                1U << definition.collisionGroup
            );
            description.collisionMask = static_cast<std::uint16_t>(
                ~definition.nonCollisionMask
            );
            const std::size_t runtimeIndex = this->rigidBodies.size();
            RuntimeBody runtime;
            runtime.definition = &definition;
            runtime.handle = world.CreateBody(description);
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
            this->rigidBodies.push_back(std::move(runtime));
            if (definition.mode != MmdRigidBodyMode::FollowBone &&
                definition.bone != InvalidBoneIndex)
            {
                this->drivenRuntimeBodyByBone[definition.bone] = runtimeIndex;
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
    this->PrePhysicsUpdate(*this->transform, deltaTime);
    if (!this->suppressImpulseMorphOnce &&
        this->morphState != nullptr &&
        this->morphState->GetMorphSet().HasKind(MorphKind::Impulse))
    {
        this->ApplyImpulseMorphs(*this->morphState);
    }
    this->suppressImpulseMorphOnce = false;
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
    if (this->debugOverlay == MmdPhysicsDebugOverlay::Off)
        return;

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
    constexpr float positionEpsilonSquared = 0.000001f;
    constexpr float rotationDotEpsilon = 0.000001f;
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

        const glm::vec3 positionDelta =
            animated.position - runtime.lastAnimatedPosition;
        const bool positionChanged = !runtime.hasAnimatedTransform ||
            glm::dot(positionDelta, positionDelta) > positionEpsilonSquared;
        const bool rotationChanged = !runtime.hasAnimatedTransform ||
            1.0f - std::abs(glm::dot(
                animated.rotation,
                runtime.lastAnimatedRotation
            )) > rotationDotEpsilon;

        if (positionChanged || rotationChanged || this->stabilizationFailed)
        {
            this->world->SetTransform(
                runtime.handle,
                animated.position,
                animated.rotation,
                this->stabilizationFailed
            );
        }

        runtime.lastAnimatedPosition = animated.position;
        runtime.lastAnimatedRotation = animated.rotation;
        runtime.hasAnimatedTransform = true;
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
            // PMX mode 2 keeps the animation-authored bone translation while
            // taking the final orientation from the freely simulated rigid
            // body. The Bullet body itself must remain fully dynamic: locking
            // its linear factor or teleporting it each frame prevents gravity
            // and joint chains from reproducing MMD hair and clothing motion.
            BoneTransform animatedBone = BoneTransform::FromMatrix(
                currentGlobals[boneIndex]
            );
            const BoneTransform physicsBone = BoneTransform::FromMatrix(
                drivenGlobal
            );
            animatedBone.rotation = physicsBone.rotation;
            this->globalMatrixScratch[boneIndex] = animatedBone.Matrix();
        }
        else
        {
            this->globalMatrixScratch[boneIndex] = drivenGlobal;
        }
        this->localMatrixScratch[boneIndex] =
            glm::inverse(parentGlobal) * this->globalMatrixScratch[boneIndex];
    }

    this->pose->SetLocalMatrices(this->localMatrixScratch);
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

    const JointSnapshot afterReset = this->CaptureJointSnapshot(
        "after-reset"
    );
    this->jointSnapshots.push_back(afterReset);
    this->pendingStabilizationSteps = 0U;
    this->resetTargetRefreshPending = true;
    this->suppressImpulseMorphOnce = true;
    this->BuildAlignmentDiagnostics();

    if (this->rigidBodies.size() >= 32U)
    {
        this->LogJointSnapshot(beforeReset);
        this->LogJointSnapshot(afterReset);
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
}
