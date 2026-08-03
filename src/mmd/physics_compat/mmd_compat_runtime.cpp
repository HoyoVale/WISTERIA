#include "wisteria/mmd/physics_compat/mmd_compat_runtime.hpp"

#include "wisteria/animation/pose.hpp"
#include "wisteria/animation/skeleton.hpp"
#include "wisteria/core/transform.hpp"
#include "wisteria/mmd/physics/mmd_physics_asset.hpp"
#include "wisteria/physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
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
            "MMD compat physics requires uniform positive Entity scale"
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

struct MmdCompatRuntime::Impl
{
    struct Body
    {
        PhysicsBodyHandle handle{};
        const MmdRigidBodyDefinition* definition = nullptr;
        PhysicsMotionType motionType = PhysicsMotionType::Static;
        BoneIndex boneIndex = InvalidBoneIndex;
        glm::mat4 boneToBody{1.0f};
        glm::mat4 bodyToBone{1.0f};
        glm::mat4 prePhysicsAnimatedModelTransform{1.0f};
    };

    PhysicsWorld& world;
    const MmdPhysicsAsset& asset;
    Pose& pose;
    const Transform& transform;
    MmdCompatSettings settings;
    bool created = false;

    std::vector<Body> bodies;
    std::vector<PhysicsConstraintHandle> joints;
    std::vector<std::size_t> drivenRuntimeBodyByBone;
    std::vector<glm::mat4> localMatrixScratch;
    std::vector<glm::mat4> globalMatrixScratch;

    Impl(
        PhysicsWorld& world_,
        const MmdPhysicsAsset& asset_,
        Pose& pose_,
        const Transform& transform_,
        const MmdCompatSettings& settings_
    )
        : world(world_),
          asset(asset_),
          pose(pose_),
          transform(transform_),
          settings(settings_)
    {
    }

    void DestroyObjects() noexcept
    {
        for (const PhysicsConstraintHandle joint : joints)
            this->world.DestroyConstraint(joint);
        joints.clear();
        for (const Body& body : bodies)
            this->world.DestroyBody(body.handle);
        bodies.clear();
        this->drivenRuntimeBodyByBone.clear();
        this->localMatrixScratch.clear();
        this->globalMatrixScratch.clear();
        this->created = false;
    }
};

MmdCompatRuntime::MmdCompatRuntime(
    PhysicsWorld& world,
    const MmdPhysicsAsset& asset,
    Pose& pose,
    const Transform& transform,
    const MmdCompatSettings& settings
)
    : impl(std::make_unique<Impl>(
          world,
          asset,
          pose,
          transform,
          settings
      ))
{
}

MmdCompatRuntime::~MmdCompatRuntime()
{
    this->Destroy();
}

MmdCompatRuntime::MmdCompatRuntime(MmdCompatRuntime&&) noexcept = default;
MmdCompatRuntime& MmdCompatRuntime::operator=(MmdCompatRuntime&&) noexcept =
    default;

