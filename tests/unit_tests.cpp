#include "test_support.hpp"

#include <glm/gtc/constants.hpp>
#include "wisteria/mmd/mmd_determinism.hpp"
#include "wisteria/mmd/physics/mmd_physics_audit.hpp"
#include "wisteria/mmd/physics/mmd_physics_configuration.hpp"
#include "wisteria/runtime/checkpoint_serialization.hpp"
#include "trace_jsonl.hpp"

#include <btBulletDynamicsCommon.h>

#include <limits>

namespace
{
void TestDeterminismHashValidation()
{
    PoseSnapshot validPose;
    validPose.localTransforms = {glm::mat4(1.0f)};
    validPose.globalTransforms = {glm::mat4(1.0f)};
    validPose.skinningTransforms = {glm::mat4(1.0f)};
    Require(
        HashPose(validPose).valid,
        "Valid pose hash was marked invalid"
    );

    PoseSnapshot mismatchedPose = validPose;
    mismatchedPose.globalTransforms.clear();
    Require(
        !HashPose(mismatchedPose).valid,
        "Mismatched pose channel counts produced a valid hash"
    );

    PoseSnapshot nanPose = validPose;
    nanPose.localTransforms[0][1][1] =
        std::numeric_limits<float>::quiet_NaN();
    Require(
        !HashPose(nanPose).valid,
        "NaN pose matrix produced a valid hash"
    );

    DeformedVertexSnapshot validVertices;
    validVertices.positions = {glm::vec3(0.0f)};
    validVertices.normals = {glm::vec3(0.0f, 0.0f, 1.0f)};
    Require(
        HashVertices(validVertices).valid,
        "Valid vertex hash was marked invalid"
    );

    DeformedVertexSnapshot mismatchedVertices;
    mismatchedVertices.positions = {
        glm::vec3(0.0f),
        glm::vec3(1.0f)
    };
    mismatchedVertices.normals = {glm::vec3(0.0f, 0.0f, 1.0f)};
    Require(
        !HashVertices(mismatchedVertices).valid,
        "Mismatched vertex arrays produced a valid hash"
    );

    PhysicsSnapshot validPhysics;
    RigidBodySnapshot body;
    body.index = 0U;
    body.mode = PmxRigidBodyMode::Physics;
    body.definitionMass = 1.0f;
    body.worldTransform.rotationBasis = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    body.interpolationTransform.rotationBasis =
        body.worldTransform.rotationBasis;
    validPhysics.rigidBodies.push_back(body);
    Require(
        HashPhysics(validPhysics).valid,
        "Valid physics hash was marked invalid"
    );

    PhysicsSnapshot duplicateIndexPhysics = validPhysics;
    duplicateIndexPhysics.rigidBodies.push_back(body);
    Require(
        !HashPhysics(duplicateIndexPhysics).valid,
        "Duplicate rigid-body index produced a valid hash"
    );
}

void TestBulletLinkedBodyCollisionDisable()
{
    // R1.3 §8.1: DisableConstraintLinkedPairs must suppress contact only for
    // constraint-linked pairs and must never affect ground contacts. This is
    // the exact Bullet mechanism used by the Saba adapter (removeConstraint
    // + addConstraint(constraint, disable)).
    btDefaultCollisionConfiguration configuration;
    btCollisionDispatcher dispatcher(&configuration);
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world(
        &dispatcher,
        &broadphase,
        &solver,
        &configuration
    );
    world.setGravity(btVector3(0.0f, 0.0f, 0.0f));

    btBoxShape boxShape(btVector3(0.5f, 0.5f, 0.5f));
    btStaticPlaneShape groundShape(btVector3(0.0f, 1.0f, 0.0f), 0.0f);

    btTransform groundTransform;
    groundTransform.setIdentity();
    btDefaultMotionState groundMotion(groundTransform);
    btRigidBody::btRigidBodyConstructionInfo groundInfo(
        0.0f,
        &groundMotion,
        &groundShape,
        btVector3(0.0f, 0.0f, 0.0f)
    );
    btRigidBody ground(groundInfo);
    world.addRigidBody(&ground, 1, 1);

    btTransform startA;
    startA.setIdentity();
    startA.setOrigin(btVector3(0.0f, 0.25f, 0.0f));
    btDefaultMotionState motionA(startA);
    btRigidBody::btRigidBodyConstructionInfo infoA(
        1.0f,
        &motionA,
        &boxShape,
        btVector3(1.0f, 1.0f, 1.0f)
    );
    btRigidBody bodyA(infoA);
    btDefaultMotionState motionB(startA);
    btRigidBody::btRigidBodyConstructionInfo infoB(
        1.0f,
        &motionB,
        &boxShape,
        btVector3(1.0f, 1.0f, 1.0f)
    );
    btRigidBody bodyB(infoB);
    world.addRigidBody(&bodyA, 1, 1);
    world.addRigidBody(&bodyB, 1, 1);

    btTransform frame;
    frame.setIdentity();
    btPoint2PointConstraint constraint(
        bodyA,
        bodyB,
        btVector3(0.0f, 0.0f, 0.0f),
        btVector3(0.0f, 0.0f, 0.0f)
    );

    const auto pairContact = [&world](
        const btCollisionObject* first,
        const btCollisionObject* second)
    {
        btDispatcher* dispatch = world.getDispatcher();
        for (int index = 0; index < dispatch->getNumManifolds(); ++index)
        {
            const btPersistentManifold* manifold =
                dispatch->getManifoldByIndexInternal(index);
            if (manifold == nullptr || manifold->getNumContacts() == 0)
                continue;
            const btCollisionObject* body0 = manifold->getBody0();
            const btCollisionObject* body1 = manifold->getBody1();
            if ((body0 == first && body1 == second) ||
                (body0 == second && body1 == first))
            {
                return true;
            }
        }
        return false;
    };
    const auto clearManifolds = [&world]()
    {
        btDispatcher* dispatch = world.getDispatcher();
        for (int index = dispatch->getNumManifolds() - 1;
             index >= 0;
             --index)
        {
            dispatch->clearManifold(
                dispatch->getManifoldByIndexInternal(index)
            );
        }
    };

    // PmxMaskOnly equivalent: addConstraint without the disable flag.
    world.addConstraint(&constraint, false);
    world.stepSimulation(1.0f / 60.0f, 1, 1.0f / 60.0f);
    Require(
        pairContact(&bodyA, &bodyB),
        "linked overlapping bodies did not collide with disable=false"
    );
    Require(
        pairContact(&bodyA, &ground),
        "body-ground contact missing with disable=false"
    );

    // DisableConstraintLinkedPairs equivalent: re-add with the disable flag.
    world.removeConstraint(&constraint);
    world.addConstraint(&constraint, true);
    clearManifolds();
    world.stepSimulation(1.0f / 60.0f, 1, 1.0f / 60.0f);
    Require(
        !pairContact(&bodyA, &bodyB),
        "linked overlapping bodies still collide with disable=true"
    );
    Require(
        pairContact(&bodyA, &ground),
        "body-ground contact was affected by linked-body disable"
    );
}

void TestSkeletonAndPose()
{
    const glm::mat4 rootLocal = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    const glm::mat4 childLocal = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 2.0f, 0.0f)
    );
    const glm::mat4 leafLocal = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 3.0f)
    );
    const glm::mat4 childGlobal = rootLocal * childLocal;
    const glm::mat4 leafGlobal = childGlobal * leafLocal;

    // The child intentionally appears before its parent. Skeleton must derive
    // a safe parent-before-child evaluation order rather than trusting input.
    Skeleton skeleton({
        Bone{"child", 2U, childLocal, glm::inverse(childGlobal)},
        Bone{"leaf", 0U, leafLocal, glm::inverse(leafGlobal)},
        Bone{"root", InvalidBoneIndex, rootLocal, glm::inverse(rootLocal)}
    });

    Require(skeleton.BoneCount() == 3, "Skeleton bone count is incorrect");
    Require(skeleton.RootCount() == 1, "Skeleton root count is incorrect");
    Require(
        skeleton.FindBone("root") == std::optional<BoneIndex>(2U) &&
        !skeleton.FindBone("missing").has_value(),
        "Skeleton bone-name lookup is incorrect"
    );
    Require(
        skeleton.EvaluationOrder()[0] == 2U &&
        skeleton.EvaluationOrder()[1] == 0U &&
        skeleton.EvaluationOrder()[2] == 1U,
        "Skeleton evaluation order is not parent-before-child"
    );
    Require(
        skeleton.Children(2U).size() == 1U &&
        skeleton.Children(2U)[0] == 0U &&
        skeleton.Children(0U).size() == 1U &&
        skeleton.Children(0U)[0] == 1U &&
        skeleton.Children(1U).empty(),
        "Skeleton child lookup does not match its hierarchy"
    );
    Require(
        NearlyEqual(skeleton.BindGlobalMatrices()[1], leafGlobal),
        "Skeleton bind global matrix is incorrect"
    );

    Pose pose(skeleton);
    Require(pose.IsDirty(), "New Pose was unexpectedly clean");
    for (const glm::mat4& skinMatrix : pose.SkinningMatrices())
    {
        Require(
            NearlyEqual(skinMatrix, glm::mat4(1.0f)),
            "Bind pose did not produce an identity skinning matrix"
        );
    }
    Require(!pose.IsDirty(), "Pose remained dirty after evaluation");

    pose.SetLocalTransform(
        0U,
        BoneTransform{
            .translation = {0.0f, 4.0f, 0.0f}
        }
    );
    Require(pose.IsDirty(), "Pose edit did not invalidate cached matrices");
    Require(
        NearlyEqual(pose.GlobalMatrix(1U)[3].x, 1.0f) &&
        NearlyEqual(pose.GlobalMatrix(1U)[3].y, 4.0f) &&
        NearlyEqual(pose.GlobalMatrix(1U)[3].z, 3.0f),
        "Pose edit did not propagate through the bone hierarchy"
    );
    Require(
        NearlyEqual(
            pose.SkinningMatrices()[1],
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f))
        ),
        "Pose produced an incorrect final skinning matrix"
    );

    pose.ResetToBindPose();
    Require(
        NearlyEqual(pose.SkinningMatrices()[1], glm::mat4(1.0f)),
        "Pose reset did not restore the bind pose"
    );
}

void TestSkeletonValidation()
{
    bool duplicateRejected = false;
    try
    {
        Skeleton duplicate({Bone{"bone"}, Bone{"bone"}});
    }
    catch (const std::invalid_argument&)
    {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "Skeleton accepted duplicate bone names");

    bool badParentRejected = false;
    try
    {
        Skeleton badParent({Bone{"bone", 4U}});
    }
    catch (const std::invalid_argument&)
    {
        badParentRejected = true;
    }
    Require(badParentRejected, "Skeleton accepted an invalid parent index");

    bool cycleRejected = false;
    try
    {
        Skeleton cycle({Bone{"first", 1U}, Bone{"second", 0U}});
    }
    catch (const std::invalid_argument&)
    {
        cycleRejected = true;
    }
    Require(cycleRejected, "Skeleton accepted a hierarchy cycle");

    Skeleton valid({Bone{"root"}});
    Pose pose(valid);
    bool badPoseIndexRejected = false;
    try
    {
        pose.SetLocalMatrix(1U, glm::mat4(1.0f));
    }
    catch (const std::out_of_range&)
    {
        badPoseIndexRejected = true;
    }
    Require(badPoseIndexRejected, "Pose accepted an invalid bone index");
}

