#include "test_support.hpp"

namespace
{
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

    Pose& GetPose() override
    {
        return this->pose;
    }

    bool NeedsDynamicVertexUpload() const noexcept override
    {
        return false;
    }

    void UploadDynamicVertices(Mesh&) override
    {
    }

    PhysicsInstance* TryGetPhysicsInstance() noexcept override
    {
        return nullptr;
    }

    void SetMmdIkEnabled(BoneIndex, bool) override
    {
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

    void ApplyCameraMotion(float, Camera&) override
    {
    }

    void ApplyCameraTrack(const CameraTrack&, float, Camera&) override
    {
    }

    bool LoadLightMotion(const std::filesystem::path&) override
    {
        return false;
    }

    void ApplyLightMotion(float, DirectionalLight&) override
    {
    }

    void ApplyLightTrack(const LightTrack&, float, DirectionalLight&) override
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
    failures += !RunTest("Morph runtime", TestMorphRuntime);
    failures += !RunTest(
        "Saba adapter interface compilation",
        TestInterfaceCompilation
    );
    failures += !RunTest("FXAA settings", TestFxaaSettings);
    failures += !RunTest("GraphicsDevice ownership", TestGraphicsDevice);
    return failures == 0 ? 0 : 1;
}