bool MmdCompatRuntime::Create()
{
    try
    {
        this->Destroy();

        const EntityFrame entity = ExtractEntityFrame(this->impl->transform);
        const std::size_t boneCount = this->impl->pose.BoneCount();
        const std::size_t invalidIndex =
            std::numeric_limits<std::size_t>::max();

        this->impl->bodies.reserve(this->impl->asset.RigidBodyCount());
        this->impl->joints.reserve(this->impl->asset.JointCount());
        this->impl->drivenRuntimeBodyByBone.assign(
            boneCount,
            invalidIndex
        );
        this->impl->localMatrixScratch.resize(boneCount);
        this->impl->globalMatrixScratch.resize(boneCount);

        for (const MmdRigidBodyDefinition& definition :
             this->impl->asset.RigidBodies())
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
            description.disableDeactivation =
                this->impl->settings.disableDynamicDeactivation;
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

            const PhysicsBodyHandle handle = this->impl->world.CreateBody(
                description
            );

            Impl::Body body;
            body.handle = handle;
            body.definition = &definition;
            body.motionType = description.motionType;
            body.boneIndex = definition.bone;
            body.boneToBody = definition.boneToBody;
            body.bodyToBone = definition.bodyToBone;
            body.prePhysicsAnimatedModelTransform =
                definition.modelBindTransform;
            const std::size_t runtimeIndex = this->impl->bodies.size();
            this->impl->bodies.push_back(std::move(body));

            if (definition.mode != MmdRigidBodyMode::FollowBone &&
                definition.bone != InvalidBoneIndex &&
                definition.bone < boneCount)
            {
                this->impl->drivenRuntimeBodyByBone[definition.bone] =
                    runtimeIndex;
            }
        }

        const bool disableLinkedCollisions =
            this->impl->settings.disableLinkedBodyCollisions;
        for (const MmdJointDefinition& joint : this->impl->asset.Joints())
        {
            if (joint.bodyA == InvalidRigidBodyIndex ||
                joint.bodyB == InvalidRigidBodyIndex ||
                joint.bodyA == joint.bodyB ||
                joint.bodyA >= this->impl->bodies.size() ||
                joint.bodyB >= this->impl->bodies.size())
            {
                continue;
            }

            const PhysicsBodyHandle bodyA =
                this->impl->bodies[joint.bodyA].handle;
            const PhysicsBodyHandle bodyB =
                this->impl->bodies[joint.bodyB].handle;
            const RigidTransform jointWorld = ModelToWorld(
                joint.modelBindTransform,
                entity
            );
            const glm::mat4 jointWorldMatrix = ToMatrix(jointWorld);

            const auto bodyWorldMatrix = [&](RigidBodyIndex index)
            {
                return ToMatrix(ModelToWorld(
                    this->impl->asset.RigidBodyAt(index).modelBindTransform,
                    entity
                ));
            };

            PhysicsConstraintFrame frameA{};
            PhysicsConstraintFrame frameB{};
            if (joint.bodyA != InvalidRigidBodyIndex)
            {
                frameA = ConstraintFrameFromMatrix(
                    glm::inverse(bodyWorldMatrix(joint.bodyA)) *
                    jointWorldMatrix
                );
            }
            if (joint.bodyB != InvalidRigidBodyIndex)
            {
                frameB = ConstraintFrameFromMatrix(
                    glm::inverse(bodyWorldMatrix(joint.bodyB)) *
                    jointWorldMatrix
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
                description.linearLower =
                    joint.linearLower * entity.scale;
                description.linearUpper =
                    joint.linearUpper * entity.scale;
                description.angularLower = joint.angularLower;
                description.angularUpper = joint.angularUpper;
                description.linearStiffness = joint.linearSpring;
                description.angularStiffness = joint.angularSpring;
                description.useLegacySpringConstraint =
                    this->impl->settings.legacySpringConstraint;
                description.disableOffsetForConstraintFrame =
                    this->impl->settings.disableOffsetForConstraintFrame;
                description.constraintStopErp =
                    this->impl->settings.constraintStopErp;
                description.disableCollisionsBetweenLinkedBodies =
                    disableLinkedCollisions;
                handle = this->impl->world.CreateSpring6DofConstraint(
                    description
                );
                break;
            }
            case MmdJointType::SixDof:
            {
                PhysicsSixDofDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                description.linearLower =
                    joint.linearLower * entity.scale;
                description.linearUpper =
                    joint.linearUpper * entity.scale;
                description.angularLower = joint.angularLower;
                description.angularUpper = joint.angularUpper;
                description.bullet275Mode =
                    this->impl->settings.disableOffsetForConstraintFrame;
                description.constraintStopErp =
                    this->impl->settings.constraintStopErp;
                description.disableCollisionsBetweenLinkedBodies =
                    disableLinkedCollisions;
                handle = this->impl->world.CreateSixDofConstraint(
                    description
                );
                break;
            }
            case MmdJointType::PointToPoint:
            {
                PhysicsPointToPointDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.pivotA = frameA.position;
                description.pivotB = frameB.position;
                description.disableCollisionsBetweenLinkedBodies =
                    disableLinkedCollisions;
                handle = this->impl->world.CreatePointToPointConstraint(
                    description
                );
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
                description.disableCollisionsBetweenLinkedBodies =
                    disableLinkedCollisions;
                handle = this->impl->world.CreateConeTwistConstraint(
                    description
                );
                break;
            }
            case MmdJointType::Slider:
            {
                PhysicsSliderDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                description.linearLower =
                    joint.linearLower.x * entity.scale;
                description.linearUpper =
                    joint.linearUpper.x * entity.scale;
                description.angularLower = joint.angularLower.x;
                description.angularUpper = joint.angularUpper.x;
                description.disableCollisionsBetweenLinkedBodies =
                    disableLinkedCollisions;
                handle = this->impl->world.CreateSliderConstraint(
                    description
                );
                break;
            }
            case MmdJointType::Hinge:
            {
                PhysicsHingeDesc description;
                description.bodyA = bodyA;
                description.bodyB = bodyB;
                description.frameA = frameA;
                description.frameB = frameB;
                description.lowerAngle = joint.angularLower.z;
                description.upperAngle = joint.angularUpper.z;
                description.disableCollisionsBetweenLinkedBodies =
                    disableLinkedCollisions;
                handle = this->impl->world.CreateHingeConstraint(
                    description
                );
                break;
            }
            }
            this->impl->joints.push_back(handle);
        }

        this->impl->created = true;
        return true;
    }
    catch (const std::exception&)
    {
        this->impl->DestroyObjects();
        return false;
    }
}