void TestAnimationSamplingAndAnimator()
{
    const glm::mat4 childBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    Skeleton skeleton({
        Bone{"root", InvalidBoneIndex, glm::mat4(1.0f), glm::mat4(1.0f)},
        Bone{"child", 0U, childBind, glm::inverse(childBind)}
    });

    AnimationTrack rootTrack(
        0U,
        {
            VectorKeyframe{0.0f, glm::vec3(0.0f)},
            VectorKeyframe{2.0f, glm::vec3(4.0f, 0.0f, 0.0f)}
        },
        {
            QuaternionKeyframe{0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
            QuaternionKeyframe{
                2.0f,
                glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f))
            }
        }
    );
    const BoneTransform middle = rootTrack.Sample(1.0f, BoneTransform{});
    Require(
        NearlyEqual(middle.translation.x, 2.0f),
        "AnimationTrack did not interpolate translation"
    );
    Require(
        NearlyEqual(glm::length(middle.rotation), 1.0f),
        "AnimationTrack did not normalize its quaternion sample"
    );

    ModelAsset model("animatedModel");
    model.SetBackendKind(ModelBackendKind::WisteriaGeneric);
    model.SetSkeleton(std::move(skeleton));
    AnimationClip& firstClip = model.AddAnimationClip(AnimationClip(
        "turnAndMove",
        2.0f,
        {std::move(rootTrack)}
    ));
    Require(
        model.AnimationClipCount() == 1 &&
        model.FindAnimationClip("turnAndMove") == &firstClip,
        "ModelAsset did not retain its animation clip"
    );

    PoseBuffer startPose(model.GetSkeleton());
    PoseBuffer endPose(model.GetSkeleton());
    PoseBuffer blendedPose(model.GetSkeleton());
    firstClip.Sample(0.0f, startPose);
    firstClip.Sample(2.0f, endPose);
    BlendPoseBuffers(startPose, endPose, 0.25f, blendedPose);
    Require(
        NearlyEqual(blendedPose.TransformAt(0U).translation.x, 1.0f),
        "PoseBuffer did not blend sampled translations"
    );
    Require(
        NearlyEqual(blendedPose.LocalMatrices()[1], childBind),
        "AnimationClip::Sample changed an unanimated bind-pose bone"
    );
    Pose sampledPose(model.GetSkeleton());
    blendedPose.ApplyTo(sampledPose);
    Require(
        NearlyEqual(sampledPose.LocalMatrix(0U)[3].x, 1.0f),
        "PoseBuffer did not apply its local pose to Pose"
    );

    Scene scene;
    Entity& entity = scene.InstantiateModel(model);
    Require(entity.HasAnimator(), "Animated model instance has no Animator");
    Require(
        entity.GetAnimator().CurrentClip() == &firstClip &&
        entity.GetAnimator().IsPlaying(),
        "Scene did not start the model's first animation clip"
    );

    scene.Update(0.5f);
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(0U)[3].x, 1.0f),
        "Animator time did not advance in seconds"
    );
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(1U), childBind),
        "Animator changed a bone without an animation track"
    );

    entity.GetAnimator().SetSpeed(2.0f);
    scene.Update(0.25f);
    Require(
        NearlyEqual(entity.GetAnimator().Time(), 1.0f) &&
        NearlyEqual(entity.GetPose().LocalMatrix(0U)[3].x, 2.0f),
        "Animator playback speed was not applied"
    );

    entity.GetAnimator().Pause();
    scene.Update(0.5f);
    Require(
        NearlyEqual(entity.GetAnimator().Time(), 1.0f),
        "Paused Animator continued advancing"
    );
    entity.GetAnimator().Resume();
    entity.GetAnimator().SetLooping(false);
    entity.GetAnimator().SetTime(1.75f);
    scene.Update(1.0f);
    Require(
        NearlyEqual(entity.GetAnimator().Time(), 2.0f) &&
        !entity.GetAnimator().IsPlaying() &&
        NearlyEqual(entity.GetPose().LocalMatrix(0U)[3].x, 4.0f),
        "Non-looping Animator did not stop on its final frame"
    );

    const AnimationClip* stableAddress = entity.GetAnimator().CurrentClip();
    AnimationClip& secondClip = model.AddAnimationClip(AnimationClip(
        "secondClip",
        1.0f,
        {AnimationTrack(
            0U,
            {
                VectorKeyframe{0.0f, glm::vec3(0.0f)},
                VectorKeyframe{1.0f, glm::vec3(1.0f)}
            }
        )}
    ));
    Require(
        entity.GetAnimator().CurrentClip() == stableAddress,
        "Adding a ModelAsset clip invalidated a running Animator"
    );

    Animator& animator = entity.GetAnimator();
    animator.SetSpeed(1.0f);
    animator.SetLooping(false);
    animator.Play(firstClip);
    animator.SetTime(1.0f);
    animator.CrossFade(secondClip, 2.0f);
    Require(
        animator.IsTransitioning() &&
        animator.CurrentClip() == &secondClip &&
        NearlyEqual(entity.GetPose().LocalMatrix(0U)[3].x, 2.0f),
        "CrossFade did not begin from the currently displayed pose"
    );

    animator.Update(0.5f);
    Require(
        NearlyEqual(animator.TransitionProgress(), 0.25f) &&
        NearlyEqual(entity.GetPose().LocalMatrix(0U)[3].x, 2.375f),
        "CrossFade did not blend independently advancing clips"
    );
    animator.Pause();
    animator.Update(0.5f);
    Require(
        NearlyEqual(animator.TransitionProgress(), 0.25f),
        "Paused CrossFade continued advancing"
    );
    animator.Resume();
    animator.Update(1.5f);
    Require(
        !animator.IsTransitioning() &&
        animator.CurrentClip() == &secondClip &&
        NearlyEqual(entity.GetPose().LocalMatrix(0U)[3].x, 1.0f),
        "CrossFade did not finish on the destination clip"
    );

    animator.Play(firstClip);
    animator.SetTime(1.0f);
    animator.CrossFade(secondClip, 2.0f);
    animator.Update(0.5f);
    const glm::mat4 poseBeforeInterruption =
        entity.GetPose().LocalMatrix(0U);
    animator.CrossFade(firstClip, 1.0f);
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(0U), poseBeforeInterruption),
        "Interrupting a CrossFade caused a pose discontinuity"
    );
    animator.Update(0.5f);
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(0U)[3].x, 1.6875f),
        "Interrupted CrossFade did not blend from its captured pose"
    );

    animator.CrossFade(secondClip, 0.0f);
    Require(
        !animator.IsTransitioning() &&
        animator.CurrentClip() == &secondClip &&
        NearlyEqual(animator.Time(), 0.0f),
        "Zero-duration CrossFade did not switch immediately"
    );

    bool invalidFadeRejected = false;
    try
    {
        animator.CrossFade(firstClip, -1.0f);
    }
    catch (const std::invalid_argument&)
    {
        invalidFadeRejected = true;
    }
    Require(
        invalidFadeRejected,
        "Animator accepted a negative CrossFade duration"
    );

    animator.SetFloat("motionSpeed", 0.0f);
    animator.SetBool("grounded", true);
    AnimationStateMachine& stateMachine = animator.GetStateMachine();
    stateMachine.AddState(AnimationState{
        "Idle",
        &firstClip,
        1.0f,
        true
    });
    stateMachine.AddState(AnimationState{
        "Move",
        &secondClip,
        1.5f,
        false
    });
    stateMachine.AddTransition(AnimationTransitionRule{
        "Idle",
        "Move",
        0.4f,
        [](const Animator& value)
        {
            return value.GetBool("grounded") &&
                value.GetFloat("motionSpeed") > 0.5f;
        }
    });
    stateMachine.AddTransition(AnimationTransitionRule{
        std::string(AnimationStateMachine::AnyState),
        "Idle",
        0.1f,
        [](const Animator& value)
        {
            return value.IsTriggerSet("reset");
        }
    });
    stateMachine.SetState("Idle");
    animator.Update(0.0f);
    Require(
        stateMachine.CurrentState() != nullptr &&
        stateMachine.CurrentState()->name == "Idle" &&
        animator.CurrentClip() == &firstClip,
        "AnimationStateMachine did not apply its initial state"
    );

    animator.SetFloat("motionSpeed", 1.0f);
    animator.Update(0.0f);
    Require(
        stateMachine.CurrentState()->name == "Move" &&
        animator.CurrentClip() == &secondClip &&
        animator.IsTransitioning() &&
        NearlyEqual(animator.Speed(), 1.5f) &&
        !animator.IsLooping(),
        "AnimationStateMachine did not enter its conditional destination"
    );

    animator.SetTrigger("reset");
    animator.Update(0.0f);
    Require(
        stateMachine.CurrentState()->name == "Idle" &&
        animator.CurrentClip() == &firstClip &&
        !animator.IsTriggerSet("reset"),
        "Any-state trigger transition or trigger consumption failed"
    );

    bool parameterTypeRejected = false;
    try
    {
        animator.SetBool("motionSpeed", true);
    }
    catch (const std::invalid_argument&)
    {
        parameterTypeRejected = true;
    }
    Require(
        parameterTypeRejected,
        "Animator accepted one parameter name with two types"
    );

    animator.Stop();
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(0U), glm::mat4(1.0f)),
        "Stopping Animator did not restore the bind pose"
    );
}

void TestRootMotion()
{
    Skeleton skeleton({Bone{"root"}});
    AnimationClip translationClip(
        "translation",
        1.0f,
        {AnimationTrack(
            0U,
            {
                VectorKeyframe{0.0f, glm::vec3(0.0f)},
                VectorKeyframe{1.0f, glm::vec3(2.0f, 0.0f, 0.0f)}
            }
        )}
    );

    Entity entity;
    entity.SetSkeleton(skeleton);
    Animator& animator = entity.GetAnimator();
    animator.Play(translationClip);
    animator.SetRootMotionBone(0U);
    animator.SetRootMotionEnabled(true);
    Require(
        animator.IsRootMotionEnabled() &&
        animator.RootMotionBone() == std::optional<BoneIndex>(0U),
        "Animator did not retain its root motion configuration"
    );

    entity.Update(0.5f);
    Require(
        NearlyEqual(entity.GetTransform().Position().x, 1.0f),
        "Entity did not apply Animator root translation"
    );
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(0U), glm::mat4(1.0f)),
        "Extracted root motion remained in the skeletal Pose"
    );

    // Crossing the loop boundary must continue forward instead of subtracting
    // the end position when playback wraps to the beginning.
    entity.Update(0.75f);
    Require(
        NearlyEqual(entity.GetTransform().Position().x, 2.5f) &&
        NearlyEqual(animator.Time(), 0.25f),
        "Looping root motion moved backward at the clip boundary"
    );
    animator.Pause();
    entity.Update(1.0f);
    Require(
        NearlyEqual(entity.GetTransform().Position().x, 2.5f),
        "Paused Animator produced root motion"
    );

    Entity singleStepEntity;
    singleStepEntity.SetSkeleton(skeleton);
    Animator& singleStepAnimator = singleStepEntity.GetAnimator();
    singleStepAnimator.Play(translationClip);
    singleStepAnimator.SetRootMotionBone(0U);
    singleStepAnimator.SetRootMotionEnabled(true);
    singleStepEntity.Update(1.25f);
    Require(
        NearlyEqual(singleStepEntity.GetTransform().Position().x, 2.5f),
        "Root motion changed with update subdivision"
    );

    AnimationClip fasterTranslationClip(
        "fasterTranslation",
        1.0f,
        {AnimationTrack(
            0U,
            {
                VectorKeyframe{0.0f, glm::vec3(0.0f)},
                VectorKeyframe{1.0f, glm::vec3(4.0f, 0.0f, 0.0f)}
            }
        )}
    );
    Entity fadingEntity;
    fadingEntity.SetSkeleton(skeleton);
    Animator& fadingAnimator = fadingEntity.GetAnimator();
    fadingAnimator.Play(translationClip);
    fadingAnimator.SetRootMotionBone(0U);
    fadingAnimator.SetRootMotionEnabled(true);
    fadingAnimator.CrossFade(fasterTranslationClip, 1.0f);
    fadingEntity.Update(1.0f);
    Require(
        NearlyEqual(fadingEntity.GetTransform().Position().x, 3.0f) &&
        !fadingAnimator.IsTransitioning() &&
        NearlyEqual(fadingEntity.GetPose().LocalMatrix(0U), glm::mat4(1.0f)),
        "CrossFade did not blend and remove source/destination root motion"
    );

    Pose standalonePose(skeleton);
    Animator standaloneAnimator(standalonePose);
    standaloneAnimator.Play(translationClip);
    standaloneAnimator.SetRootMotionBone(0U);
    standaloneAnimator.SetRootMotionEnabled(true);
    standaloneAnimator.Update(0.25f);
    const RootMotionDelta consumed = standaloneAnimator.ConsumeRootMotion();
    Require(
        NearlyEqual(consumed.translation.x, 0.5f) &&
        standaloneAnimator.ConsumeRootMotion().IsIdentity(),
        "ConsumeRootMotion did not consume exactly one pending delta"
    );

    Entity disabledEntity;
    disabledEntity.SetSkeleton(skeleton);
    disabledEntity.GetAnimator().Play(translationClip);
    disabledEntity.Update(0.5f);
    Require(
        NearlyEqual(disabledEntity.GetTransform().Position().x, 0.0f) &&
        NearlyEqual(disabledEntity.GetPose().LocalMatrix(0U)[3].x, 1.0f),
        "Disabled root motion changed normal skeletal animation"
    );

    AnimationClip rotationClip(
        "rotation",
        1.0f,
        {AnimationTrack(
            0U,
            {},
            {
                QuaternionKeyframe{
                    0.0f,
                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
                },
                QuaternionKeyframe{
                    1.0f,
                    glm::angleAxis(
                        glm::radians(90.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)
                    )
                }
            }
        )}
    );
    Entity rotatingEntity;
    rotatingEntity.SetSkeleton(skeleton);
    Animator& rotatingAnimator = rotatingEntity.GetAnimator();
    rotatingAnimator.Play(rotationClip);
    rotatingAnimator.SetLooping(false);
    rotatingAnimator.SetRootMotionBone(0U);
    rotatingAnimator.SetRootMotionEnabled(true);
    rotatingEntity.Update(1.0f);
    Require(
        NearlyEqual(rotatingEntity.GetTransform().Rotation().y, 90.0f) &&
        NearlyEqual(rotatingEntity.GetPose().LocalMatrix(0U), glm::mat4(1.0f)),
        "Root motion rotation was not transferred to Entity Transform"
    );

    Transform scaledTransform(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 90.0f, 0.0f),
        glm::vec3(2.0f)
    );
    scaledTransform.ApplyLocalMotion(RootMotionDelta{
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
    });
    Require(
        NearlyEqual(scaledTransform.Position().x, 0.0f) &&
        NearlyEqual(scaledTransform.Position().z, -2.0f),
        "Transform did not apply root translation in scaled local space"
    );

    bool missingBoneRejected = false;
    try
    {
        Pose invalidPose(skeleton);
        Animator invalidAnimator(invalidPose);
        invalidAnimator.SetRootMotionEnabled(true);
    }
    catch (const std::logic_error&)
    {
        missingBoneRejected = true;
    }
    Require(
        missingBoneRejected,
        "Animator enabled root motion without a configured bone"
    );
}

void TestMmdBoneConstraints()
{
    Bone appendSource{"appendSource"};
    appendSource.sourceOrder = 0U;
    Bone appendedBone{"appendedBone"};
    appendedBone.deformLayer = 1;
    appendedBone.sourceOrder = 1U;
    appendedBone.appendTransform = MmdAppendTransform{
        0U,
        0.5f,
        true,
        true
    };
    Skeleton appendSkeleton({appendSource, appendedBone});
    Require(
        appendSkeleton.HasMmdConstraints() &&
        appendSkeleton.MmdConstraintOrder().size() == 1U &&
        appendSkeleton.MmdConstraintOrder()[0] == 1U,
        "Skeleton did not retain its MMD append evaluation order"
    );

    AnimationClip appendClip(
        "append",
        1.0f,
        {AnimationTrack(
            0U,
            {
                VectorKeyframe{0.0f, glm::vec3(0.0f)},
                VectorKeyframe{1.0f, glm::vec3(2.0f, 0.0f, 0.0f)}
            },
            {
                QuaternionKeyframe{
                    0.0f,
                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
                },
                QuaternionKeyframe{
                    1.0f,
                    glm::angleAxis(
                        glm::radians(90.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)
                    )
                }
            }
        )}
    );
    Pose appendPose(appendSkeleton);
    Animator appendAnimator(appendPose);
    appendAnimator.Play(appendClip);
    appendAnimator.SetLooping(false);
    appendAnimator.SetTime(1.0f);
    const BoneTransform appended = BoneTransform::FromMatrix(
        appendPose.LocalMatrix(1U)
    );
    const glm::vec3 rotatedAxis =
        appended.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    Require(
        NearlyEqual(appended.translation.x, 1.0f) &&
        NearlyEqual(rotatedAxis.x, std::sqrt(0.5f)) &&
        NearlyEqual(rotatedAxis.y, std::sqrt(0.5f)),
        "MMD append translation or rotation weight was not applied"
    );

    Bone ikLink{"ikLink"};
    ikLink.sourceOrder = 0U;
    Bone ikEffector{"ikEffector"};
    ikEffector.parentIndex = 0U;
    ikEffector.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    ikEffector.inverseBindMatrix = glm::inverse(
        ikEffector.bindLocalMatrix
    );
    ikEffector.sourceOrder = 1U;
    Bone ikController{"ikController"};
    ikController.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    ikController.inverseBindMatrix = glm::inverse(
        ikController.bindLocalMatrix
    );
    ikController.deformLayer = 1;
    ikController.sourceOrder = 2U;
    ikController.ikConstraint = MmdIkConstraint{
        1U,
        8U,
        glm::radians(45.0f),
        {MmdIkLink{0U}}
    };
    Skeleton ikSkeleton({ikLink, ikEffector, ikController});
    AnimationClip ikClip(
        "ik",
        1.0f,
        {AnimationTrack(
            2U,
            {VectorKeyframe{0.0f, glm::vec3(0.0f, 1.0f, 0.0f)}}
        )}
    );
    Pose ikPose(ikSkeleton);
    Animator ikAnimator(ikPose);
    ikAnimator.Play(ikClip);
    glm::vec3 effectorPosition(ikPose.GlobalMatrix(1U)[3]);
    Require(
        glm::length(effectorPosition - glm::vec3(0.0f, 1.0f, 0.0f)) <
            0.001f,
        "MMD CCD IK did not move its effector to the controller"
    );

    ikAnimator.SetMmdIkEnabled(2U, false);
    effectorPosition = glm::vec3(ikPose.GlobalMatrix(1U)[3]);
    Require(
        !ikAnimator.IsMmdIkEnabled(2U) &&
        glm::length(effectorPosition - glm::vec3(1.0f, 0.0f, 0.0f)) <
            0.001f,
        "Disabling one MMD IK controller did not restore the sampled pose"
    );
    ikAnimator.SetMmdIkEnabled(2U, true);
    effectorPosition = glm::vec3(ikPose.GlobalMatrix(1U)[3]);
    Require(
        ikAnimator.IsMmdIkEnabled(2U) &&
        glm::length(effectorPosition - glm::vec3(0.0f, 1.0f, 0.0f)) <
            0.001f,
        "Re-enabling one MMD IK controller did not re-evaluate the pose"
    );
}

