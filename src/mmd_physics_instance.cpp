#include "pch.hpp"
#include "mmd_physics_instance.hpp"
#include "physics_world.hpp"
#include "pose.hpp"
#include "transform.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

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
    return pose.GlobalMatrix(definition.bone) * definition.boneToBody;
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
            this->rigidBodies.push_back(RuntimeBody{
                &definition,
                world.CreateBody(description),
                initial.position,
                initial.rotation,
                true
            });
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
        this->ResetToPose(transform);
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
    if (this->morphState != nullptr &&
        this->morphState->GetMorphSet().HasKind(MorphKind::Impulse))
    {
        this->ApplyImpulseMorphs(*this->morphState);
    }
}

void MmdPhysicsInstance::FinishSimulation()
{
    this->PostPhysicsUpdate(*this->transform);
}

void MmdPhysicsInstance::ResetSimulation()
{
    this->ResetToPose(*this->transform);
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
        const MmdRigidBodyDefinition& definition = *runtime.definition;
        if (definition.mode != MmdRigidBodyMode::FollowBone)
            continue;

        const RigidTransform animated = ModelToWorld(
            AnimatedBodyModelTransform(definition, *this->pose),
            entity
        );
        const glm::vec3 positionDelta =
            animated.position - runtime.lastAnimatedPosition;
        const bool positionChanged = !runtime.hasAnimatedTransform ||
            glm::dot(positionDelta, positionDelta) > positionEpsilonSquared;
        const bool rotationChanged = !runtime.hasAnimatedTransform ||
            1.0f - std::abs(glm::dot(
                animated.rotation,
                runtime.lastAnimatedRotation
            )) > rotationDotEpsilon;

        if (positionChanged || rotationChanged)
        {
            this->world->SetTransform(
                runtime.handle,
                animated.position,
                animated.rotation,
                false
            );
        }

        runtime.lastAnimatedPosition = animated.position;
        runtime.lastAnimatedRotation = animated.rotation;
        runtime.hasAnimatedTransform = true;
    }
}

void MmdPhysicsInstance::PostPhysicsUpdate(const Transform& transform)
{
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
        const glm::mat4 drivenGlobal = bodyModel * definition.bodyToBone;
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

void MmdPhysicsInstance::ResetToPose(const Transform& transform)
{
    const EntityFrame entity = ExtractEntityFrame(transform);
    for (RuntimeBody& runtime : this->rigidBodies)
    {
        const RigidTransform target = ModelToWorld(
            AnimatedBodyModelTransform(*runtime.definition, *this->pose),
            entity
        );
        this->world->SetTransform(
            runtime.handle,
            target.position,
            target.rotation,
            true
        );
        runtime.lastAnimatedPosition = target.position;
        runtime.lastAnimatedRotation = target.rotation;
        runtime.hasAnimatedTransform = true;
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