void MmdCompatRuntime::Destroy() noexcept
{
    this->impl->DestroyObjects();
}

void MmdCompatRuntime::UpdateFromBones()
{
    if (!this->impl->created)
        return;
    const EntityFrame entity = ExtractEntityFrame(this->impl->transform);
    for (Impl::Body& body : this->impl->bodies)
    {
        body.prePhysicsAnimatedModelTransform =
            AnimatedBodyModelTransform(*body.definition, this->impl->pose);
    }
    for (const Impl::Body& body : this->impl->bodies)
    {
        if (body.motionType != PhysicsMotionType::Kinematic ||
            body.boneIndex == InvalidBoneIndex)
        {
            continue;
        }
        const RigidTransform target = ModelToWorld(
            body.prePhysicsAnimatedModelTransform,
            entity
        );
        this->impl->world.SetTransform(
            body.handle,
            target.position,
            target.rotation,
            true
        );
    }
}

void MmdCompatRuntime::Step(float deltaTime)
{
    if (!this->impl->created)
        return;
    this->impl->world.StepSimulation(
        deltaTime,
        this->impl->settings.maxSubSteps,
        this->impl->settings.fixedTimeStep
    );
}

void MmdCompatRuntime::UpdateBones()
{
    if (!this->impl->created)
        return;
    const EntityFrame entity = ExtractEntityFrame(this->impl->transform);
    const Skeleton& skeleton = this->impl->pose.GetSkeleton();
    const std::span<const glm::mat4> currentLocals =
        this->impl->pose.LocalMatrices();
    const std::span<const glm::mat4> currentGlobals =
        this->impl->pose.GlobalMatrices();
    std::copy(
        currentLocals.begin(),
        currentLocals.end(),
        this->impl->localMatrixScratch.begin()
    );
    std::copy(
        currentGlobals.begin(),
        currentGlobals.end(),
        this->impl->globalMatrixScratch.begin()
    );

    const std::size_t invalidIndex =
        std::numeric_limits<std::size_t>::max();
    for (BoneIndex boneIndex : skeleton.EvaluationOrder())
    {
        const Bone& bone = skeleton.BoneAt(boneIndex);
        const glm::mat4 parentGlobal = bone.parentIndex == InvalidBoneIndex
            ? glm::mat4(1.0f)
            : this->impl->globalMatrixScratch[bone.parentIndex];
        const std::size_t runtimeIndex =
            this->impl->drivenRuntimeBodyByBone[boneIndex];
        if (runtimeIndex == invalidIndex)
        {
            this->impl->globalMatrixScratch[boneIndex] =
                parentGlobal * this->impl->localMatrixScratch[boneIndex];
            continue;
        }

        const Impl::Body& body = this->impl->bodies[runtimeIndex];
        const MmdRigidBodyDefinition& definition = *body.definition;
        const glm::mat4 bodyModel = WorldToModel(
            this->impl->world.State(body.handle),
            entity
        );
        const glm::mat4 drivenModel = bodyModel * body.bodyToBone;
        const glm::mat4 drivenGlobal =
            glm::inverse(skeleton.InverseRootMatrix()) * drivenModel;

        if (definition.mode == MmdRigidBodyMode::PhysicsWithBone)
        {
            const BoneTransform animatedBone = BoneTransform::FromMatrix(
                currentGlobals[boneIndex]
            );
            const BoneTransform physicsBone = BoneTransform::FromMatrix(
                drivenGlobal
            );
            switch (this->impl->settings.physicsWithBoneSync)
            {
            case MmdPhysicsWithBoneSyncMode::RotationOnly:
            {
                BoneTransform result = animatedBone;
                result.rotation = physicsBone.rotation;
                this->impl->globalMatrixScratch[boneIndex] = result.Matrix();
                break;
            }
            case MmdPhysicsWithBoneSyncMode::FullBody:
                this->impl->globalMatrixScratch[boneIndex] = drivenGlobal;
                break;
            case MmdPhysicsWithBoneSyncMode::TranslationDelta:
            {
                const glm::vec3 bodyTranslationDeltaModel =
                    glm::vec3(bodyModel[3]) -
                    glm::vec3(body.prePhysicsAnimatedModelTransform[3]);
                const glm::mat3 modelToSkeletonRoot(
                    glm::inverse(skeleton.InverseRootMatrix())
                );
                BoneTransform result = animatedBone;
                result.translation +=
                    modelToSkeletonRoot * bodyTranslationDeltaModel;
                result.rotation = physicsBone.rotation;
                this->impl->globalMatrixScratch[boneIndex] = result.Matrix();
                break;
            }
            }
        }
        else
        {
            this->impl->globalMatrixScratch[boneIndex] = drivenGlobal;
        }
        this->impl->localMatrixScratch[boneIndex] =
            glm::inverse(parentGlobal) *
            this->impl->globalMatrixScratch[boneIndex];
    }

    this->impl->pose.SetLocalMatrices(this->impl->localMatrixScratch);
}