void TestMorphRuntime()
{
    MorphSet morphSet({
        MorphDefinition{"smile", MorphCategory::Mouth},
        MorphDefinition{"blink", MorphCategory::Eye}
    });
    Require(
        morphSet.MorphCount() == 2U &&
        morphSet.FindMorph("blink") == std::optional<MorphIndex>(1U),
        "MorphSet name/index mapping is incorrect"
    );

    MorphState firstState(morphSet);
    MorphState secondState(morphSet);
    firstState.SetWeight("smile", 0.75f);
    Require(
        NearlyEqual(firstState.Weight(0U), 0.75f) &&
        NearlyEqual(secondState.Weight(0U), 0.0f) &&
        firstState.Revision() == 1U,
        "MorphState weights are not instance-local"
    );

    DefaultModelData data{
        {
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f
        },
        {0U, 1U, 2U},
        {{"position", 3, FLOAT}}
    };
    Mesh mesh(
        std::move(data),
        0U,
        {
            MeshMorphTarget{
                0U,
                {
                    VertexMorphOffset{1U, glm::vec3(2.0f, 0.0f, 0.0f)},
                    VertexMorphOffset{2U, glm::vec3(0.0f, 2.0f, 0.0f)}
                }
            }
        }
    );
    std::vector<glm::vec3> offsets;
    Require(
        mesh.HasMorphTargets() && mesh.VertexCount() == 3U &&
        mesh.CalculateMorphOffsets(firstState.Weights(), offsets) &&
        offsets.size() == 3U &&
        NearlyEqual(offsets[1].x, 1.5f) &&
        NearlyEqual(offsets[2].y, 1.5f),
        "Mesh did not combine sparse morph offsets with instance weights"
    );

    ModelAsset model("morphModel");
    model.SetMorphs({
        MorphDefinition{"smile", MorphCategory::Mouth},
        MorphDefinition{"blink", MorphCategory::Eye}
    });
    Require(
        model.HasMorphs() &&
        model.GetMorphSet().FindMorph("smile").has_value(),
        "ModelAsset did not retain its MorphSet"
    );
    Entity firstEntity;
    Entity secondEntity;
    firstEntity.SetMorphSet(model.GetMorphSet());
    secondEntity.SetMorphSet(model.GetMorphSet());
    firstEntity.GetMorphState().SetWeight("blink", 1.0f);
    Require(
        NearlyEqual(firstEntity.GetMorphState().Weight("blink"), 1.0f) &&
        NearlyEqual(secondEntity.GetMorphState().Weight("blink"), 0.0f),
        "Entities unexpectedly share mutable MorphState weights"
    );

    MorphSet groupedMorphSet({
        MorphDefinition{"smile", MorphCategory::Mouth},
        MorphDefinition{"blink", MorphCategory::Eye},
        MorphDefinition{
            "expression",
            MorphCategory::Mouth,
            MorphKind::Group,
            {
                GroupMorphMember{0U, 0.5f},
                GroupMorphMember{1U, 1.0f}
            }
        },
        MorphDefinition{
            "nested",
            MorphCategory::Other,
            MorphKind::Group,
            {
                GroupMorphMember{2U, 0.5f},
                GroupMorphMember{0U, 0.25f}
            }
        }
    });
    MorphState groupedState(groupedMorphSet);
    groupedState.SetWeight("smile", 0.1f);
    groupedState.SetWeight("expression", 0.8f);
    std::span<const float> effective = groupedState.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.5f) &&
        NearlyEqual(effective[1U], 0.8f) &&
        NearlyEqual(effective[2U], 0.0f) &&
        NearlyEqual(effective[3U], 0.0f),
        "Group Morph did not accumulate direct and grouped weights"
    );
    groupedState.Reset();
    groupedState.SetWeight("nested", 1.0f);
    effective = groupedState.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.5f) &&
        NearlyEqual(effective[1U], 0.5f),
        "Nested Group Morph did not recursively expand its weights"
    );

    bool cycleRejected = false;
    try
    {
        MorphSet cyclic({
            MorphDefinition{
                "first",
                MorphCategory::Other,
                MorphKind::Group,
                {GroupMorphMember{1U, 1.0f}}
            },
            MorphDefinition{
                "second",
                MorphCategory::Other,
                MorphKind::Group,
                {GroupMorphMember{0U, 1.0f}}
            }
        });
        (void)cyclic;
    }
    catch (const std::invalid_argument&)
    {
        cycleRejected = true;
    }
    Require(cycleRejected, "MorphSet accepted a cyclic Group Morph graph");
}

void TestBoneAndUvMorphEvaluation()
{
    Bone root;
    root.name = "root";
    root.bindLocalMatrix = glm::mat4(1.0f);
    root.inverseBindMatrix = glm::mat4(1.0f);
    Skeleton skeleton({root});

    const MorphIndex boneMorph = 0U;
    const MorphIndex uvMorph = 1U;
    MorphSet morphSet({
        MorphDefinition{
            "boneShift",
            MorphCategory::Other,
            MorphKind::Bone,
            {},
            {},
            {
                BoneMorphOffset{
                    0U,
                    glm::vec3(1.0f, 2.0f, 3.0f),
                    glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f))
                }
            }
        },
        MorphDefinition{
            "uvShift",
            MorphCategory::Other,
            MorphKind::Uv
        }
    });

    // Bone morph: translation accumulates offset * weight; rotation slerps
    // from identity toward the offset quaternion and post-multiplies.
    MorphState boneState(morphSet);
    PoseBuffer poseBuffer(skeleton);
    poseBuffer.ResetToBindPose();
    boneState.SetWeight(boneMorph, 0.5f);
    morphSet.ApplyBoneMorphs(boneState.Weights(), poseBuffer);
    const BoneTransform transformed = poseBuffer.TransformAt(0U);
    Require(
        NearlyEqual(transformed.translation, glm::vec3(0.5f, 1.0f, 1.5f)),
        "Bone morph translation did not accumulate offset * weight"
    );
    const glm::quat expectedHalfRotation = glm::angleAxis(
        glm::quarter_pi<float>(),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    Require(
        NearlySameRotation(transformed.rotation, expectedHalfRotation),
        "Bone morph rotation did not slerp with the morph weight"
    );

    // UV morph: per-channel vec4 offsets accumulate into channels 1..4.
    DefaultModelData data{
        {
            0.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 1.0f
        },
        {0U},
        {{"position", 3, FLOAT}, {"color", 3, FLOAT}}
    };
    Mesh uvMesh(
        std::move(data),
        0U,
        {
            MeshMorphTarget{
                uvMorph,
                {},
                {
                    UvMorphOffset{
                        0U,
                        1U,
                        glm::vec4(0.1f, 0.2f, 0.3f, 0.4f)
                    },
                    UvMorphOffset{
                        0U,
                        4U,
                        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)
                    }
                }
            }
        }
    );
    MorphState uvState(morphSet);
    uvState.SetWeight(uvMorph, 0.5f);
    std::vector<MorphVertexDelta> deltas;
    Require(
        uvMesh.CalculateMorphDeltas(uvState.EffectiveWeights(), deltas) &&
        deltas.size() == 1U,
        "UV morph mesh did not produce per-vertex deltas"
    );
    Require(
        NearlyEqual(
            deltas[0U].uv[1U],
            glm::vec4(0.05f, 0.10f, 0.15f, 0.20f)
        ) &&
        NearlyEqual(deltas[0U].uv[4U].x, 0.5f) &&
        NearlyEqual(deltas[0U].uv[0U], glm::vec4(0.0f)),
        "UV morph deltas did not accumulate into the correct channels"
    );

    // Flip morph: PMX 2.1 flip slots overwrite the target weight.
    MorphSet flipSet({
        MorphDefinition{
            "target",
            MorphCategory::Other,
            MorphKind::Vertex
        },
        MorphDefinition{
            "flip",
            MorphCategory::Other,
            MorphKind::Flip,
            {},
            {
                FlipMorphMember{0U, 0.25f},
                FlipMorphMember{0U, 0.75f}
            }
        }
    });
    MorphState flipState(flipSet);
    flipState.SetWeight(1U, 0.5f);
    Require(
        NearlyEqual(flipState.EffectiveWeights()[0U], 0.25f),
        "Flip morph did not select the low-weight slot"
    );
    flipState.SetWeight(1U, 1.0f);
    Require(
        NearlyEqual(flipState.EffectiveWeights()[0U], 0.75f),
        "Flip morph did not select the last slot at full control"
    );
    flipState.SetWeight(1U, 0.0f);
    Require(
        NearlyEqual(flipState.EffectiveWeights()[0U], 0.0f),
        "Zero flip control must not overwrite the target"
    );
}

class InterfaceCompilationRuntime final : public MmdRuntimeModel
{
public:
    explicit InterfaceCompilationRuntime(Pose& poseReference)
        : pose(poseReference)
    {
    }

    bool Initialize() override
    {
        return true;
    }

    void Update(float) override
    {
    }

    void Reset() override
    {
    }

    Pose* TryGetPose() noexcept override
    {
        return &this->pose;
    }

    const Pose* TryGetPose() const noexcept override
    {
        return &this->pose;
    }

    bool NeedsDynamicVertexUpload() const noexcept override
    {
        return false;
    }

    ModelVertexFrame VertexFrame() const noexcept override
    {
        return {};
    }

    PhysicsInstance* TryGetPhysicsInstance() noexcept override
    {
        return nullptr;
    }

    const PhysicsInstance* TryGetPhysicsInstance() const noexcept override
    {
        return nullptr;
    }

    std::string_view BackendName() const noexcept override
    {
        return "test";
    }

    void SetMmdIkEnabled(BoneIndex, bool) override
    {
    }

    BoneIndex FindBoneIndex(const std::string&) const override
    {
        return InvalidBoneIndex;
    }

    bool LoadMotion(const std::filesystem::path&) override
    {
        return false;
    }

    void ClearMotion() override
    {
    }

    bool HasMotion() const noexcept override
    {
        return false;
    }

    void SetMotionLooping(bool) override
    {
    }

    bool IsMotionLooping() const noexcept override
    {
        return true;
    }

    void PauseMotion() override
    {
    }

    void ResumeMotion() override
    {
    }

    bool IsMotionPaused() const noexcept override
    {
        return false;
    }

    void RestartMotion(bool) override
    {
    }

    double MotionFrame() const noexcept override
    {
        return 0.0;
    }

    void SetMotionFrame(double) override
    {
    }

    double MotionMaxFrame() const noexcept override
    {
        return 0.0;
    }

    bool LoadCameraMotion(const std::filesystem::path&) override
    {
        return false;
    }

    std::optional<CameraTrackSample>
        SampleCameraMotion(float) const override
    {
        return std::nullopt;
    }

    std::size_t MorphCount() const noexcept override
    {
        return 0U;
    }

    bool DescribeMorph(
        std::size_t,
        MorphDescriptor&
    ) const override
    {
        return false;
    }

    bool ReadMorphState(
        std::size_t,
        MorphRuntimeState&
    ) const override
    {
        return false;
    }

    std::uint64_t MorphRevision() const noexcept override
    {
        return 0U;
    }

    bool LoadLightMotion(const std::filesystem::path&) override
    {
        return false;
    }

    std::optional<LightTrackSample>
        SampleLightMotion(float) const override
    {
        return std::nullopt;
    }

    void SetMmdPhysicsSettings(
        const MmdPhysicsRuntimeSettings&
    ) override
    {
    }

    void ResetMmdPhysics() override
    {
    }

    MmdSkinningKind SkinningKind() const noexcept override
    {
        return MmdSkinningKind::LinearBlend;
    }

    PhysicsInstance* GetMmdPhysics() noexcept override
    {
        return nullptr;
    }

private:
    Pose& pose;
};

class InterfaceSelfSteppingInstance final : public PhysicsInstance
{
public:
    bool OwnsSimulationStep() const noexcept override
    {
        return true;
    }

    void PrepareSimulation(float) override
    {
    }

    void FinishSimulation() override
    {
    }

    void ResetSimulation() override
    {
    }
};

void TestInterfaceCompilation()
{
    // Importer interface is extensible and the Saba stub is instantiable.
    SabaMmdImporter sabaImporter;
    ModelImporter baseImporter;
    Require(
        sizeof(sabaImporter) > 0U,
        "SabaMmdImporter must be instantiable"
    );
    (void)baseImporter;

    // RuntimeModelBase / MmdRuntimeModel can be implemented by a stub.
    std::vector<Bone> bones;
    Bone root;
    root.name = "root";
    bones.push_back(root);
    Skeleton skeleton(std::move(bones));
    Pose pose(skeleton);
    InterfaceCompilationRuntime runtime(pose);
    Require(runtime.Initialize(), "Runtime stub Initialize failed");
    Require(
        !runtime.NeedsDynamicVertexUpload(),
        "Runtime stub must not request dynamic uploads"
    );
    Require(
        runtime.SkinningKind() == MmdSkinningKind::LinearBlend,
        "Runtime stub skinning kind mismatch"
    );
    Require(
        runtime.TryGetPhysicsInstance() == nullptr,
        "Runtime stub must not expose a physics instance"
    );
    runtime.Update(0.0f);
    runtime.Reset();
    Require(
        &runtime.GetPose() == &pose,
        "Runtime stub pose identity mismatch"
    );

    // Mesh dynamic vertex bridge exists (implementation lands in phase 2).
    Mesh mesh(DefaultModelData{});
    Require(
        !mesh.HasDynamicVertexSource(),
        "Mesh must start without a dynamic vertex source"
    );
    bool emptyUploadRejected = false;
    try
    {
        mesh.UploadDynamicVertices({}, {});
    }
    catch (const std::invalid_argument&)
    {
        emptyUploadRejected = true;
    }
    Require(
        emptyUploadRejected,
        "Empty mesh must reject dynamic vertex uploads"
    );

    // CameraTrack interface exists; empty tracks reject sampling.
    CameraTrack track({});
    CameraKeyframe cameraSample;
    Require(
        !track.Sample(0.0f, cameraSample),
        "Empty CameraTrack must reject sampling"
    );
    Require(
        track.EndTime() == 0.0f,
        "Empty CameraTrack end time must be zero"
    );
    LightTrack lightTrack({});
    LightKeyframe lightSample;
    Require(
        !lightTrack.Sample(0.0f, lightSample),
        "Empty LightTrack must reject sampling"
    );

    // PhysicsInstance self-stepping hook defaults to false and can be
    // overridden by Saba's per-model world.
    InterfaceSelfSteppingInstance selfStepping;
    Require(
        selfStepping.OwnsSimulationStep(),
        "Self-stepping override must return true"
    );
}

void TestFxaaSettings()
{
    Renderer renderer;
    renderer.SetFxaaSettings(FxaaSettings{
        .enabled = false,
        .minimumContrast = 0.05f,
        .relativeContrast = 0.2f,
        .subpixelBlending = 0.6f
    });
    const FxaaSettings& settings = renderer.GetFxaaSettings();
    Require(!settings.enabled, "FXAA enabled flag changed");
    Require(
        NearlyEqual(settings.minimumContrast, 0.05f) &&
        NearlyEqual(settings.relativeContrast, 0.2f) &&
        NearlyEqual(settings.subpixelBlending, 0.6f),
        "FXAA settings changed"
    );

    bool rejected = false;
    try
    {
        renderer.SetFxaaSettings(FxaaSettings{
            .subpixelBlending = 1.1f
        });
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Invalid FXAA settings were accepted");
}

void StepPhysics(PhysicsWorld& world, int frameCount)
{
    for (int frame = 0; frame < frameCount; ++frame)
        world.Step(1.0f / 60.0f);
}

PhysicsBodyDesc StaticGroundDesc()
{
    PhysicsBodyDesc ground;
    ground.shape = PhysicsShapeDesc::Box(glm::vec3(10.0f, 0.5f, 10.0f));
    ground.motionType = PhysicsMotionType::Static;
    ground.position = glm::vec3(0.0f, -0.5f, 0.0f);
    ground.friction = 0.8f;
    return ground;
}

PhysicsBodyDesc DynamicBodyDesc(
    const PhysicsShapeDesc& shape,
    const glm::vec3& position
)
{
    PhysicsBodyDesc body;
    body.shape = shape;
    body.motionType = PhysicsMotionType::Dynamic;
    body.position = position;
    body.mass = 1.0f;
    body.linearDamping = 0.01f;
    body.angularDamping = 0.01f;
    body.friction = 0.5f;
    return body;
}

void TestBulletFoundationValidation()
{
    Require(
        PhysicsShapeDesc::Sphere(0.5f).kind == PhysicsShapeKind::Sphere,
        "Physics sphere factory returned the wrong kind"
    );
    Require(
        NearlyEqual(
            PhysicsShapeDesc::Box(glm::vec3(1.0f, 2.0f, 3.0f)).dimensions,
            glm::vec3(1.0f, 2.0f, 3.0f)
        ),
        "Physics box factory changed the half extents"
    );
    Require(
        NearlyEqual(
            PhysicsShapeDesc::Capsule(0.25f, 1.5f).dimensions,
            glm::vec3(0.25f, 1.5f, 0.0f)
        ),
        "Physics capsule factory changed its dimensions"
    );

    PhysicsWorld world;
    Require(
        NearlyEqual(world.Gravity(), glm::vec3(0.0f, -9.8f, 0.0f)),
        "PhysicsWorld default gravity is incorrect"
    );
    world.SetGravity(glm::vec3(0.0f, -4.0f, 0.0f));
    Require(
        NearlyEqual(world.Gravity(), glm::vec3(0.0f, -4.0f, 0.0f)),
        "PhysicsWorld did not retain its gravity"
    );

    bool badMassRejected = false;
    try
    {
        PhysicsBodyDesc body = DynamicBodyDesc(
            PhysicsShapeDesc::Sphere(0.5f),
            glm::vec3(0.0f)
        );
        body.mass = 0.0f;
        world.CreateBody(body);
    }
    catch (const std::invalid_argument&)
    {
        badMassRejected = true;
    }
    Require(badMassRejected, "PhysicsWorld accepted a zero-mass dynamic body");

    bool badShapeRejected = false;
    try
    {
        PhysicsBodyDesc body = StaticGroundDesc();
        body.shape = PhysicsShapeDesc::Box(glm::vec3(1.0f, 0.0f, 1.0f));
        world.CreateBody(body);
    }
    catch (const std::invalid_argument&)
    {
        badShapeRejected = true;
    }
    Require(badShapeRejected, "PhysicsWorld accepted an invalid box shape");

    bool badStepRejected = false;
    try
    {
        world.SetStepSettings(PhysicsStepSettings{4, 0.0f, 0.1f});
    }
    catch (const std::invalid_argument&)
    {
        badStepRejected = true;
    }
    Require(badStepRejected, "PhysicsWorld accepted a zero fixed time step");
}

void TestBulletP1CcdMarginsAndSolver()
{
    PhysicsStepSettings settings;
    settings.solverIterations = 21;
    settings.splitImpulse = true;
    settings.splitImpulsePenetrationThreshold = -0.015f;
    settings.solverErp = 0.18f;
    settings.solverErp2 = 0.16f;
    settings.maximumErrorReduction = 3.5f;
    settings.restitutionVelocityThreshold = 0.75f;
    PhysicsWorld world(settings);
    world.SetGravity(glm::vec3(0.0f));

    PhysicsBodyDesc thin = DynamicBodyDesc(
        PhysicsShapeDesc::Box(glm::vec3(0.10f, 0.50f, 0.20f)),
        glm::vec3(-2.0f, 0.0f, 0.0f)
    );
    thin.enableCcd = true;
    thin.ccdMotionThreshold = 0.05f;
    thin.ccdSweptSphereRadius = 0.08f;
    const PhysicsBodyHandle thinHandle = world.CreateBody(thin);
    const PhysicsBodyRuntimeSettings runtime =
        world.RuntimeSettings(thinHandle);

    Require(
        runtime.ccdEnabled &&
        NearlyEqual(runtime.ccdMotionThreshold, 0.05f) &&
        NearlyEqual(runtime.ccdSweptSphereRadius, 0.08f),
        "PhysicsWorld did not retain selective CCD settings"
    );
    Require(
        runtime.collisionMargin > 0.0f &&
        runtime.collisionMargin < 0.02f,
        "Size-related box collision margin was not applied"
    );

    const PhysicsWorldStatistics statistics = world.Statistics();
    Require(
        statistics.ccdBodyCount == 1U &&
        statistics.solverIterations == 21 &&
        statistics.splitImpulse &&
        NearlyEqual(
            statistics.splitImpulsePenetrationThreshold,
            -0.015f
        ) &&
        NearlyEqual(statistics.maximumErrorReduction, 3.5f) &&
        NearlyEqual(statistics.restitutionVelocityThreshold, 0.75f),
        "P1 solver/CCD statistics do not match the active Bullet policy"
    );

    PhysicsBodyDesc wall;
    wall.shape = PhysicsShapeDesc::Box(glm::vec3(0.05f, 2.0f, 2.0f));
    wall.motionType = PhysicsMotionType::Static;
    wall.position = glm::vec3(0.0f);
    world.CreateBody(wall);
    world.SetLinearVelocity(thinHandle, glm::vec3(300.0f, 0.0f, 0.0f));
    world.StepFixed(1.0f / 60.0f);
    Require(
        world.State(thinHandle).position.x < 0.5f,
        "Selective CCD allowed a fast thin body to tunnel through a wall"
    );
}

void TestBulletContactDiagnosticsAndPairIgnore()
{
    PhysicsWorld world;
    world.SetGravity(glm::vec3(0.0f));
    const PhysicsBodyHandle bodyA = world.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(-0.2f, 0.0f, 0.0f)
    ));
    const PhysicsBodyHandle bodyB = world.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(0.2f, 0.0f, 0.0f)
    ));
    world.StepFixed(1.0f / 60.0f);
    Require(
        !world.ContactPairs().empty() &&
        world.ContactPairs().front().contactPointCount > 0U &&
        world.ContactPairs().front().maximumPenetrationDepth > 0.0f &&
        world.Statistics().contactPairCount > 0U,
        "Bullet contact-pair diagnostics did not capture overlap data"
    );

    PhysicsWorld ignoredWorld;
    ignoredWorld.SetGravity(glm::vec3(0.0f));
    const PhysicsBodyHandle ignoredA = ignoredWorld.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(-0.2f, 0.0f, 0.0f)
    ));
    const PhysicsBodyHandle ignoredB = ignoredWorld.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(0.2f, 0.0f, 0.0f)
    ));
    ignoredWorld.SetCollisionPairIgnored(ignoredA, ignoredB, true);
    ignoredWorld.StepFixed(1.0f / 60.0f);
    Require(
        ignoredWorld.ContactPairs().empty(),
        "Explicit Bullet collision-pair ignore did not suppress contacts"
    );

    ignoredWorld.ConfigureCcd(ignoredA, true, 0.05f, 0.1f);
    Require(
        ignoredWorld.RuntimeSettings(ignoredA).ccdEnabled,
        "Runtime CCD activation was not applied"
    );
    ignoredWorld.ConfigureCcd(ignoredA, false, 0.0f, 0.0f);
    Require(
        !ignoredWorld.RuntimeSettings(ignoredA).ccdEnabled,
        "Runtime CCD deactivation was not applied"
    );
    ignoredWorld.Clear();
    Require(
        ignoredWorld.ContactPairs().empty() &&
        ignoredWorld.Statistics().contactPairCount == 0U,
        "Clearing the Bullet world retained stale contact-pair diagnostics"
    );
}

void TestBulletFoundationRigidBodies()
{
    PhysicsWorld world;
    world.CreateBody(StaticGroundDesc());
    const PhysicsBodyHandle sphere = world.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(-2.0f, 4.0f, 0.0f)
    ));
    const PhysicsBodyHandle box = world.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Box(glm::vec3(0.5f)),
        glm::vec3(0.0f, 4.0f, 0.0f)
    ));
    const PhysicsBodyHandle capsule = world.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Capsule(0.35f, 1.0f),
        glm::vec3(2.0f, 4.0f, 0.0f)
    ));

    StepPhysics(world, 360);
    const PhysicsBodyState sphereState = world.State(sphere);
    const PhysicsBodyState boxState = world.State(box);
    const PhysicsBodyState capsuleState = world.State(capsule);

    Require(
        sphereState.position.y > 0.35f && sphereState.position.y < 0.75f,
        "Bullet sphere did not settle on the ground"
    );
    Require(
        boxState.position.y > 0.35f && boxState.position.y < 0.75f,
        "Bullet box did not use its real box collision shape"
    );
    Require(
        capsuleState.position.y > 0.65f && capsuleState.position.y < 1.15f,
        "Bullet capsule did not settle at its expected height"
    );
}

void TestBulletFoundationCollisionFilters()
{
    PhysicsWorld world;
    PhysicsBodyDesc ground = StaticGroundDesc();
    ground.collisionGroup = 0x0001U;
    ground.collisionMask = 0x0001U;
    world.CreateBody(ground);

    PhysicsBodyDesc colliding = DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(-1.0f, 3.0f, 0.0f)
    );
    colliding.collisionGroup = 0x0001U;
    colliding.collisionMask = 0x0001U;
    const PhysicsBodyHandle collidingHandle = world.CreateBody(colliding);

    PhysicsBodyDesc filtered = DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(1.0f, 3.0f, 0.0f)
    );
    filtered.collisionGroup = 0x0002U;
    filtered.collisionMask = 0x0002U;
    const PhysicsBodyHandle filteredHandle = world.CreateBody(filtered);

    StepPhysics(world, 240);
    Require(
        world.State(collidingHandle).position.y > 0.3f,
        "Matching Bullet collision filters did not collide"
    );
    Require(
        world.State(filteredHandle).position.y < -2.0f,
        "Separated Bullet collision filters unexpectedly collided"
    );
}

void TestBulletFoundationKinematicAndHandles()
{
    PhysicsWorld world;
    PhysicsBodyDesc kinematic;
    kinematic.shape = PhysicsShapeDesc::Box(glm::vec3(0.5f));
    kinematic.motionType = PhysicsMotionType::Kinematic;
    kinematic.position = glm::vec3(0.0f, 1.0f, 0.0f);
    const PhysicsBodyHandle first = world.CreateBody(kinematic);

    world.SetTransform(
        first,
        glm::vec3(3.0f, 2.0f, -1.0f),
        glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        true
    );
    StepPhysics(world, 60);
    const PhysicsBodyState moved = world.State(first);
    Require(
        NearlyEqual(moved.position, glm::vec3(3.0f, 2.0f, -1.0f)),
        "Kinematic Bullet body did not retain an engine-driven transform"
    );

    Require(world.DestroyBody(first), "PhysicsWorld failed to destroy a body");
    Require(!world.Contains(first), "Destroyed physics handle stayed valid");
    Require(!world.DestroyBody(first), "PhysicsWorld destroyed a stale handle twice");

    const PhysicsBodyHandle second = world.CreateBody(kinematic);
    Require(
        second.index == first.index && second.generation != first.generation,
        "PhysicsWorld did not protect a recycled slot with a generation"
    );
    Require(world.BodyCount() == 1U, "PhysicsWorld body count is incorrect");

    bool staleRejected = false;
    try
    {
        world.State(first);
    }
    catch (const std::out_of_range&)
    {
        staleRejected = true;
    }
    Require(staleRejected, "PhysicsWorld accepted a stale body handle");

    world.Clear();
    Require(world.BodyCount() == 0U, "PhysicsWorld Clear kept live bodies");
    Require(!world.Contains(second), "PhysicsWorld Clear kept a handle valid");
}

void TestBulletFoundationFixedStepAndIsolation()
{
    Scene firstScene;
    Scene secondScene;
    Scene thirdScene;
    PhysicsWorld& firstWorld = firstScene.Physics();
    PhysicsWorld& secondWorld = secondScene.Physics();
    PhysicsWorld& thirdWorld = thirdScene.Physics();
    firstWorld.SetGravity(glm::vec3(0.0f, -9.8f, 0.0f));
    secondWorld.SetGravity(glm::vec3(0.0f, -9.8f, 0.0f));
    thirdWorld.SetGravity(glm::vec3(0.0f, -9.8f, 0.0f));

    const PhysicsBodyDesc falling = DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.5f),
        glm::vec3(0.0f, 10.0f, 0.0f)
    );
    const PhysicsBodyHandle first = firstWorld.CreateBody(falling);
    const PhysicsBodyHandle second = secondWorld.CreateBody(falling);
    const PhysicsBodyHandle third = thirdWorld.CreateBody(falling);

    for (int frame = 0; frame < 30; ++frame)
        firstScene.Update(1.0f / 30.0f);
    for (int frame = 0; frame < 60; ++frame)
        secondScene.Update(1.0f / 60.0f);
    for (int frame = 0; frame < 144; ++frame)
        thirdScene.Update(1.0f / 144.0f);

    Require(
        std::abs(
            firstWorld.State(first).position.y -
            secondWorld.State(second).position.y
        ) < 0.08f &&
        std::abs(
            secondWorld.State(second).position.y -
            thirdWorld.State(third).position.y
        ) < 0.08f,
        "WISTERIA fixed substeps changed across 30/60/144 FPS"
    );
    Require(
        firstScene.LastPhysicsFrameStatistics().substepCount == 2U &&
        secondScene.LastPhysicsFrameStatistics().substepCount == 1U,
        "Scene did not expose render-rate-independent fixed substep counts"
    );

    firstWorld.ApplyCentralImpulse(first, glm::vec3(3.0f, 0.0f, 0.0f));
    for (int frame = 0; frame < 10; ++frame)
        firstScene.Update(1.0f / 60.0f);
    Require(
        firstWorld.State(first).position.x > 0.1f,
        "Bullet central impulse did not affect the target body"
    );
    Require(
        std::abs(secondWorld.State(second).position.x) < Epsilon,
        "Separate PhysicsWorld instances contaminated each other"
    );
}