void MmdCompatRuntime::Update(float deltaTime)
{
    this->UpdateFromBones();
    this->Step(deltaTime);
    this->UpdateBones();
}

void MmdCompatRuntime::Reset()
{
    if (!this->impl->created)
        return;
    const EntityFrame entity = ExtractEntityFrame(this->impl->transform);

    // Saba-style reset: all bodies become kinematic, snap to the current bone
    // pose, run one warmup tick, then restore dynamics and clear velocities.
    for (const Impl::Body& body : this->impl->bodies)
    {
        this->impl->world.SetKinematic(body.handle, true);
        const RigidTransform target = ModelToWorld(
            AnimatedBodyModelTransform(*body.definition, this->impl->pose),
            entity
        );
        this->impl->world.SetTransform(
            body.handle,
            target.position,
            target.rotation,
            true
        );
    }
    this->impl->world.StepSimulation(1.0f / 60.0f, 1, 1.0f / 60.0f);
    for (const Impl::Body& body : this->impl->bodies)
    {
        this->impl->world.SetKinematic(body.handle, false);
        this->impl->world.ClearDynamics(body.handle);
    }
}

void MmdCompatRuntime::SetGravity(const glm::vec3& gravity)
{
    this->impl->settings.gravity = gravity;
    this->impl->world.SetGravity(gravity);
}

std::size_t MmdCompatRuntime::RigidBodyCount() const noexcept
{
    return this->impl->bodies.size();
}

std::size_t MmdCompatRuntime::JointCount() const noexcept
{
    return this->impl->joints.size();
}

PhysicsBodyState MmdCompatRuntime::BodyStateAt(RigidBodyIndex index) const
{
    if (index >= this->impl->bodies.size())
        throw std::out_of_range("MMD compat rigid-body index is out of range");
    return this->impl->world.State(this->impl->bodies[index].handle);
}