void TestScenePhysicsAccumulatorAndStatistics()
{
    Scene scene;
    Entity& entity = scene.CreateEntity();
    PhysicsLifecycleCounters counters;
    entity.SetPhysicsInstance(
        std::make_unique<CountingPhysicsInstance>(counters)
    );

    scene.Update(1.0f / 30.0f);
    const PhysicsFrameStatistics regular =
        scene.LastPhysicsFrameStatistics();
    Require(
        counters.prepareCount == 1 &&
        counters.substepCount == 2 &&
        counters.finishCount == 1 &&
        counters.substepAlphas.size() == 2U &&
        NearlyEqual(counters.substepAlphas[0U], 0.5f) &&
        NearlyEqual(counters.substepAlphas[1U], 1.0f),
        "Scene did not bracket a 30 FPS frame with two interpolated substeps"
    );
    Require(
        regular.substepCount == 2U &&
        NearlyEqual(regular.simulatedDeltaTime, 1.0f / 30.0f) &&
        regular.droppedTime < Epsilon &&
        regular.world.finite,
        "Scene fixed-step statistics are incorrect for a normal frame"
    );

    scene.Update(1.0f);
    const PhysicsFrameStatistics longFrame =
        scene.LastPhysicsFrameStatistics();
    Require(
        longFrame.substepCount == 4U &&
        longFrame.catchUpLimited &&
        longFrame.droppedTime > 0.9f &&
        longFrame.accumulatorTime < Epsilon,
        "Long frame did not cap catch-up work and report dropped time"
    );

    const PhysicsWorldStatistics world = scene.Physics().Statistics();
    Require(
        world.bodyCount == 0U &&
        world.constraintCount == 0U &&
        world.contactPointCount == 0U &&
        world.finite,
        "Empty PhysicsWorld statistics are inconsistent"
    );

    Scene highRateScene;
    Entity& highRateEntity = highRateScene.CreateEntity();
    PhysicsLifecycleCounters highRateCounters;
    highRateEntity.SetPhysicsInstance(
        std::make_unique<CountingPhysicsInstance>(highRateCounters)
    );
    highRateScene.Update(1.0f / 144.0f);
    highRateScene.Update(1.0f / 144.0f);
    highRateScene.Update(1.0f / 144.0f);
    Require(
        highRateCounters.substepCount == 1U &&
        highRateCounters.substepAlphas.size() == 1U &&
        std::abs(highRateCounters.substepAlphas.front() - 0.4f) < 0.001f,
        "High-rate render frames did not sample the exact 60 Hz tick time"
    );
}

void TestBulletSpring6DofFoundation()
{
    PhysicsWorld world;
    world.SetGravity(glm::vec3(0.0f));
    const PhysicsBodyHandle firstBody = world.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.2f),
        glm::vec3(-0.5f, 0.0f, 0.0f)
    ));
    const PhysicsBodyHandle secondBody = world.CreateBody(DynamicBodyDesc(
        PhysicsShapeDesc::Sphere(0.2f),
        glm::vec3(0.5f, 0.0f, 0.0f)
    ));

    PhysicsSpring6DofDesc constraint;
    constraint.bodyA = firstBody;
    constraint.bodyB = secondBody;
    constraint.frameA.position = glm::vec3(0.5f, 0.0f, 0.0f);
    constraint.frameB.position = glm::vec3(-0.5f, 0.0f, 0.0f);
    const PhysicsConstraintHandle handle =
        world.CreateSpring6DofConstraint(constraint);
    Require(
        world.ConstraintCount() == 1U && world.Contains(handle),
        "PhysicsWorld did not retain a Spring 6DOF constraint"
    );

    world.ApplyCentralImpulse(firstBody, glm::vec3(-4.0f, 0.0f, 0.0f));
    world.ApplyCentralImpulse(secondBody, glm::vec3(4.0f, 0.0f, 0.0f));
    StepPhysics(world, 120);
    const float separation = glm::distance(
        world.State(firstBody).position,
        world.State(secondBody).position
    );
    Require(
        std::abs(separation - 1.0f) < 0.15f,
        "Bullet Spring 6DOF did not preserve its locked anchor distance"
    );

    Require(world.DestroyBody(firstBody), "Failed to destroy constrained body");
    Require(
        world.ConstraintCount() == 0U && !world.Contains(handle),
        "DestroyBody did not remove constraints referencing the body"
    );
}

void TestBulletAdditionalConstraintsAndDebugDraw()
{
    PhysicsWorld world;
    world.SetGravity(glm::vec3(0.0f));

    std::array<PhysicsBodyHandle, 10U> bodies{};
    for (std::size_t index = 0; index < bodies.size(); ++index)
    {
        bodies[index] = world.CreateBody(DynamicBodyDesc(
            PhysicsShapeDesc::Sphere(0.12f),
            glm::vec3(
                static_cast<float>(index / 2U) * 3.0f,
                2.0f,
                static_cast<float>(index % 2U)
            )
        ));
    }

    PhysicsSixDofDesc sixDof;
    sixDof.bodyA = bodies[0U];
    sixDof.bodyB = bodies[1U];
    sixDof.linearLower = glm::vec3(-0.1f);
    sixDof.linearUpper = glm::vec3(0.1f);
    sixDof.angularLower = glm::vec3(-0.2f);
    sixDof.angularUpper = glm::vec3(0.2f);
    const PhysicsConstraintHandle sixDofHandle =
        world.CreateSixDofConstraint(sixDof);

    PhysicsPointToPointDesc point;
    point.bodyA = bodies[2U];
    point.bodyB = bodies[3U];
    point.pivotA = glm::vec3(0.0f, 0.0f, 0.5f);
    point.pivotB = glm::vec3(0.0f, 0.0f, -0.5f);
    const PhysicsConstraintHandle pointHandle =
        world.CreatePointToPointConstraint(point);

    PhysicsConeTwistDesc cone;
    cone.bodyA = bodies[4U];
    cone.bodyB = bodies[5U];
    cone.swingSpan1 = 0.4f;
    cone.swingSpan2 = 0.35f;
    cone.twistSpan = 0.25f;
    const PhysicsConstraintHandle coneHandle =
        world.CreateConeTwistConstraint(cone);

    PhysicsSliderDesc slider;
    slider.bodyA = bodies[6U];
    slider.bodyB = bodies[7U];
    slider.linearLower = -0.25f;
    slider.linearUpper = 0.25f;
    slider.angularLower = -0.15f;
    slider.angularUpper = 0.15f;
    const PhysicsConstraintHandle sliderHandle =
        world.CreateSliderConstraint(slider);

    PhysicsHingeDesc hinge;
    hinge.bodyA = bodies[8U];
    hinge.bodyB = bodies[9U];
    hinge.lowerAngle = -0.35f;
    hinge.upperAngle = 0.35f;
    const PhysicsConstraintHandle hingeHandle =
        world.CreateHingeConstraint(hinge);

    Require(
        world.ConstraintCount() == 5U &&
        world.Contains(sixDofHandle) &&
        world.Contains(pointHandle) &&
        world.Contains(coneHandle) &&
        world.Contains(sliderHandle) &&
        world.Contains(hingeHandle),
        "PhysicsWorld did not retain every PMX 2.1 constraint type"
    );

    world.SetDebugDrawEnabled(true);
    world.Step(1.0f / 60.0f);
    Require(
        !world.DebugLines().empty(),
        "Bullet debug draw did not collect any wireframe or constraint lines"
    );
    for (PhysicsBodyHandle body : bodies)
    {
        const PhysicsBodyState state = world.State(body);
        Require(
            std::isfinite(state.position.x) &&
            std::isfinite(state.rotation.w),
            "An additional Bullet constraint produced non-finite state"
        );
    }
    world.SetDebugDrawEnabled(false);
    Require(
        world.DebugLines().empty(),
        "Disabling physics debug draw retained stale lines"
    );
}

}

void TestGraphicsDevice()
{
    GraphicsDevice device;
    Require(device.Programs() != nullptr, "device owns a program cache");
    Require(device.ProgramCount() == 0, "fresh device has no programs");
    Require(!device.HasContextToken(), "fresh device has no context token");

    const void* token = reinterpret_cast<const void*>(std::uintptr_t(0x1234));
    device.SetContextToken(token);
    Require(device.HasContextToken(), "context token registered");
    Require(device.ContextToken() == token, "context token preserved");
    device.RequireContextToken(token);

    bool threw = false;
    try
    {
        device.RequireContextToken(
            reinterpret_cast<const void*>(std::uintptr_t(0x5678))
        );
    }
    catch (const std::logic_error&)
    {
        threw = true;
    }
    Require(threw, "mismatched context token must throw");

    device.ReleaseAll();
    Require(device.ProgramCount() == 0, "ReleaseAll clears the program cache");
}

void TestMaterialShadowFlags()
{
    MaterialData data;
    data.shadingModel = MaterialShadingModel::MmdToon;
    data.groundShadow = true;
    data.castSelfShadow = false;
    data.receiveSelfShadow = true;
    data.shaderInterface.shadowingSupported = true;
    Material material(data);
    Require(
        material.IsGroundShadow() &&
        !material.CastsSelfShadow() &&
        material.ReceivesSelfShadow() &&
        material.Interface().shadowingSupported,
        "Material MMD shadow flags were not preserved"
    );
}

void TestMmdPhysicsConfigurationPresets()
{
    const MmdPhysicsConfiguration raw =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    const MmdPhysicsConfiguration community =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdCommunity);
    const MmdPhysicsConfiguration adaptive =
        BuildPresetConfiguration(MmdPhysicsPreset::WisteriaAdaptive);

    Require(
        ValidateConfiguration(raw),
        "MMD_RAW preset configuration is invalid"
    );
    Require(
        ValidateConfiguration(community),
        "MMD_COMMUNITY preset configuration is invalid"
    );
    Require(
        ValidateConfiguration(adaptive),
        "WISTERIA_ADAPTIVE preset configuration is invalid"
    );
    Require(
        FormatConfigurationIdentity(raw) == "mmd-raw-v1",
        "MMD_RAW identity mismatch"
    );
    Require(
        FormatConfigurationIdentity(community) == "mmd-community-v1",
        "MMD_COMMUNITY identity mismatch"
    );
    Require(
        FormatConfigurationIdentity(adaptive) == "wisteria-adaptive-v1",
        "WISTERIA_ADAPTIVE identity mismatch"
    );

    // SabaBaseline v1 defaults.
    Require(
        NearlyEqual(raw.runtime.fixedTimeStep, 1.0f / 120.0f),
        "preset fixedTimeStep is not 1/120"
    );
    Require(raw.runtime.maxSubSteps == 10, "preset maxSubSteps is not 10");
    Require(
        NearlyEqual(raw.runtime.gravity, glm::vec3(0.0f, -98.0f, 0.0f)),
        "preset gravity is not -98"
    );
    Require(raw.runtime.enabled, "preset physics must be enabled");
    Require(
        raw.compatibility.linkedBodyCollision ==
            MmdLinkedBodyCollisionMode::PmxMaskOnly,
        "preset linked-body mode is not PmxMaskOnly"
    );
    Require(
        raw.compatibility.mode2 ==
            MmdMode2WritebackMode::PreserveAnimatedTranslation,
        "preset Mode 2 mode is not PreserveAnimatedTranslation"
    );
    Require(
        !raw.adaptive.recoveryEnabled &&
            !raw.adaptive.adaptiveCcdEnabled &&
            !raw.adaptive.adaptiveMarginEnabled &&
            !raw.adaptive.localChainEnhancementsEnabled,
        "Phase 0A adaptive policy must be fully disabled"
    );

    // Phase 0A: all presets are behaviour-identical, so the effective
    // fingerprint must not change with the display label (R1.3 §5).
    const std::uint64_t rawHash =
        ComputeEffectiveConfigurationFingerprint(raw);
    Require(
        rawHash == ComputeEffectiveConfigurationFingerprint(community),
        "preset label changed the effective configuration hash"
    );
    Require(
        rawHash == ComputeEffectiveConfigurationFingerprint(adaptive),
        "preset label changed the effective configuration hash"
    );
}

void TestMmdPhysicsConfigurationValidation()
{
    MmdPhysicsConfiguration anonymous;
    anonymous.identity.backend.clear();
    anonymous.identity.baseline.clear();
    Require(
        !ValidateConfiguration(anonymous),
        "anonymous configuration was accepted"
    );

    MmdPhysicsConfiguration zeroStep =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    zeroStep.runtime.fixedTimeStep = 0.0f;
    Require(
        !ValidateConfiguration(zeroStep),
        "zero fixedTimeStep was accepted"
    );

    MmdPhysicsConfiguration nanGravity =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    nanGravity.runtime.gravity.y = std::numeric_limits<float>::quiet_NaN();
    Require(
        !ValidateConfiguration(nanGravity),
        "NaN gravity was accepted"
    );

    MmdPhysicsConfiguration reservedLinked =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    reservedLinked.compatibility.linkedBodyCollision =
        MmdLinkedBodyCollisionMode::ForceEnableLinkedPairsDiagnostic;
    Require(
        !ValidateConfiguration(reservedLinked),
        "Reserved linked-body diagnostic mode was accepted"
    );

    MmdPhysicsConfiguration reservedMode2 =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    reservedMode2.compatibility.mode2 =
        MmdMode2WritebackMode::StrictBoneLength;
    Require(
        !ValidateConfiguration(reservedMode2),
        "Reserved StrictBoneLength mode was accepted"
    );

    MmdPhysicsConfiguration directDiagnostic =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    directDiagnostic.compatibility.mode2 =
        MmdMode2WritebackMode::FullTransformDiagnostic;
    Require(
        !ValidateConfiguration(directDiagnostic),
        "diagnostic Mode 2 accepted in a direct preset profile"
    );

    MmdPhysicsConfiguration badOrigin =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    badOrigin.identity.originPreset = "not-a-preset";
    Require(
        !ValidateConfiguration(badOrigin),
        "unknown originPreset was accepted"
    );

    // Phase 0A rejects unimplemented behaviour claims: gravityScale is not
    // applied by the Saba runtime yet, and no adaptive enhancement exists.
    MmdPhysicsConfiguration badScale =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    badScale.compatibility.gravityScale = 2.0f;
    Require(
        !ValidateConfiguration(badScale),
        "gravityScale != 1 was accepted"
    );

    MmdPhysicsConfiguration adaptiveOn =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    adaptiveOn.adaptive.recoveryEnabled = true;
    Require(
        !ValidateConfiguration(adaptiveOn),
        "unimplemented recovery flag was accepted"
    );
    adaptiveOn = BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    adaptiveOn.adaptive.adaptiveCcdEnabled = true;
    Require(
        !ValidateConfiguration(adaptiveOn),
        "unimplemented adaptive CCD flag was accepted"
    );
    adaptiveOn = BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    adaptiveOn.adaptive.adaptiveMarginEnabled = true;
    Require(
        !ValidateConfiguration(adaptiveOn),
        "unimplemented adaptive margin flag was accepted"
    );
    adaptiveOn = BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    adaptiveOn.adaptive.localChainEnhancementsEnabled = true;
    Require(
        !ValidateConfiguration(adaptiveOn),
        "unimplemented chain enhancement flag was accepted"
    );

    // A direct preset label may only represent the exact frozen preset;
    // behaviour mutations must carry a custom identity.
    MmdPhysicsConfiguration mutatedPreset =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    mutatedPreset.runtime.gravity.y = -9.8f;
    Require(
        !ValidateConfiguration(mutatedPreset),
        "mutated MMD_RAW gravity was accepted under the raw label"
    );
    mutatedPreset = BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    mutatedPreset.compatibility.linkedBodyCollision =
        MmdLinkedBodyCollisionMode::DisableConstraintLinkedPairs;
    Require(
        !ValidateConfiguration(mutatedPreset),
        "direct preset with a linked-body override was accepted"
    );

    MmdPhysicsConfiguration badRevision =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    badRevision.identity.profileRevision = 999U;
    Require(
        !ValidateConfiguration(badRevision),
        "unknown profile revision was accepted"
    );

    // custom-from-* may carry legal runtime overrides (closure fix): the
    // configuration a runtime exposes after a low-level override must pass
    // its own validator.
    MmdPhysicsConfiguration customOverride =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    customOverride.identity.originPreset = "mmd-raw";
    customOverride.runtime.gravity.y = -9.8f;
    Require(
        ValidateConfiguration(customOverride),
        "custom identity with a legal gravity override was rejected"
    );
    customOverride.runtime.fixedTimeStep = 1.0f / 60.0f;
    Require(
        ValidateConfiguration(customOverride),
        "custom identity with a legal timestep override was rejected"
    );

    // The custom label must still match its preset, and forbidden fields
    // stay forbidden even under a custom identity.
    MmdPhysicsConfiguration wrongOrigin = customOverride;
    wrongOrigin.identity.originPreset = "mmd-community";
    Require(
        !ValidateConfiguration(wrongOrigin),
        "custom identity with a mismatched origin was accepted"
    );
    customOverride.compatibility.gravityScale = 2.0f;
    Require(
        !ValidateConfiguration(customOverride),
        "custom config with gravityScale != 1 was accepted"
    );
    customOverride.compatibility.gravityScale = 1.0f;
    customOverride.adaptive.recoveryEnabled = true;
    Require(
        !ValidateConfiguration(customOverride),
        "custom config with an unimplemented adaptive flag was accepted"
    );
}

void TestMmdPhysicsConfigurationDerivation()
{
    const MmdPhysicsConfiguration base =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    const std::uint64_t baseHash =
        ComputeEffectiveConfigurationFingerprint(base);

    MmdPhysicsDiagnosticOverrides linkedOverrides;
    linkedOverrides.linkedBodyCollision =
        MmdLinkedBodyCollisionMode::DisableConstraintLinkedPairs;
    MmdPhysicsConfiguration derived;
    Require(
        DeriveDiagnosticConfiguration(base, linkedOverrides, derived) ==
            TimelineStatus::Ok,
        "deriving DisableConstraintLinkedPairs failed"
    );
    Require(
        derived.compatibility.linkedBodyCollision ==
            MmdLinkedBodyCollisionMode::DisableConstraintLinkedPairs,
        "linked-body override was not applied"
    );
    Require(
        derived.compatibility.mode2 == base.compatibility.mode2,
        "unrelated compatibility field changed during derivation"
    );
    Require(
        derived.identity.originPreset == "mmd-raw",
        "derived originPreset mismatch"
    );
    Require(
        FormatConfigurationIdentity(derived) == "custom-from-mmd-raw-v1",
        "derived identity mismatch"
    );
    Require(
        ValidateConfiguration(derived),
        "derived diagnostic configuration is invalid"
    );
    Require(
        ComputeEffectiveConfigurationFingerprint(derived) != baseHash,
        "behaviour override did not change the effective hash"
    );

    MmdPhysicsDiagnosticOverrides mode2Overrides;
    mode2Overrides.mode2 = MmdMode2WritebackMode::FullTransformDiagnostic;
    MmdPhysicsConfiguration mode2Derived;
    Require(
        DeriveDiagnosticConfiguration(base, mode2Overrides, mode2Derived) ==
            TimelineStatus::Ok,
        "deriving FullTransformDiagnostic failed"
    );
    Require(
        ValidateConfiguration(mode2Derived),
        "Mode 2 diagnostic derived configuration is invalid"
    );

    MmdPhysicsDiagnosticOverrides reservedOverrides;
    reservedOverrides.linkedBodyCollision =
        MmdLinkedBodyCollisionMode::ForceEnableLinkedPairsDiagnostic;
    MmdPhysicsConfiguration untouched = base;
    Require(
        DeriveDiagnosticConfiguration(base, reservedOverrides, untouched) ==
            TimelineStatus::InvalidState,
        "Reserved linked-body diagnostic derivation was accepted"
    );
    Require(
        untouched.identity.originPreset.empty(),
        "failed derivation modified the output configuration"
    );

    MmdPhysicsConfiguration invalidBase;
    invalidBase.identity.backend.clear();
    MmdPhysicsDiagnosticOverrides emptyOverrides;
    Require(
        DeriveDiagnosticConfiguration(
            invalidBase,
            emptyOverrides,
            untouched
        ) == TimelineStatus::InvalidState,
        "derivation from an invalid base was accepted"
    );

    Require(
        ComputeEffectiveConfigurationFingerprint(base) == baseHash,
        "derivation mutated the base configuration"
    );
}

void TestMmdPhysicsTraceJsonlRoundTrip()
{
    MmdPhysicsTraceFrame frame;
    frame.presetIdentity = "mmd-raw-v1";
    frame.effectiveConfigurationHash =
        FormatTraceHex(0x0123456789ABCDEFULL);
    frame.modelHash = FormatTraceHex(0x1111ULL);
    frame.motionHash = FormatTraceHex(0x2222ULL);
    frame.hasMotion = false;
    frame.frame = 7U;
    frame.physicsTick = 28U;
    frame.canonical = true;
    frame.poseHash.hex = "0123456789abcdef";
    frame.poseHash.valid = true;
    frame.physicsHash.hex = "fedcba9876543210";
    frame.physicsHash.valid = true;
    frame.vertexHash.hex = "0000000000000001";
    frame.vertexHash.valid = true;

    MmdPhysicsTraceBody body;
    body.index = 0U;
    body.mode = PmxRigidBodyMode::PhysicsWithBone;
    body.worldTransform.position = glm::vec3(1.0f, 2.0f, 3.0f);
    body.worldTransform.rotationBasis = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    body.interpolationWorldTransform.position =
        glm::vec3(0.5f, 2.0f, 3.0f);
    body.motionStateTransform.position = glm::vec3(1.0f, 2.0f, 3.0f);
    body.motionStateAvailable = true;
    body.linearVelocity = glm::vec3(0.1f, -0.2f, 0.3f);
    body.angularVelocity = glm::vec3(0.0f, 0.5f, 0.0f);
    frame.bodies.push_back(body);

    MmdPhysicsTraceBone bone;
    bone.index = 3U;
    bone.globalMatrix[12] = 42.0f;
    frame.bones.push_back(bone);

    MmdPhysicsTraceJoint joint;
    joint.index = 1U;
    joint.rawLinearError = 0.02f;
    joint.linearViolation = 0.0f;
    joint.rawAngularErrorDeg = 1.3f;
    joint.angularViolationDeg = 0.0f;
    frame.joints.push_back(joint);

    MmdPhysicsTraceContactPair pair;
    pair.bodyA = 4U;
    pair.bodyB = 2U;
    pair.pointCount = 2;
    pair.maxPenetration = -0.015f;
    pair.normalImpulse = 1.72f;
    frame.contactPairs.push_back(pair);
    frame.events.push_back("reset");

    std::ostringstream output;
    Require(
        wisteria::trace::WriteTraceFrameJson(frame, output),
        "trace JSONL write failed"
    );
    MmdPhysicsTraceFrame read;
    Require(
        wisteria::trace::ReadTraceFrameJson(output.str(), read),
        "trace JSONL parse failed"
    );
    Require(
        read.traceSchemaVersion == frame.traceSchemaVersion &&
            read.backendIdentity == frame.backendIdentity &&
            read.presetIdentity == frame.presetIdentity &&
            read.effectiveConfigurationHash ==
                frame.effectiveConfigurationHash &&
            read.executionProfile == frame.executionProfile &&
            read.modelHash == frame.modelHash &&
            read.motionHash == frame.motionHash &&
            read.hasMotion == frame.hasMotion &&
            read.frame == frame.frame &&
            read.physicsTick == frame.physicsTick &&
            read.canonical == frame.canonical,
        "trace scalar fields did not round-trip"
    );
    Require(
        read.poseHash.hex == frame.poseHash.hex &&
            read.poseHash.valid == frame.poseHash.valid &&
            read.physicsHash.hex == frame.physicsHash.hex &&
            read.physicsHash.valid == frame.physicsHash.valid &&
            read.vertexHash.hex == frame.vertexHash.hex &&
            read.vertexHash.valid == frame.vertexHash.valid,
        "trace state hashes did not round-trip"
    );
    Require(
        read.bodies.size() == 1U && read.bones.size() == 1U &&
            read.joints.size() == 1U &&
            read.contactPairs.size() == 1U &&
            read.events.size() == 1U,
        "trace array sizes did not round-trip"
    );
    Require(
        read.bodies[0].index == 0U &&
            read.bodies[0].mode == PmxRigidBodyMode::PhysicsWithBone &&
            NearlyEqual(read.bodies[0].worldTransform.position, body.worldTransform.position) &&
            read.bodies[0].worldTransform.rotationBasis ==
                body.worldTransform.rotationBasis &&
            NearlyEqual(
                read.bodies[0].interpolationWorldTransform.position,
                body.interpolationWorldTransform.position
            ) &&
            NearlyEqual(
                read.bodies[0].motionStateTransform.position,
                body.motionStateTransform.position
            ) &&
            read.bodies[0].motionStateAvailable &&
            NearlyEqual(read.bodies[0].linearVelocity, body.linearVelocity) &&
            NearlyEqual(
                read.bodies[0].angularVelocity,
                body.angularVelocity
            ),
        "trace body fields did not round-trip"
    );
    Require(
        read.bones[0].index == 3U &&
            NearlyEqual(read.bones[0].globalMatrix[12], 42.0f),
        "trace bone fields did not round-trip"
    );
    Require(
        read.joints[0].index == 1U &&
            NearlyEqual(read.joints[0].rawLinearError, 0.02f) &&
            NearlyEqual(read.joints[0].rawAngularErrorDeg, 1.3f),
        "trace joint fields did not round-trip"
    );
    Require(
        read.contactPairs[0].bodyA == 4U &&
            read.contactPairs[0].bodyB == 2U &&
            read.contactPairs[0].pointCount == 2 &&
            NearlyEqual(
                read.contactPairs[0].maxPenetration,
                -0.015f
            ) &&
            NearlyEqual(read.contactPairs[0].normalImpulse, 1.72f),
        "trace contact fields did not round-trip"
    );
    Require(read.events[0] == "reset", "trace events did not round-trip");
}