MmdCompatJointDiagnostics MmdCompatRuntime::JointDiagnostics() const
{
    MmdCompatJointDiagnostics diagnostics;
    if (!this->impl->created)
        return diagnostics;

    const EntityFrame entity = ExtractEntityFrame(this->impl->transform);
    std::vector<glm::mat4> bodyModels;
    bodyModels.reserve(this->impl->bodies.size());
    for (const Impl::Body& body : this->impl->bodies)
    {
        const PhysicsBodyState state = this->impl->world.State(body.handle);
        const bool finite = std::isfinite(state.position.x) &&
            std::isfinite(state.position.y) &&
            std::isfinite(state.position.z) &&
            std::isfinite(state.rotation.w) &&
            std::isfinite(state.rotation.x) &&
            std::isfinite(state.rotation.y) &&
            std::isfinite(state.rotation.z);
        if (!finite)
        {
            diagnostics.finite = false;
            bodyModels.push_back(glm::mat4(1.0f));
            continue;
        }
        bodyModels.push_back(WorldToModel(state, entity));
    }

    const std::span<const MmdJointDefinition> joints =
        this->impl->asset.Joints();
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
            this->impl->asset.RigidBodyAt(joint.bodyA).modelBindTransform
        ) * joint.modelBindTransform;
        const glm::mat4 frameB = glm::inverse(
            this->impl->asset.RigidBodyAt(joint.bodyB).modelBindTransform
        ) * joint.modelBindTransform;
        const glm::mat4 anchorA = bodyModels[joint.bodyA] * frameA;
        const glm::mat4 anchorB = bodyModels[joint.bodyB] * frameB;
        const auto [positionError, rotationError] =
            TransformError(anchorA, anchorB);
        const glm::mat4 relative = glm::inverse(anchorA) * anchorB;
        const glm::vec3 relativePosition = glm::vec3(relative[3]);
        const glm::vec3 relativeEuler = EulerXyzFromRotation(relative);
        const glm::vec3 linearViolation = LinearLimitViolation(
            joint,
            relativePosition
        );
        const glm::vec3 angularViolation = AngularLimitViolation(
            joint,
            relativeEuler
        );
        const float maximumLinearViolation = std::max({
            linearViolation.x,
            linearViolation.y,
            linearViolation.z
        });
        const float maximumAngularViolationDegrees = glm::degrees(
            std::max({
                angularViolation.x,
                angularViolation.y,
                angularViolation.z
            })
        );
        if (!std::isfinite(positionError) ||
            !std::isfinite(rotationError) ||
            !std::isfinite(maximumLinearViolation) ||
            !std::isfinite(maximumAngularViolationDegrees))
        {
            diagnostics.finite = false;
            ++diagnostics.jointsOverFailureThreshold;
            continue;
        }
        diagnostics.maximumPositionSeparation = std::max(
            diagnostics.maximumPositionSeparation,
            positionError
        );
        diagnostics.maximumRotationErrorDegrees = std::max(
            diagnostics.maximumRotationErrorDegrees,
            rotationError
        );
        diagnostics.maximumLinearLimitViolation = std::max(
            diagnostics.maximumLinearLimitViolation,
            maximumLinearViolation
        );
        diagnostics.maximumAngularLimitViolationDegrees = std::max(
            diagnostics.maximumAngularLimitViolationDegrees,
            maximumAngularViolationDegrees
        );
        if (IsWideTravelHelperJoint(joint))
        {
            ++diagnostics.wideTravelHelperJoints;
            continue;
        }
        if (maximumLinearViolation >
                this->impl->settings.failureLinearViolation ||
            maximumAngularViolationDegrees >
                this->impl->settings.failureAngularViolationDegrees)
        {
            ++diagnostics.jointsOverFailureThreshold;
        }
    }
    return diagnostics;
}

void MmdCompatRuntime::ApplyCentralImpulse(
    RigidBodyIndex index,
    const glm::vec3& impulse
)
{
    if (index >= this->impl->bodies.size())
        throw std::out_of_range("MMD compat rigid-body index is out of range");
    this->impl->world.ApplyCentralImpulse(
        this->impl->bodies[index].handle,
        impulse
    );
}

void MmdCompatRuntime::ApplyTorqueImpulse(
    RigidBodyIndex index,
    const glm::vec3& impulse
)
{
    if (index >= this->impl->bodies.size())
        throw std::out_of_range("MMD compat rigid-body index is out of range");
    this->impl->world.ApplyTorqueImpulse(
        this->impl->bodies[index].handle,
        impulse
    );
}