void TestMmdPhysicsAudit()
{
    std::vector<MmdRigidBodyDefinition> bodies;
    MmdRigidBodyDefinition sphere;
    sphere.name = "sphere";
    sphere.shape = MmdRigidBodyShape::Sphere;
    sphere.size = glm::vec3(0.5f, 0.0f, 0.0f);
    bodies.push_back(sphere);
    MmdRigidBodyDefinition box;
    box.name = "box";
    box.shape = MmdRigidBodyShape::Box;
    box.size = glm::vec3(1.0f, 2.0f, 0.5f);
    bodies.push_back(box);
    MmdRigidBodyDefinition capsule;
    capsule.name = "capsule";
    capsule.shape = MmdRigidBodyShape::Capsule;
    capsule.size = glm::vec3(0.25f, 1.0f, 0.0f);
    bodies.push_back(capsule);

    std::vector<MmdJointDefinition> joints;
    MmdJointDefinition jointA;
    jointA.name = "joint-a";
    jointA.bodyA = 0U;
    jointA.bodyB = 1U;
    jointA.linearLower = glm::vec3(0.0f);
    jointA.linearUpper = glm::vec3(1.0f, 0.0f, 0.0f);
    joints.push_back(jointA);
    MmdJointDefinition jointB;
    jointB.name = "joint-b";
    jointB.bodyA = 1U;
    jointB.bodyB = 2U;
    jointB.linearLower = glm::vec3(-2.0f, -1.0f, -0.5f);
    jointB.linearUpper = glm::vec3(2.0f, 1.0f, 0.5f);
    jointB.angularLower = glm::vec3(-1.0f, 0.0f, 0.0f);
    jointB.angularUpper = glm::vec3(1.0f, 0.0f, 0.0f);
    joints.push_back(jointB);
    MmdJointDefinition jointZero;
    jointZero.name = "joint-zero";
    jointZero.bodyA = 0U;
    jointZero.bodyB = 2U;
    joints.push_back(jointZero);
    MmdPhysicsAsset asset(std::move(bodies), std::move(joints));

    std::vector<Bone> bones(4U);
    bones[0].name = "root";
    bones[1].name = "child1";
    bones[1].parentIndex = 0U;
    bones[1].bindLocalMatrix =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    bones[2].name = "child2";
    bones[2].parentIndex = 1U;
    bones[2].bindLocalMatrix =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
    bones[3].name = "child3";
    bones[3].parentIndex = 0U;
    bones[3].bindLocalMatrix =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));

    MmdPhysicsAuditBounds bounds;
    bounds.available = true;
    bounds.min = glm::vec3(0.0f);
    bounds.max = glm::vec3(1.0f);
    MmdPhysicsAuditOptions options;
    options.collisionMargin = 0.05f;

    const MmdPhysicsAuditResult audit = RunMmdPhysicsAudit(
        asset,
        bones,
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw),
        bounds,
        options
    );
    Require(
        audit.modelHeightAvailable &&
            NearlyEqual(audit.modelHeight, 1.0f),
        "audit model height mismatch"
    );
    Require(
        audit.boneLength.available &&
            audit.boneLength.count == 3U &&
            audit.boneLength.zeroCount == 1U &&
            NearlyEqual(audit.boneLength.minPositive, 1.0f) &&
            NearlyEqual(audit.boneLength.median, 1.0f) &&
            NearlyEqual(audit.boneLength.max, 2.0f),
        "audit bone length statistics mismatch"
    );
    Require(
        audit.rigidBodySize.available &&
            audit.rigidBodySize.count == 3U &&
            NearlyEqual(audit.rigidBodySize.minPositive, 1.0f) &&
            NearlyEqual(audit.rigidBodySize.median, 1.5f) &&
            NearlyEqual(audit.rigidBodySize.max, 2.0f),
        "audit rigid body size statistics mismatch"
    );
    Require(
        audit.jointLinearRange.available &&
            audit.jointLinearRange.count == 3U &&
            audit.jointLinearRange.zeroCount == 1U &&
            NearlyEqual(audit.jointLinearRange.minPositive, 1.0f),
        "audit joint linear range mismatch"
    );
    Require(
        audit.jointAngularRangeDeg.available &&
            audit.jointAngularRangeDeg.count == 3U &&
            audit.jointAngularRangeDeg.zeroCount == 2U &&
            NearlyEqual(
                audit.jointAngularRangeDeg.minPositive,
                glm::degrees(2.0f)
            ),
        "audit joint angular range mismatch"
    );
    Require(
        audit.gravityAvailable &&
            NearlyEqual(audit.gravityMagnitude, 98.0f) &&
            audit.gravityPerModelHeightAvailable &&
            NearlyEqual(audit.gravityPerModelHeight, 98.0f),
        "audit gravity metrics mismatch"
    );
    Require(
        NearlyEqual(audit.fixedTimeStep, 1.0f / 120.0f),
        "audit fixedTimeStep mismatch"
    );
    Require(
        audit.shapeMarginRatioAvailable &&
            NearlyEqual(
                audit.shapeMarginPerMedianBodySize,
                0.05f / 1.5f
            ),
        "audit shape margin ratio mismatch"
    );

    // Degenerate model height: no division by zero, ratio stays unavailable.
    MmdPhysicsAuditBounds degenerate = bounds;
    degenerate.max = degenerate.min;
    const MmdPhysicsAuditResult degenerateAudit = RunMmdPhysicsAudit(
        asset,
        bones,
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw),
        degenerate,
        options
    );
    Require(
        !degenerateAudit.modelHeightAvailable &&
            !degenerateAudit.gravityPerModelHeightAvailable,
        "degenerate model height produced a ratio"
    );

    const MmdPhysicsAsset empty(
        std::vector<MmdRigidBodyDefinition>{},
        std::vector<MmdJointDefinition>{}
    );
    const MmdPhysicsAuditResult emptyAudit = RunMmdPhysicsAudit(
        empty,
        std::span<const Bone>{},
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw)
    );
    Require(
        !emptyAudit.boneLength.available &&
            !emptyAudit.rigidBodySize.available &&
            !emptyAudit.jointLinearRange.available &&
            !emptyAudit.jointAngularRangeDeg.available &&
            !emptyAudit.shapeMarginRatioAvailable,
        "empty audit reported unavailable data as available"
    );

    // Non-finite samples must be reported, never silently dropped, while
    // valid samples still contribute statistics.
    std::vector<Bone> mixedBones(3U);
    mixedBones[0].name = "root";
    mixedBones[1].name = "child-valid";
    mixedBones[1].parentIndex = 0U;
    mixedBones[1].bindLocalMatrix =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    mixedBones[2].name = "child-nan";
    mixedBones[2].parentIndex = 0U;
    mixedBones[2].bindLocalMatrix =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
    mixedBones[2].bindLocalMatrix[3] = glm::vec4(
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
        0.0f,
        1.0f
    );
    const MmdPhysicsAuditResult nanAudit = RunMmdPhysicsAudit(
        asset,
        mixedBones,
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw)
    );
    Require(
        !nanAudit.finite && !nanAudit.boneLength.finite,
        "audit did not report non-finite bone lengths"
    );
    Require(
        nanAudit.boneLength.nonFiniteCount == 1U &&
            nanAudit.boneLength.count == 1U &&
            nanAudit.boneLength.available &&
            NearlyEqual(nanAudit.boneLength.median, 1.0f),
        "audit did not separate valid samples from non-finite ones"
    );

    // Non-finite inputs outside the ranges must also poison result.finite.
    MmdPhysicsAuditBounds nanBounds;
    nanBounds.available = true;
    nanBounds.max.y = std::numeric_limits<float>::quiet_NaN();
    const MmdPhysicsAuditResult nanBoundsAudit = RunMmdPhysicsAudit(
        asset,
        std::span<const Bone>{},
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw),
        nanBounds
    );
    Require(
        !nanBoundsAudit.finite,
        "non-finite model bounds did not poison the audit result"
    );

    MmdPhysicsConfiguration nanGravityConfig =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    nanGravityConfig.runtime.gravity.z =
        std::numeric_limits<float>::quiet_NaN();
    const MmdPhysicsAuditResult nanGravityAudit = RunMmdPhysicsAudit(
        asset,
        std::span<const Bone>{},
        nanGravityConfig
    );
    Require(
        !nanGravityAudit.finite && !nanGravityAudit.gravityAvailable,
        "non-finite gravity did not poison the audit result"
    );

    MmdPhysicsConfiguration nanStepConfig =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    nanStepConfig.runtime.fixedTimeStep =
        std::numeric_limits<float>::quiet_NaN();
    const MmdPhysicsAuditResult nanStepAudit = RunMmdPhysicsAudit(
        asset,
        std::span<const Bone>{},
        nanStepConfig
    );
    Require(
        !nanStepAudit.finite,
        "non-finite fixedTimeStep did not poison the audit result"
    );

    MmdPhysicsAuditOptions nanMargin;
    nanMargin.collisionMargin = std::numeric_limits<float>::quiet_NaN();
    const MmdPhysicsAuditResult nanMarginAudit = RunMmdPhysicsAudit(
        asset,
        std::span<const Bone>{},
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw),
        {},
        nanMargin
    );
    Require(
        !nanMarginAudit.finite,
        "non-finite collision margin did not poison the audit result"
    );
}

bool TransformsEqualForWire(
    const RigidTransformSnapshot& left,
    const RigidTransformSnapshot& right
)
{
    return left.position == right.position &&
        left.rotationBasis == right.rotationBasis;
}

bool BodiesEqualForWire(
    const RigidBodySnapshot& left,
    const RigidBodySnapshot& right
)
{
    return left.index == right.index &&
        left.mode == right.mode &&
        left.definitionMass == right.definitionMass &&
        TransformsEqualForWire(
            left.worldTransform,
            right.worldTransform
        ) &&
        TransformsEqualForWire(
            left.interpolationTransform,
            right.interpolationTransform
        ) &&
        left.linearVelocity == right.linearVelocity &&
        left.angularVelocity == right.angularVelocity &&
        left.interpolationLinearVelocity ==
            right.interpolationLinearVelocity &&
        left.interpolationAngularVelocity ==
            right.interpolationAngularVelocity &&
        left.totalForce == right.totalForce &&
        left.totalTorque == right.totalTorque &&
        left.activationState == right.activationState &&
        left.deactivationTime == right.deactivationTime;
}

bool PhysicsSnapshotsEqualForWire(
    const PhysicsSnapshot& left,
    const PhysicsSnapshot& right
)
{
    if (left.schemaVersion != right.schemaVersion ||
        left.layoutFingerprint != right.layoutFingerprint ||
        left.physicsConfigurationFingerprint !=
            right.physicsConfigurationFingerprint ||
        left.motionFrame != right.motionFrame ||
        left.physicsTick != right.physicsTick ||
        left.jointCount != right.jointCount ||
        left.canonical != right.canonical ||
        left.rigidBodies.size() != right.rigidBodies.size())
    {
        return false;
    }
    for (std::size_t index = 0U; index < left.rigidBodies.size(); ++index)
    {
        if (!BodiesEqualForWire(
                left.rigidBodies[index],
                right.rigidBodies[index]
            ))
        {
            return false;
        }
    }
    return true;
}

bool OverridesEqualForWire(
    const UserOverrideState& left,
    const UserOverrideState& right
)
{
    if (left.physicsEnabled != right.physicsEnabled ||
        left.loopMotion != right.loopMotion ||
        left.morphOverrides.size() != right.morphOverrides.size() ||
        left.ikOverrides.size() != right.ikOverrides.size())
    {
        return false;
    }
    for (std::size_t index = 0U;
         index < left.morphOverrides.size();
         ++index)
    {
        if (left.morphOverrides[index] != right.morphOverrides[index])
            return false;
    }
    for (std::size_t index = 0U;
         index < left.ikOverrides.size();
         ++index)
    {
        if (left.ikOverrides[index] != right.ikOverrides[index])
            return false;
    }
    return true;
}

bool ReplayConfigsEqualForWire(
    const ReplayConfig& left,
    const ReplayConfig& right
)
{
    return left.motionFps == right.motionFps &&
        left.physicsHz == right.physicsHz &&
        left.warmupFrames == right.warmupFrames &&
        left.loopMotion == right.loopMotion;
}

bool AssetIdentitiesEqualForWire(
    const AssetIdentity& left,
    const AssetIdentity& right
)
{
    return left.pmxFileHash == right.pmxFileHash &&
        left.vmdFileHash == right.vmdFileHash &&
        left.hasMotion == right.hasMotion &&
        left.layoutFingerprint == right.layoutFingerprint &&
        left.physicsConfigurationFingerprint ==
            right.physicsConfigurationFingerprint;
}

bool DeterminismHashesEqualForWire(
    const DeterminismHashes& left,
    const DeterminismHashes& right
)
{
    return left.exactHash == right.exactHash &&
        left.canonicalHash == right.canonicalHash &&
        left.valid == right.valid;
}

bool CheckpointsEqualForWire(
    const FrameCheckpoint& left,
    const FrameCheckpoint& right
)
{
    return left.frame == right.frame &&
        PhysicsSnapshotsEqualForWire(left.physics, right.physics) &&
        OverridesEqualForWire(left.overrides, right.overrides) &&
        ReplayConfigsEqualForWire(left.config, right.config) &&
        left.fingerprint.schemaVersion == right.fingerprint.schemaVersion &&
        left.fingerprint.frame == right.fingerprint.frame &&
        AssetIdentitiesEqualForWire(
            left.fingerprint.asset,
            right.fingerprint.asset
        ) &&
        ReplayConfigsEqualForWire(
            left.fingerprint.config,
            right.fingerprint.config
        ) &&
        OverridesEqualForWire(
            left.fingerprint.overrides,
            right.fingerprint.overrides
        ) &&
        DeterminismHashesEqualForWire(
            left.fingerprint.state.pose,
            right.fingerprint.state.pose
        ) &&
        DeterminismHashesEqualForWire(
            left.fingerprint.state.physics,
            right.fingerprint.state.physics
        ) &&
        DeterminismHashesEqualForWire(
            left.fingerprint.state.vertex,
            right.fingerprint.state.vertex
        );
}

FrameCheckpoint MakeSerializableCheckpoint()
{
    FrameCheckpoint checkpoint;
    checkpoint.frame = 120U;
    checkpoint.config.motionFps = 30U;
    checkpoint.config.physicsHz = 120U;
    checkpoint.config.warmupFrames = 0U;
    checkpoint.config.loopMotion = false;

    checkpoint.overrides.physicsEnabled = true;
    checkpoint.overrides.loopMotion = false;
    checkpoint.overrides.morphOverrides = {
        {"blink", 0.5f},
        {"mouth", 0.25f}
    };
    checkpoint.overrides.ikOverrides = {
        {"legL", true},
        {"legR", false}
    };

    checkpoint.fingerprint.schemaVersion = 1U;
    checkpoint.fingerprint.frame = checkpoint.frame;
    checkpoint.fingerprint.asset.pmxFileHash = 0x1122334455667788ULL;
    checkpoint.fingerprint.asset.vmdFileHash = 0x8877665544332211ULL;
    checkpoint.fingerprint.asset.hasMotion = true;
    checkpoint.fingerprint.asset.layoutFingerprint = 0xAABBCCDD00112233ULL;
    checkpoint.fingerprint.asset.physicsConfigurationFingerprint =
        0x5566778899AABBCCULL;
    checkpoint.fingerprint.config = checkpoint.config;
    checkpoint.fingerprint.overrides = checkpoint.overrides;
    checkpoint.fingerprint.state.pose.exactHash = 0x1111111111111111ULL;
    checkpoint.fingerprint.state.pose.canonicalHash = 0x2222222222222222ULL;
    checkpoint.fingerprint.state.pose.valid = true;
    checkpoint.fingerprint.state.physics.exactHash = 0x3333333333333333ULL;
    checkpoint.fingerprint.state.physics.canonicalHash = 0x4444444444444444ULL;
    checkpoint.fingerprint.state.physics.valid = true;
    checkpoint.fingerprint.state.vertex.exactHash = 0x5555555555555555ULL;
    checkpoint.fingerprint.state.vertex.canonicalHash = 0x6666666666666666ULL;
    checkpoint.fingerprint.state.vertex.valid = true;

    checkpoint.physics.schemaVersion = 2U;
    checkpoint.physics.layoutFingerprint =
        checkpoint.fingerprint.asset.layoutFingerprint;
    checkpoint.physics.physicsConfigurationFingerprint =
        checkpoint.fingerprint.asset.physicsConfigurationFingerprint;
    checkpoint.physics.motionFrame = checkpoint.frame;
    checkpoint.physics.physicsTick = checkpoint.frame * 4U;
    checkpoint.physics.jointCount = 3U;
    checkpoint.physics.canonical = true;

    RigidBodySnapshot bodyA;
    bodyA.index = 0U;
    bodyA.mode = PmxRigidBodyMode::Physics;
    bodyA.definitionMass = 2.5f;
    bodyA.worldTransform.position = glm::vec3(1.0f, 2.0f, 3.0f);
    bodyA.worldTransform.rotationBasis = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    bodyA.interpolationTransform.position = glm::vec3(0.5f, -1.0f, 2.0f);
    bodyA.interpolationTransform.rotationBasis = {
        0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    bodyA.linearVelocity = glm::vec3(0.1f, -0.2f, 0.3f);
    bodyA.angularVelocity = glm::vec3(0.01f, 0.02f, -0.03f);
    bodyA.interpolationLinearVelocity = glm::vec3(1.0f, 0.0f, 0.0f);
    bodyA.interpolationAngularVelocity = glm::vec3(0.0f, 1.0f, 0.0f);
    bodyA.totalForce = glm::vec3(-9.8f, 0.0f, 0.0f);
    bodyA.totalTorque = glm::vec3(0.0f, 0.0f, 0.5f);
    bodyA.activationState = 1;
    bodyA.deactivationTime = 0.25f;
    checkpoint.physics.rigidBodies.push_back(bodyA);

    RigidBodySnapshot bodyB = bodyA;
    bodyB.index = 1U;
    bodyB.mode = PmxRigidBodyMode::FollowBone;
    bodyB.definitionMass = 0.0f;
    bodyB.worldTransform.position = glm::vec3(0.0f, 10.0f, 0.0f);
    bodyB.interpolationTransform.position = bodyB.worldTransform.position;
    bodyB.linearVelocity = glm::vec3(0.0f);
    bodyB.angularVelocity = glm::vec3(0.0f);
    bodyB.interpolationLinearVelocity = glm::vec3(0.0f);
    bodyB.interpolationAngularVelocity = glm::vec3(0.0f);
    bodyB.totalForce = glm::vec3(0.0f);
    bodyB.totalTorque = glm::vec3(0.0f);
    bodyB.activationState = 0;
    bodyB.deactivationTime = 0.0f;
    checkpoint.physics.rigidBodies.push_back(bodyB);
    return checkpoint;
}

constexpr std::uint64_t kTestFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kTestFnvPrime = 1099511628211ULL;

std::uint64_t TestFnv1a64(const std::vector<std::uint8_t>& bytes)
{
    std::uint64_t state = kTestFnvOffsetBasis;
    for (const std::uint8_t byte : bytes)
    {
        state ^= byte;
        state *= kTestFnvPrime;
    }
    return state;
}

void WriteU32Le(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value
)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

void WriteU64Le(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint64_t value
)
{
    for (int shift = 0; shift < 64; shift += 8)
    {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

void RecomputeCheckpointChecksum(std::vector<std::uint8_t>& bytes)
{
    const std::size_t checksumOffset =
        static_cast<std::size_t>(CheckpointWireHeaderSize) - 8U;
    std::fill(
        bytes.begin() + static_cast<std::ptrdiff_t>(checksumOffset),
        bytes.begin() + static_cast<std::ptrdiff_t>(checksumOffset + 8U),
        0U
    );
    const std::uint64_t checksum = TestFnv1a64(bytes);
    for (int shift = 0; shift < 64; shift += 8)
    {
        bytes[checksumOffset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>((checksum >> shift) & 0xFFU);
    }
}

std::size_t TopLevelAssetIdentityOffset(
    const FrameCheckpoint& checkpoint
)
{
    std::size_t cursor =
        static_cast<std::size_t>(CheckpointWireHeaderSize);
    cursor += 4U + 8U + 8U;  // payload schema + frame + physicsTick
    cursor += 4U + 4U + 4U + 1U;  // replay config
    cursor += 1U + 1U + 4U;  // overrides: enabled + loop + morph count
    for (const auto& entry : checkpoint.overrides.morphOverrides)
        cursor += 4U + entry.first.size() + 4U;
    cursor += 4U;  // ik count
    for (const auto& entry : checkpoint.overrides.ikOverrides)
        cursor += 4U + entry.first.size() + 1U;
    return cursor;
}

std::size_t BodyCountFieldOffset(const FrameCheckpoint& checkpoint)
{
    std::size_t cursor =
        static_cast<std::size_t>(CheckpointWireHeaderSize);
    cursor += 4U + 8U + 8U;  // payload schema + frame + physicsTick
    cursor += 4U + 4U + 4U + 1U;  // replay config
    cursor += 1U + 1U + 4U;  // overrides: enabled + loop + morph count
    for (const auto& entry : checkpoint.overrides.morphOverrides)
        cursor += 4U + entry.first.size() + 4U;
    cursor += 4U;  // ik count
    for (const auto& entry : checkpoint.overrides.ikOverrides)
        cursor += 4U + entry.first.size() + 1U;
    cursor += 8U + 8U + 1U + 8U + 8U;  // asset identity
    cursor += 4U + 8U;  // fingerprint schema + frame
    cursor += 8U + 8U + 1U + 8U + 8U;  // fingerprint asset
    cursor += 4U + 4U + 4U + 1U;  // fingerprint config
    cursor += 1U + 1U + 4U;  // fingerprint overrides header
    for (const auto& entry : checkpoint.overrides.morphOverrides)
        cursor += 4U + entry.first.size() + 4U;
    cursor += 4U;  // fingerprint ik count
    for (const auto& entry : checkpoint.overrides.ikOverrides)
        cursor += 4U + entry.first.size() + 1U;
    cursor += 17U + 17U + 17U;  // pose + physics + vertex hashes
    cursor += 4U + 8U + 8U + 8U + 8U + 4U + 1U;  // physics snapshot header
    return cursor;
}

void TestCheckpointWireCodec()
{
    Require(
        CheckpointWireHeaderSize == 48U,
        "checkpoint wire header size drifted from 48"
    );
    const FrameCheckpoint valid = MakeSerializableCheckpoint();
    const std::vector<std::uint8_t> bytes = SerializeCheckpoint(valid);
    Require(
        bytes.size() > CheckpointWireHeaderSize,
        "serialized checkpoint is smaller than the wire header"
    );

    FrameCheckpoint decoded;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            {},
            decoded
        ) == TimelineStatus::Ok,
        "valid checkpoint round trip failed"
    );
    Require(
        CheckpointsEqualForWire(valid, decoded),
        "round trip changed checkpoint fields"
    );

    // Production serialize writes the engine-owned identity in the header.
    const std::size_t buildIdOffset = 24U;  // magic(4) + 5 U32 ids
    std::uint64_t wireBuildId = 0U;
    for (int shift = 0; shift < 64; shift += 8)
    {
        wireBuildId |=
            static_cast<std::uint64_t>(
                bytes[buildIdOffset + static_cast<std::size_t>(shift / 8)]
            ) << shift;
    }
    Require(
        wireBuildId == CurrentBuildCompatibilityId(),
        "production serializer did not write the engine-owned build id"
    );
    Require(
        CurrentBuildCompatibilityId() != 0U,
        "engine build identity must be non-zero"
    );

    // Tampered payload byte is rejected and output is left untouched.
    std::vector<std::uint8_t> tampered = bytes;
    tampered[bytes.size() / 2U] ^= 0x01U;
    FrameCheckpoint untouched = decoded;
    Require(
        DeserializeCheckpoint(
            tampered.data(),
            tampered.size(),
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "tampered checkpoint was accepted"
    );
    Require(
        CheckpointsEqualForWire(untouched, decoded),
        "failed deserialize modified the output"
    );

    // Build-compatibility mismatch (test-only override).
    CheckpointSerializationOptions otherBuild;
    otherBuild.buildCompatibilityIdOverride =
        CurrentBuildCompatibilityId() + 1U;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            otherBuild,
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "build-compatibility mismatch was accepted"
    );

    // Default deserialize must also reject a foreign identity.
    CheckpointSerializationOptions foreignBytes;
    foreignBytes.buildCompatibilityIdOverride =
        CurrentBuildCompatibilityId() + 1U;
    const std::vector<std::uint8_t> foreignBytesSerialized =
        SerializeCheckpoint(valid, foreignBytes);
    Require(
        DeserializeCheckpoint(
            foreignBytesSerialized.data(),
            foreignBytesSerialized.size(),
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "default deserializer accepted a foreign build identity"
    );

    // A zero override is invalid on deserialize too (interface contract).
    CheckpointSerializationOptions zeroOverride;
    zeroOverride.buildCompatibilityIdOverride = 0U;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            zeroOverride,
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "zero build identity override was accepted by deserialize"
    );

    // Truncation.
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size() - 1U,
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "truncated checkpoint was accepted"
    );

    // Untrusted-input limits: max payload bytes.
    CheckpointSerializationOptions tinyPayload;
    tinyPayload.maxPayloadBytes =
        bytes.size() - CheckpointWireHeaderSize - 1U;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            tinyPayload,
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "over-limit payload was accepted"
    );

    // Untrusted-input limits: max morph / IK / string sizes.
    CheckpointSerializationOptions tinyMorphCount;
    tinyMorphCount.maxMorphOverrideCount = 1U;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            tinyMorphCount,
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "over-limit morph override count was accepted"
    );
    CheckpointSerializationOptions tinyString;
    tinyString.maxStringBytes = 4U;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            tinyString,
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "over-limit override name was accepted"
    );

    // Untrusted-input limits: max rigid body count.
    CheckpointSerializationOptions tinyBodyCount;
    tinyBodyCount.maxRigidBodyCount = 1U;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            tinyBodyCount,
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "over-limit body count was accepted"
    );

    // Absurd body count with a valid checksum still fails structurally
    // before any unbounded allocation.
    std::vector<std::uint8_t> hugeCount = bytes;
    WriteU32Le(
        hugeCount,
        BodyCountFieldOffset(valid),
        0xFFFFFFF0U
    );
    RecomputeCheckpointChecksum(hugeCount);
    CheckpointSerializationOptions unlimited;
    unlimited.maxRigidBodyCount =
        std::numeric_limits<std::uint64_t>::max();
    Require(
        DeserializeCheckpoint(
            hugeCount.data(),
            hugeCount.size(),
            unlimited,
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "absurd body count was accepted"
    );

    // Duplicate-field tamper detection: the top-level physicsTick must
    // match the physics-snapshot copy even with a recomputed checksum.
    std::vector<std::uint8_t> tickTampered = bytes;
    WriteU64Le(
        tickTampered,
        static_cast<std::size_t>(CheckpointWireHeaderSize) + 4U + 8U,
        valid.physics.physicsTick + 1U
    );
    RecomputeCheckpointChecksum(tickTampered);
    Require(
        DeserializeCheckpoint(
            tickTampered.data(),
            tickTampered.size(),
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "tampered top-level physicsTick was accepted"
    );

    // Duplicate-field tamper detection: the top-level asset identity must
    // match the fingerprint copy.
    std::vector<std::uint8_t> assetTampered = bytes;
    WriteU64Le(
        assetTampered,
        TopLevelAssetIdentityOffset(valid) + 8U + 8U + 1U,
        valid.fingerprint.asset.layoutFingerprint ^ 0x12345678ULL
    );
    RecomputeCheckpointChecksum(assetTampered);
    Require(
        DeserializeCheckpoint(
            assetTampered.data(),
            assetTampered.size(),
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "tampered top-level asset identity was accepted"
    );

    // NaN float payload is rejected by the codec.
    FrameCheckpoint nanBody = valid;
    nanBody.physics.rigidBodies[0U].linearVelocity.x =
        std::numeric_limits<float>::quiet_NaN();
    const std::vector<std::uint8_t> nanBytes =
        SerializeCheckpoint(nanBody);
    Require(
        DeserializeCheckpoint(
            nanBytes.data(),
            nanBytes.size(),
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "NaN physics payload was accepted"
    );

    // Non-finite override weight is rejected by the codec.
    FrameCheckpoint nanMorph = valid;
    nanMorph.overrides.morphOverrides[0U].second =
        std::numeric_limits<float>::quiet_NaN();
    const std::vector<std::uint8_t> nanMorphBytes =
        SerializeCheckpoint(nanMorph);
    Require(
        DeserializeCheckpoint(
            nanMorphBytes.data(),
            nanMorphBytes.size(),
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "NaN morph weight was accepted"
    );

    // Structurally inconsistent duplicate fields are rejected.
    FrameCheckpoint inconsistent = valid;
    inconsistent.frame = 121U;
    const std::vector<std::uint8_t> inconsistentBytes =
        SerializeCheckpoint(inconsistent);
    Require(
        DeserializeCheckpoint(
            inconsistentBytes.data(),
            inconsistentBytes.size(),
            {},
            decoded
        ) == TimelineStatus::InvalidCheckpoint,
        "structurally inconsistent checkpoint was accepted"
    );

    // Null / short inputs.
    Require(
        DeserializeCheckpoint(nullptr, 0U, {}, decoded) ==
            TimelineStatus::InvalidCheckpoint,
        "empty input was accepted"
    );
    Require(
        DeserializeCheckpoint(bytes.data(), 0U, {}, decoded) ==
            TimelineStatus::InvalidCheckpoint,
        "zero-length input was accepted"
    );

    // Zero build compatibility id override is invalid on serialize.
    CheckpointSerializationOptions zeroBuild;
    zeroBuild.buildCompatibilityIdOverride = 0U;
    bool threw = false;
    try
    {
        (void)SerializeCheckpoint(valid, zeroBuild);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    Require(threw, "zero buildCompatibilityId did not throw");
}

int main()
{
    int failures = 0;
    failures += !RunTest(
        "Bullet foundation validation",
        TestBulletFoundationValidation
    );
    failures += !RunTest(
        "Bullet P1 CCD, margins and solver",
        TestBulletP1CcdMarginsAndSolver
    );
    failures += !RunTest(
        "Bullet contact diagnostics and pair ignore",
        TestBulletContactDiagnosticsAndPairIgnore
    );
    failures += !RunTest(
        "Bullet rigid bodies",
        TestBulletFoundationRigidBodies
    );
    failures += !RunTest(
        "Bullet collision filters",
        TestBulletFoundationCollisionFilters
    );
    failures += !RunTest(
        "Bullet kinematic bodies and handles",
        TestBulletFoundationKinematicAndHandles
    );
    failures += !RunTest(
        "Bullet fixed step and world isolation",
        TestBulletFoundationFixedStepAndIsolation
    );
    failures += !RunTest(
        "Scene physics accumulator and statistics",
        TestScenePhysicsAccumulatorAndStatistics
    );
    failures += !RunTest(
        "Bullet Spring 6DOF foundation",
        TestBulletSpring6DofFoundation
    );
    failures += !RunTest(
        "Bullet additional constraints and debug draw",
        TestBulletAdditionalConstraintsAndDebugDraw
    );
    failures += !RunTest("Skeleton and Pose", TestSkeletonAndPose);
    failures += !RunTest("Skeleton validation", TestSkeletonValidation);
    failures += !RunTest(
        "Animation sampling and Animator",
        TestAnimationSamplingAndAnimator
    );
    failures += !RunTest("Root motion", TestRootMotion);
    failures += !RunTest("MMD append and IK constraints", TestMmdBoneConstraints);
    failures += !RunTest(
        "Determinism hash validation",
        TestDeterminismHashValidation
    );
    failures += !RunTest(
        "Checkpoint wire codec",
        TestCheckpointWireCodec
    );
    failures += !RunTest(
        "Bullet linked-body collision disable",
        TestBulletLinkedBodyCollisionDisable
    );
    failures += !RunTest(
        "MMD physics configuration presets",
        TestMmdPhysicsConfigurationPresets
    );
    failures += !RunTest(
        "MMD physics configuration validation",
        TestMmdPhysicsConfigurationValidation
    );
    failures += !RunTest(
        "MMD physics configuration derivation",
        TestMmdPhysicsConfigurationDerivation
    );
    failures += !RunTest(
        "MMD physics trace JSONL round trip",
        TestMmdPhysicsTraceJsonlRoundTrip
    );
    failures += !RunTest(
        "MMD physics unit audit",
        TestMmdPhysicsAudit
    );
    failures += !RunTest("Morph runtime", TestMorphRuntime);
    failures += !RunTest(
        "Bone and UV morph evaluation",
        TestBoneAndUvMorphEvaluation
    );
    failures += !RunTest(
        "Saba adapter interface compilation",
        TestInterfaceCompilation
    );
    failures += !RunTest("FXAA settings", TestFxaaSettings);
    failures += !RunTest("GraphicsDevice ownership", TestGraphicsDevice);
    failures += !RunTest("Material shadow flags", TestMaterialShadowFlags);
    return failures == 0 ? 0 : 1;
}
