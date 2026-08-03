#include "wisteria/animation/animation.hpp"
#include "wisteria/animation/animator.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/rendering/primitives/cube.hpp"
#include "wisteria/scene/entity.hpp"
#include "wisteria/assets/importer.hpp"
#include "wisteria/platform/input.hpp"
#include "wisteria/assets/manager.hpp"
#include "wisteria/assets/model_asset.hpp"
#include "wisteria/mmd/physics/mmd_physics_instance.hpp"
#include "wisteria/mmd/physics_compat/mmd_compat_physics_instance.hpp"
#include "wisteria/animation/pose.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/physics/physics_world.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/scene/scene.hpp"
#include "wisteria/mmd/vmd_importer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
constexpr float Epsilon = 0.0001f;
const std::filesystem::path TestAssetDirectory = WISTERIA_TEST_ASSET_DIR;
const std::filesystem::path ProjectAssetDirectory = WISTERIA_PROJECT_ASSET_DIR;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool NearlyEqual(float left, float right)
{
    return std::abs(left - right) <= Epsilon;
}

bool NearlyEqual(const glm::vec3& left, const glm::vec3& right)
{
    return NearlyEqual(left.x, right.x) &&
        NearlyEqual(left.y, right.y) &&
        NearlyEqual(left.z, right.z);
}

bool NearlyEqual(const glm::vec4& left, const glm::vec4& right)
{
    return NearlyEqual(left.x, right.x) &&
        NearlyEqual(left.y, right.y) &&
        NearlyEqual(left.z, right.z) &&
        NearlyEqual(left.w, right.w);
}

bool NearlySameRotation(const glm::quat& left, const glm::quat& right)
{
    return NearlyEqual(std::abs(glm::dot(
        glm::normalize(left),
        glm::normalize(right)
    )), 1.0f);
}

bool NearlyEqual(const glm::mat4& left, const glm::mat4& right)
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (!NearlyEqual(left[column][row], right[column][row]))
                return false;
        }
    }
    return true;
}

float MatrixIdentityDeviation(const glm::mat4& matrix)
{
    float result = 0.0f;
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            result = std::max(
                result,
                std::abs(matrix[column][row] -
                    (column == row ? 1.0f : 0.0f))
            );
        }
    }
    return result;
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

void TestAnimatedModelImporter()
{
    const std::filesystem::path modelPath =
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "animated_triangle.gltf";
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.skeleton.has_value(), "Animated glTF lost its Skeleton");
    Require(imported.animations.size() == 1, "Animated glTF lost its clip");
    Require(
        imported.animations[0].Name() == "moveRoot" &&
        NearlyEqual(imported.animations[0].Duration(), 1.0f) &&
        imported.animations[0].TrackCount() == 1,
        "Assimp animation metadata was imported incorrectly"
    );
    const std::optional<BoneIndex> rootBone =
        imported.skeleton->FindBone("rootBone");
    Require(rootBone.has_value(), "Animated glTF root bone was not imported");
    const AnimationTrack* track =
        imported.animations[0].FindTrack(*rootBone);
    Require(track != nullptr, "Animation channel was not mapped to its bone");
    Require(
        NearlyEqual(
            track->Sample(0.5f, BoneTransform{}).translation.x,
            1.0f
        ),
        "Imported animation keys were not converted to seconds"
    );

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("animatedTriangle", modelPath);
    Require(
        model.AnimationClipCount() == 1 &&
        model.FindAnimationClip("moveRoot") != nullptr,
        "ResourceManager lost imported animation data"
    );
    Scene scene;
    Entity& entity = scene.InstantiateModel(model);
    scene.Update(0.25f);
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(*rootBone)[3].x, 0.5f),
        "Imported animation did not drive the instantiated Pose"
    );
}

void TestExtendedPmxMorphImporter()
{
    const std::filesystem::path modelPath =
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "extended_morph.pmx";
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.skeleton.has_value() &&
        imported.skeleton->BoneCount() == 2U,
        "Extended PMX fixture lost its imported Skeleton"
    );
    Require(
        imported.meshes.size() == 1U &&
        imported.materials.size() == 1U &&
        imported.morphs.size() == 5U,
        "Extended PMX fixture changed its resource counts"
    );

    const MorphSet morphSet(imported.morphs);
    const std::optional<MorphIndex> vertexMorph =
        morphSet.FindMorph("vertex");
    const std::optional<MorphIndex> boneMorphIndex =
        morphSet.FindMorph("bone");
    const std::optional<MorphIndex> uvMorph = morphSet.FindMorph("uv");
    const std::optional<MorphIndex> materialMorphIndex =
        morphSet.FindMorph("materialMorph");
    const std::optional<MorphIndex> groupMorph =
        morphSet.FindMorph("group");
    Require(
        vertexMorph.has_value() && boneMorphIndex.has_value() &&
        uvMorph.has_value() && materialMorphIndex.has_value() &&
        groupMorph.has_value(),
        "Extended PMX fixture lost one or more named morphs"
    );
    const MorphIndex vertexIndex = *vertexMorph;
    const MorphIndex boneIndex = *boneMorphIndex;
    const MorphIndex uvIndex = *uvMorph;
    const MorphIndex materialIndex = *materialMorphIndex;
    const MorphIndex groupIndex = *groupMorph;
    Require(
        morphSet.DefinitionAt(vertexIndex).kind == MorphKind::Vertex &&
        morphSet.DefinitionAt(boneIndex).kind == MorphKind::Bone &&
        morphSet.DefinitionAt(uvIndex).kind == MorphKind::Uv &&
        morphSet.DefinitionAt(materialIndex).kind == MorphKind::Material &&
        morphSet.DefinitionAt(groupIndex).kind == MorphKind::Group,
        "PMX morph types 1, 2, 3, 8, or 0 were mapped incorrectly"
    );

    const MorphDefinition& boneMorph = morphSet.DefinitionAt(boneIndex);
    Require(
        boneMorph.boneOffsets.size() == 1U &&
        imported.skeleton->BoneAt(
            boneMorph.boneOffsets[0U].boneIndex
        ).name == "root",
        "PMX Bone Morph source index was not remapped to the Skeleton"
    );
    const MorphDefinition& materialMorph =
        morphSet.DefinitionAt(materialIndex);
    Require(
        materialMorph.materialOffsets.size() == 1U &&
        materialMorph.materialOffsets[0U].materialIndex == 0U &&
        materialMorph.materialOffsets[0U].operation ==
            MaterialMorphOperation::Add,
        "PMX Material Morph metadata was imported incorrectly"
    );

    MorphState state(morphSet);
    state.SetWeight(groupIndex, 1.0f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[boneIndex], 0.5f) &&
        NearlyEqual(effective[uvIndex], 2.0f) &&
        NearlyEqual(effective[materialIndex], 1.0f) &&
        NearlyEqual(effective[groupIndex], 0.0f),
        "Imported PMX Group Morph did not drive extended morph kinds"
    );

    PoseBuffer pose(*imported.skeleton);
    morphSet.ApplyBoneMorphs(effective, pose);
    const BoneTransform& transformed = pose.TransformAt(
        boneMorph.boneOffsets[0U].boneIndex
    );
    Require(
        NearlyEqual(
            transformed.translation,
            glm::vec3(0.5f, 1.0f, -1.5f)
        ) &&
        NearlySameRotation(
            transformed.rotation,
            glm::angleAxis(
                glm::radians(-45.0f),
                glm::vec3(1.0f, 0.0f, 0.0f)
            )
        ),
        "Imported PMX Bone Morph coordinate conversion is incorrect"
    );

    const ImportedMeshData& importedMeshData = imported.meshes[0U];
    Mesh importedMesh(
        DefaultModelData{
            importedMeshData.data.vertices,
            importedMeshData.data.indices,
            importedMeshData.data.layout
        },
        importedMeshData.requiredBoneCount,
        importedMeshData.morphTargets
    );
    std::vector<MorphVertexDelta> deltas;
    Require(
        importedMeshData.morphMaterialIndex ==
            std::optional<std::uint32_t>(0U) &&
        importedMeshData.morphTargets.size() == 2U &&
        importedMesh.CalculateMorphDeltas(effective, deltas),
        "Imported PMX UV Morph did not create a runtime mesh target"
    );
    bool foundUvDelta = false;
    for (const MorphVertexDelta& delta : deltas)
    {
        if (NearlyEqual(
                delta.uv[0U],
                glm::vec4(0.5f, 1.0f, 0.0f, 0.0f)
            ))
        {
            foundUvDelta = true;
            break;
        }
    }
    Require(foundUvDelta, "Imported PMX UV Morph delta changed");

    MaterialMorphValues values;
    values.edgeSize = 1.0f;
    morphSet.ApplyMaterialMorphs(0U, effective, values);
    Require(
        NearlyEqual(values.diffuse.x, 1.2f) &&
        NearlyEqual(values.edgeSize, 1.5f) &&
        NearlyEqual(values.textureFactor.x, 1.1f),
        "Imported PMX Material Morph values changed"
    );
}

void TestVmdImporter()
{
    std::vector<std::uint8_t> bytes;
    const auto appendValue = [&bytes]<typename T>(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const std::size_t begin = bytes.size();
        bytes.resize(begin + sizeof(T));
        std::memcpy(bytes.data() + begin, &value, sizeof(T));
    };
    const auto appendFixed = [&bytes](std::string_view value, std::size_t size)
    {
        const std::size_t begin = bytes.size();
        bytes.resize(begin + size, 0U);
        const std::size_t copySize = std::min(value.size(), size);
        std::memcpy(bytes.data() + begin, value.data(), copySize);
    };
    const auto linearInterpolation = []
    {
        std::array<std::uint8_t, 64> result{};
        for (std::size_t offset : {0U, 16U, 32U, 48U})
        {
            result[offset] = 20U;
            result[offset + 4U] = 20U;
            result[offset + 8U] = 107U;
            result[offset + 12U] = 107U;
        }
        return result;
    };
    const auto appendBoneFrame = [
        &appendValue,
        &appendFixed,
        &bytes
    ](
        std::string_view shiftJisName,
        std::uint32_t frame,
        const glm::vec3& translation,
        const glm::quat& rotation,
        const std::array<std::uint8_t, 64>& interpolation
    )
    {
        appendFixed(shiftJisName, 15U);
        appendValue(frame);
        appendValue(translation.x);
        appendValue(translation.y);
        appendValue(translation.z);
        appendValue(rotation.x);
        appendValue(rotation.y);
        appendValue(rotation.z);
        appendValue(rotation.w);
        bytes.insert(
            bytes.end(),
            interpolation.begin(),
            interpolation.end()
        );
    };

    appendFixed("Vocaloid Motion Data 0002", 30U);
    appendFixed("testModel", 20U);
    appendValue(std::uint32_t{3U});

    const std::string headShiftJis("\x93\xAA", 2U);
    const std::string unknownShiftJis("\x96\xA2\x92\x6D", 4U);
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    appendBoneFrame(
        headShiftJis,
        0U,
        glm::vec3(0.0f),
        identity,
        linearInterpolation()
    );

    std::array<std::uint8_t, 64> curved = linearInterpolation();
    curved[0U] = 64U;
    curved[4U] = 0U;
    curved[8U] = 127U;
    curved[12U] = 64U;
    appendBoneFrame(
        headShiftJis,
        30U,
        glm::vec3(2.0f, 0.0f, 4.0f),
        glm::angleAxis(
            glm::radians(90.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ),
        curved
    );
    appendBoneFrame(
        unknownShiftJis,
        60U,
        glm::vec3(8.0f),
        identity,
        linearInterpolation()
    );

    // Morph frames are followed by empty camera/light/self-shadow sections
    // and VMD model frames containing per-controller IK switches.
    appendValue(std::uint32_t{3U});
    const auto appendMorphFrame = [
        &appendValue,
        &appendFixed
    ](std::string_view name, std::uint32_t frame, float weight)
    {
        appendFixed(name, 15U);
        appendValue(frame);
        appendValue(weight);
    };
    appendMorphFrame("smile", 0U, 0.0f);
    appendMorphFrame("smile", 30U, 1.0f);
    appendMorphFrame("unknownMorph", 15U, 0.5f);
    appendValue(std::uint32_t{0U});
    appendValue(std::uint32_t{0U});
    appendValue(std::uint32_t{0U});
    appendValue(std::uint32_t{3U});
    const auto appendIkModelFrame = [
        &appendValue,
        &appendFixed
    ](
        std::uint32_t frame,
        bool enabled,
        bool includeUnknown
    )
    {
        appendValue(frame);
        appendValue(std::uint8_t{1U});
        appendValue(std::uint32_t{includeUnknown ? 2U : 1U});
        appendFixed("ikController", 20U);
        appendValue(static_cast<std::uint8_t>(enabled));
        if (includeUnknown)
        {
            appendFixed("unknownIk", 20U);
            appendValue(std::uint8_t{0U});
        }
    };
    appendIkModelFrame(0U, true, false);
    appendIkModelFrame(15U, false, true);
    appendIkModelFrame(30U, true, false);

    const glm::mat4 headBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 2.0f, 3.0f)
    );
    Bone head{
        "\xE9\xA0\xAD",
        InvalidBoneIndex,
        headBind,
        glm::inverse(headBind)
    };
    Bone ikLink{"ikLink"};
    Bone ikEffector{"ikEffector"};
    ikEffector.parentIndex = 1U;
    ikEffector.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    ikEffector.inverseBindMatrix = glm::inverse(
        ikEffector.bindLocalMatrix
    );
    Bone ikController{"ikController"};
    ikController.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    ikController.inverseBindMatrix = glm::inverse(
        ikController.bindLocalMatrix
    );
    ikController.ikConstraint = MmdIkConstraint{
        2U,
        8U,
        glm::radians(45.0f),
        {MmdIkLink{1U}}
    };
    Skeleton skeleton({
        std::move(head),
        std::move(ikLink),
        std::move(ikEffector),
        std::move(ikController)
    });
    MorphSet morphSet({MorphDefinition{"smile", MorphCategory::Mouth}});
    const ImportedVmdAnimationData imported = VmdImporter().Import(
        bytes,
        skeleton,
        "memoryMotion",
        VmdImportOptions{.clipName = "walk"},
        &morphSet
    );

    Require(imported.modelName == "testModel", "VMD model name was not decoded");
    Require(
        imported.sourceBoneTrackCount == 2U &&
        imported.unmatchedBoneNames.size() == 1U,
        "VMD unmatched-bone reporting is incorrect"
    );
    Require(
        imported.sourceMorphTrackCount == 2U &&
        imported.unmatchedMorphNames.size() == 1U &&
        imported.clip.MorphWeightTrackCount() == 1U,
        "VMD morph reporting or name mapping is incorrect"
    );
    Require(
        imported.sourceIkStateTrackCount == 2U &&
        imported.unmatchedIkNames.size() == 1U &&
        imported.clip.MmdIkStateTrackCount() == 1U,
        "VMD IK-state reporting or controller mapping is incorrect"
    );
    Require(
        imported.clip.Name() == "walk" &&
        NearlyEqual(imported.clip.Duration(), 1.0f) &&
        imported.clip.TrackCount() == 1U,
        "VMD clip metadata is incorrect"
    );

    const AnimationTrack* track = imported.clip.FindTrack(0U);
    Require(track != nullptr, "VMD bone name was not mapped from Shift-JIS");
    Require(
        track->TranslationKeys()[1].interpolation[0].mode ==
            AnimationInterpolation::CubicBezier &&
        track->TranslationKeys()[1].interpolation[0].Evaluate(0.5f) < 0.5f,
        "VMD Bezier interpolation was not imported"
    );
    const BoneTransform endPose = track->Sample(1.0f, BoneTransform{});
    Require(
        NearlyEqual(endPose.translation.x, 3.0f) &&
        NearlyEqual(endPose.translation.y, 2.0f) &&
        NearlyEqual(endPose.translation.z, -1.0f),
        "VMD translation or handedness conversion is incorrect"
    );
    Require(
        endPose.rotation.y < -0.7f,
        "VMD quaternion handedness conversion is incorrect"
    );

    const MmdIkStateTrack* ikTrack =
        imported.clip.FindMmdIkStateTrack(3U);
    Require(
        ikTrack != nullptr && ikTrack->Sample(0.0f) &&
        !ikTrack->Sample(0.5f) && ikTrack->Sample(1.0f),
        "VMD IK switches were not imported as discrete keyframes"
    );
    const MorphWeightTrack* morphTrack =
        imported.clip.FindMorphWeightTrack(0U);
    Require(
        morphTrack != nullptr &&
        NearlyEqual(morphTrack->Sample(0.5f), 0.5f) &&
        NearlyEqual(morphTrack->Sample(1.0f), 1.0f),
        "VMD morph weights were not imported with linear interpolation"
    );

    Pose pose(skeleton);
    MorphState morphState(morphSet);
    Animator animator(pose, &morphState);
    animator.Play(imported.clip);
    animator.SetTime(0.5f);
    Require(
        !animator.IsMmdIkEnabled(3U) &&
        NearlyEqual(morphState.Weight(0U), 0.5f) &&
        glm::length(
            glm::vec3(pose.GlobalMatrix(2U)[3]) -
            glm::vec3(1.0f, 0.0f, 0.0f)
        ) < 0.001f,
        "Animator did not apply a disabled VMD IK state"
    );
    animator.SetTime(1.0f);
    Require(
        animator.IsMmdIkEnabled(3U) &&
        NearlyEqual(morphState.Weight("smile"), 1.0f) &&
        glm::length(
            glm::vec3(pose.GlobalMatrix(2U)[3]) -
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) < 0.001f,
        "Animator did not re-enable IK at the next VMD keyframe"
    );
    animator.SetMmdIkEnabled(3U, false);
    Require(
        !animator.IsMmdIkEnabled(3U),
        "Manual IK override did not take priority over VMD"
    );
    animator.ClearMmdIkOverride(3U);
    Require(
        animator.IsMmdIkEnabled(3U),
        "Clearing a manual IK override did not restore VMD control"
    );

    AnimationClip alwaysEnabled(
        "alwaysEnabled",
        1.0f,
        {},
        {MmdIkStateTrack(3U, {BoolKeyframe{0.0f, true}})}
    );
    AnimationClip alwaysDisabled(
        "alwaysDisabled",
        1.0f,
        {},
        {MmdIkStateTrack(3U, {BoolKeyframe{0.0f, false}})}
    );
    animator.Play(alwaysEnabled);
    animator.CrossFade(alwaysDisabled, 1.0f);
    animator.Update(0.49f);
    Require(
        animator.IsMmdIkEnabled(3U),
        "Cross Fade switched its discrete IK state too early"
    );
    animator.Update(0.02f);
    Require(
        !animator.IsMmdIkEnabled(3U),
        "Cross Fade did not switch to the destination IK state"
    );

    AnimationClip neutralExpression(
        "neutralExpression",
        1.0f,
        {},
        {},
        {MorphWeightTrack(0U, {FloatKeyframe{0.0f, 0.0f}})}
    );
    AnimationClip smileExpression(
        "smileExpression",
        1.0f,
        {},
        {},
        {MorphWeightTrack(0U, {FloatKeyframe{0.0f, 1.0f}})}
    );
    animator.Play(neutralExpression);
    animator.CrossFade(smileExpression, 1.0f);
    animator.Update(0.5f);
    Require(
        NearlyEqual(morphState.Weight("smile"), 0.5f),
        "Cross Fade did not blend source and destination morph weights"
    );

    bool invalidSignatureRejected = false;
    try
    {
        std::vector<std::uint8_t> invalid = bytes;
        invalid[0] = static_cast<std::uint8_t>('X');
        static_cast<void>(VmdImporter().Import(
            invalid,
            skeleton,
            "invalid"
        ));
    }
    catch (const std::runtime_error&)
    {
        invalidSignatureRejected = true;
    }
    Require(invalidSignatureRejected, "VMD importer accepted an invalid signature");
}

void TestVmdAssetWhenAvailable()
{
    const std::filesystem::path directory =
        ProjectAssetDirectory / "models" / "mmd" / u8"凑企鹅";
    const std::filesystem::path modelPath = directory / u8"凑企鹅.pmx";
    const std::filesystem::path motionPath = directory / "penguin_walking.vmd";
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

    const ImportedModelData model = ModelImporter().Import(modelPath);
    Require(model.skeleton.has_value(), "VMD test PMX has no Skeleton");
    std::optional<MorphSet> morphSet;
    if (!model.morphs.empty())
        morphSet.emplace(model.morphs);
    const ImportedVmdAnimationData motion = VmdImporter().Import(
        motionPath,
        *model.skeleton,
        {},
        morphSet.has_value() ? &*morphSet : nullptr
    );
    Require(
        motion.modelName == "Adelie Tomori_arm" &&
        motion.sourceBoneTrackCount == 44U,
        "Real VMD header or bone-track count changed"
    );
    Require(
        motion.clip.TrackCount() >= 20U &&
        NearlyEqual(motion.clip.Duration(), 0.6f),
        "Real VMD motion did not bind to the PMX Skeleton"
    );
    Require(
        motion.clip.MorphWeightTrackCount() +
            motion.unmatchedMorphNames.size() ==
            motion.sourceMorphTrackCount,
        "Real VMD morph tracks were not completely mapped or reported"
    );
}

void TestModelAssetSkeleton()
{
    ModelAsset model("skeletonModel");
    Require(!model.HasSkeleton(), "Static ModelAsset unexpectedly has a skeleton");
    Require(model.TryGetSkeleton() == nullptr, "Missing skeleton returned a pointer");

    bool missingRejected = false;
    try
    {
        static_cast<void>(model.GetSkeleton());
    }
    catch (const std::logic_error&)
    {
        missingRejected = true;
    }
    Require(missingRejected, "ModelAsset returned a missing skeleton");

    model.SetSkeleton(Skeleton({Bone{"root"}}));
    Require(model.HasSkeleton(), "ModelAsset did not store its skeleton");
    Require(
        model.TryGetSkeleton() == &model.GetSkeleton() &&
        model.GetSkeleton().BoneCount() == 1,
        "ModelAsset skeleton access is inconsistent"
    );

    bool replacementRejected = false;
    try
    {
        model.SetSkeleton(Skeleton({Bone{"replacement"}}));
    }
    catch (const std::logic_error&)
    {
        replacementRejected = true;
    }
    Require(replacementRejected, "ModelAsset allowed its skeleton to be replaced");
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

void TestPmx21FlipImpulseMorphRuntime()
{
    MorphDefinition first;
    first.name = "first";
    first.kind = MorphKind::Vertex;

    MorphDefinition second;
    second.name = "second";
    second.kind = MorphKind::Material;

    MorphDefinition flip;
    flip.name = "flip";
    flip.kind = MorphKind::Flip;
    flip.flipMembers = {
        FlipMorphMember{0U, 0.25f},
        FlipMorphMember{1U, 0.75f}
    };

    MorphDefinition impulse;
    impulse.name = "impulse";
    impulse.kind = MorphKind::Impulse;
    impulse.impulseOffsets = {
        ImpulseMorphOffset{
            2U,
            false,
            glm::vec3(1.0f, 2.0f, 3.0f),
            glm::vec3(4.0f, 5.0f, 6.0f)
        },
        ImpulseMorphOffset{
            2U,
            true,
            glm::vec3(-1.0f, 0.0f, 2.0f),
            glm::vec3(0.5f, -0.5f, 1.0f)
        },
        ImpulseMorphOffset{
            2U,
            false,
            glm::vec3(0.0f),
            glm::vec3(0.0f)
        }
    };

    MorphDefinition nested;
    nested.name = "nested";
    nested.kind = MorphKind::Group;
    nested.groupMembers = {
        GroupMorphMember{2U, 1.0f},
        GroupMorphMember{3U, 2.0f}
    };

    MorphDefinition outer;
    outer.name = "outer";
    outer.kind = MorphKind::Group;
    outer.groupMembers = {GroupMorphMember{4U, 1.0f}};

    MorphSet morphSet({
        std::move(first),
        std::move(second),
        std::move(flip),
        std::move(impulse),
        std::move(nested),
        std::move(outer)
    });
    Require(
        morphSet.HasKind(MorphKind::Flip) &&
        morphSet.HasKind(MorphKind::Impulse),
        "MorphSet did not retain PMX 2.1 morph kinds"
    );

    MorphState state(morphSet);
    state.SetWeight("first", 0.9f);
    state.SetWeight("flip", 0.2f);
    std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.9f) &&
        NearlyEqual(effective[1U], 0.0f),
        "Flip Morph selected a target inside its initial empty interval"
    );

    state.SetWeight("flip", 0.34f);
    effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.25f) &&
        NearlyEqual(effective[1U], 0.0f) &&
        NearlyEqual(effective[2U], 0.0f),
        "Flip Morph did not overwrite the first selected target"
    );

    state.Reset();
    state.SetWeight("second", 0.2f);
    state.SetWeight("flip", 0.67f);
    effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.0f) &&
        NearlyEqual(effective[1U], 0.75f),
        "Flip Morph did not overwrite the second selected target"
    );
    state.SetWeight("flip", 2.0f);
    Require(
        NearlyEqual(state.EffectiveWeights()[1U], 0.75f),
        "Flip Morph did not clamp an above-range control to its last target"
    );

    state.Reset();
    state.SetWeight("outer", 0.67f);
    effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[1U], 0.75f) &&
        NearlyEqual(effective[3U], 1.34f) &&
        NearlyEqual(effective[4U], 0.0f) &&
        NearlyEqual(effective[5U], 0.0f),
        "Nested Group Morph did not drive Flip and Impulse Morphs"
    );

    std::vector<MmdRigidBodyImpulse> impulses;
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.size() == 1U &&
        impulses[0U].rigidBodyIndex == 2U &&
        impulses[0U].reset &&
        NearlyEqual(
            impulses[0U].globalLinearImpulse,
            glm::vec3(1.34f, 2.68f, 4.02f)
        ) &&
        NearlyEqual(
            impulses[0U].globalTorqueImpulse,
            glm::vec3(5.36f, 6.7f, 8.04f)
        ) &&
        NearlyEqual(
            impulses[0U].localLinearImpulse,
            glm::vec3(-1.34f, 0.0f, 2.68f)
        ) &&
        NearlyEqual(
            impulses[0U].localTorqueImpulse,
            glm::vec3(0.67f, -0.67f, 1.34f)
        ),
        "Impulse Morph aggregation did not preserve local/global channels"
    );

    state.Reset();
    state.SetWeight("impulse", -0.5f);
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.size() == 1U && impulses[0U].reset &&
        NearlyEqual(
            impulses[0U].globalLinearImpulse,
            glm::vec3(-0.5f, -1.0f, -1.5f)
        ),
        "Impulse Morph incorrectly clamped a negative effective weight"
    );
    state.Reset();
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.empty(),
        "Zero-weight Impulse Morph produced a physics request"
    );

    MorphDefinition blockedLeaf;
    blockedLeaf.name = "blockedLeaf";
    MorphDefinition earlyFlip;
    earlyFlip.name = "earlyFlip";
    earlyFlip.kind = MorphKind::Flip;
    earlyFlip.flipMembers = {FlipMorphMember{0U, 0.4f}};
    MorphDefinition lateController;
    lateController.name = "lateController";
    lateController.kind = MorphKind::Flip;
    lateController.flipMembers = {FlipMorphMember{1U, 1.0f}};
    MorphSet blockedOrderSet({
        std::move(blockedLeaf),
        std::move(earlyFlip),
        std::move(lateController)
    });
    MorphState blockedOrderState(blockedOrderSet);
    blockedOrderState.SetWeight(2U, 1.0f);
    Require(
        NearlyEqual(blockedOrderState.EffectiveWeights()[0U], 0.0f),
        "A later Flip Morph incorrectly re-evaluated an earlier Flip Morph"
    );

    MorphDefinition forwardedLeaf;
    forwardedLeaf.name = "forwardedLeaf";
    MorphDefinition earlyController;
    earlyController.name = "earlyController";
    earlyController.kind = MorphKind::Flip;
    earlyController.flipMembers = {FlipMorphMember{2U, 1.0f}};
    MorphDefinition lateFlip;
    lateFlip.name = "lateFlip";
    lateFlip.kind = MorphKind::Flip;
    lateFlip.flipMembers = {FlipMorphMember{0U, 0.4f}};
    MorphSet forwardedOrderSet({
        std::move(forwardedLeaf),
        std::move(earlyController),
        std::move(lateFlip)
    });
    MorphState forwardedOrderState(forwardedOrderSet);
    forwardedOrderState.SetWeight(1U, 1.0f);
    Require(
        NearlyEqual(forwardedOrderState.EffectiveWeights()[0U], 0.4f),
        "An earlier Flip Morph did not feed a later Flip Morph in index order"
    );

    MorphDefinition selfFlip;
    selfFlip.name = "selfFlip";
    selfFlip.kind = MorphKind::Flip;
    selfFlip.flipMembers = {FlipMorphMember{0U, 0.5f}};
    MorphSet selfFlipSet({std::move(selfFlip)});
    MorphState selfFlipState(selfFlipSet);
    selfFlipState.SetWeight(0U, 1.0f);
    Require(
        NearlyEqual(selfFlipState.EffectiveWeights()[0U], 0.0f),
        "Self-referencing Flip Morph was not handled as a control-only morph"
    );
}

void TestPmx21FlipImpulseImporter()
{
    const std::filesystem::path modelPath =
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "pmx21_flip_impulse.pmx";
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.mmdPhysics.has_value() &&
        imported.mmdPhysics->RigidBodyCount() == 1U &&
        imported.morphs.size() == 7U,
        "PMX 2.1 fixture changed its rigid-body or morph count"
    );

    const MorphSet morphSet(imported.morphs);
    const std::optional<MorphIndex> vertex = morphSet.FindMorph("vertex");
    const std::optional<MorphIndex> material =
        morphSet.FindMorph("materialMorph");
    const std::optional<MorphIndex> group = morphSet.FindMorph("group");
    const std::optional<MorphIndex> flip = morphSet.FindMorph("flip");
    const std::optional<MorphIndex> impulse = morphSet.FindMorph("impulse");
    Require(
        vertex.has_value() && material.has_value() && group.has_value() &&
        flip.has_value() && impulse.has_value(),
        "PMX 2.1 fixture lost a named morph"
    );
    Require(
        morphSet.DefinitionAt(*flip).kind == MorphKind::Flip &&
        morphSet.DefinitionAt(*impulse).kind == MorphKind::Impulse &&
        morphSet.DefinitionAt(*flip).flipMembers.size() == 2U &&
        morphSet.DefinitionAt(*impulse).impulseOffsets.size() == 3U,
        "PMX 2.1 Flip/Impulse metadata was imported incorrectly"
    );

    const ImpulseMorphOffset& global =
        morphSet.DefinitionAt(*impulse).impulseOffsets[0U];
    const ImpulseMorphOffset& local =
        morphSet.DefinitionAt(*impulse).impulseOffsets[1U];
    Require(
        !global.local && local.local &&
        global.rigidBodyIndex == 0U && local.rigidBodyIndex == 0U &&
        NearlyEqual(global.velocity, glm::vec3(1.0f, 2.0f, -3.0f)) &&
        NearlyEqual(global.torque, glm::vec3(-4.0f, -5.0f, 6.0f)) &&
        NearlyEqual(local.velocity, glm::vec3(2.0f, 0.0f, 2.0f)) &&
        NearlyEqual(local.torque, glm::vec3(-1.0f, 1.0f, 3.0f)),
        "PMX Impulse Morph coordinate conversion is incorrect"
    );

    MorphState state(morphSet);
    state.SetWeight(*group, 0.67f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[*material], 1.42f) &&
        NearlyEqual(effective[*impulse], 0.335f) &&
        NearlyEqual(effective[*flip], 0.0f) &&
        NearlyEqual(effective[*group], 0.0f),
        "Imported Group Morph did not compose PMX 2.1 controls correctly"
    );

    std::vector<MmdRigidBodyImpulse> impulses;
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.size() == 1U && impulses[0U].reset &&
        NearlyEqual(
            impulses[0U].globalLinearImpulse,
            glm::vec3(0.335f, 0.67f, -1.005f)
        ) &&
        NearlyEqual(
            impulses[0U].localLinearImpulse,
            glm::vec3(0.67f, 0.0f, 0.67f)
        ),
        "Imported PMX Impulse Morph did not produce runtime impulses"
    );
}

void TestPmxPhysicsImporter()
{
    const std::filesystem::path modelPath =
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) / "pmx_physics.pmx";
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.skeleton.has_value() && imported.mmdPhysics.has_value(),
        "PMX Physics 1 fixture lost its Skeleton or physics metadata"
    );

    const MmdPhysicsAsset& physics = *imported.mmdPhysics;
    Require(
        physics.RigidBodyCount() == 3U && physics.JointCount() == 6U,
        "PMX Physics 1 fixture changed its rigid-body or joint count"
    );
    const auto sphereIndex = physics.FindRigidBody("followSphere");
    const auto boxIndex = physics.FindRigidBody("dynamicBox");
    const auto capsuleIndex = physics.FindRigidBody("mergeCapsule");
    Require(
        sphereIndex == std::optional<RigidBodyIndex>(0U) &&
        boxIndex == std::optional<RigidBodyIndex>(1U) &&
        capsuleIndex == std::optional<RigidBodyIndex>(2U),
        "PMX rigid-body name lookup changed"
    );

    const MmdRigidBodyDefinition& sphere = physics.RigidBodyAt(*sphereIndex);
    const MmdRigidBodyDefinition& box = physics.RigidBodyAt(*boxIndex);
    const MmdRigidBodyDefinition& capsule = physics.RigidBodyAt(*capsuleIndex);
    const std::optional<BoneIndex> root = imported.skeleton->FindBone("root");
    Require(
        root.has_value() &&
        imported.skeleton->BoneAt(*root).deformAfterPhysics,
        "PMX deform-after-physics bone flag was not preserved"
    );
    Require(
        root.has_value() && sphere.bone == *root &&
        capsule.bone == *root && box.bone == InvalidBoneIndex,
        "PMX rigid-body bone indices were not mapped to the runtime Skeleton"
    );
    Require(
        sphere.shape == MmdRigidBodyShape::Sphere &&
        box.shape == MmdRigidBodyShape::Box &&
        capsule.shape == MmdRigidBodyShape::Capsule &&
        sphere.mode == MmdRigidBodyMode::FollowBone &&
        box.mode == MmdRigidBodyMode::Physics &&
        capsule.mode == MmdRigidBodyMode::PhysicsWithBone,
        "PMX rigid-body shape or mode mapping changed"
    );
    Require(
        sphere.collisionGroup == 0U && sphere.nonCollisionMask == 0x0002U &&
        box.collisionGroup == 1U && box.nonCollisionMask == 0x0001U &&
        capsule.collisionGroup == 15U &&
        capsule.nonCollisionMask == 0x00F0U,
        "PMX rigid-body collision filters changed"
    );
    Require(
        NearlyEqual(sphere.size, glm::vec3(1.0f, 2.0f, 3.0f)) &&
        NearlyEqual(sphere.position, glm::vec3(1.0f, 2.0f, -3.0f)) &&
        NearlyEqual(box.position, glm::vec3(-1.0f, 4.0f, 2.0f)) &&
        NearlyEqual(capsule.position, glm::vec3(0.0f, 3.0f, -1.0f)),
        "PMX rigid-body size or coordinate conversion changed"
    );
    Require(
        NearlySameRotation(
            sphere.rotation,
            glm::angleAxis(-0.25f, glm::vec3(1.0f, 0.0f, 0.0f))
        ) &&
        NearlySameRotation(
            box.rotation,
            glm::angleAxis(-0.5f, glm::vec3(0.0f, 1.0f, 0.0f))
        ) &&
        NearlySameRotation(
            capsule.rotation,
            glm::angleAxis(0.75f, glm::vec3(0.0f, 0.0f, 1.0f))
        ),
        "PMX rigid-body rotation conversion changed"
    );
    Require(
        NearlyEqual(sphere.modelBindTransform[3],
            glm::vec4(1.0f, 2.0f, -3.0f, 1.0f)) &&
        MatrixIdentityDeviation(sphere.boneToBody * sphere.bodyToBone) <
            Epsilon,
        "PMX rigid-body bind offsets are inconsistent"
    );
    Require(
        NearlyEqual(box.mass, 2.0f) &&
        NearlyEqual(box.linearDamping, 0.25f) &&
        NearlyEqual(box.angularDamping, 0.35f) &&
        NearlyEqual(box.restitution, 0.45f) &&
        NearlyEqual(box.friction, 0.55f),
        "PMX rigid-body physical parameters changed"
    );

    for (std::size_t index = 0; index < physics.JointCount(); ++index)
    {
        Require(
            physics.JointAt(index).type ==
                static_cast<MmdJointType>(index),
            "PMX 2.1 joint type mapping changed"
        );
    }
    const MmdJointDefinition& joint = physics.JointAt(0U);
    Require(
        joint.bodyA == 0U && joint.bodyB == 1U &&
        NearlyEqual(joint.position, glm::vec3(1.0f, 2.0f, 3.0f)) &&
        NearlyEqual(joint.linearLower, glm::vec3(-1.0f, -2.0f, -6.0f)) &&
        NearlyEqual(joint.linearUpper, glm::vec3(4.0f, 5.0f, 3.0f)) &&
        NearlyEqual(joint.angularLower, glm::vec3(-0.4f, -0.5f, -0.3f)) &&
        NearlyEqual(joint.angularUpper, glm::vec3(0.1f, 0.2f, 0.6f)) &&
        NearlyEqual(joint.linearSpring, glm::vec3(1.0f, 2.0f, 3.0f)) &&
        NearlyEqual(joint.angularSpring, glm::vec3(4.0f, 5.0f, 6.0f)),
        "PMX joint limits, springs, or coordinate conversion changed"
    );
}

void TestPmxPhysicsImporterValidation()
{
    const auto rejected = [](const char* fileName)
    {
        try
        {
            const std::filesystem::path path =
                std::filesystem::path(WISTERIA_TEST_DATA_DIR) / fileName;
            (void)ModelImporter().Import(path);
            return false;
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
    };

    Require(
        rejected("pmx_physics_invalid_group.pmx"),
        "PMX importer accepted collision group 16"
    );
    Require(
        rejected("pmx_physics_invalid_joint.pmx"),
        "PMX importer accepted an out-of-range joint rigid body"
    );
    Require(
        rejected("pmx_physics_softbody.pmx"),
        "PMX importer silently accepted unsupported Soft Body data"
    );
}

void TestMmdPhysicsAssetValidation()
{
    MmdRigidBodyDefinition bodyA;
    bodyA.name = "bodyA";
    MmdRigidBodyDefinition bodyB;
    bodyB.name = "bodyB";
    MmdJointDefinition joint;
    joint.name = "joint";
    joint.bodyA = 0U;
    joint.bodyB = 1U;
    MmdPhysicsAsset physics({bodyA, bodyB}, {joint});
    Require(
        physics.RigidBodyCount() == 2U && physics.JointCount() == 1U &&
        physics.FindRigidBody("bodyB") ==
            std::optional<RigidBodyIndex>(1U),
        "MmdPhysicsAsset did not preserve valid definitions"
    );

    bool invalidGroupRejected = false;
    try
    {
        MmdRigidBodyDefinition invalid;
        invalid.name = "invalid";
        invalid.collisionGroup = 16U;
        MmdPhysicsAsset rejected({invalid}, {});
        (void)rejected;
    }
    catch (const std::invalid_argument&)
    {
        invalidGroupRejected = true;
    }
    Require(
        invalidGroupRejected,
        "MmdPhysicsAsset accepted an invalid collision group"
    );

    bool invalidJointRejected = false;
    try
    {
        MmdJointDefinition invalid;
        invalid.name = "invalidJoint";
        invalid.bodyA = InvalidRigidBodyIndex;
        invalid.bodyB = InvalidRigidBodyIndex;
        MmdPhysicsAsset rejected({bodyA}, {invalid});
        (void)rejected;
    }
    catch (const std::invalid_argument&)
    {
        invalidJointRejected = true;
    }
    Require(
        invalidJointRejected,
        "MmdPhysicsAsset accepted a joint without a rigid-body endpoint"
    );
}

void TestExtendedMmdMorphRuntime()
{
    Skeleton skeleton({Bone{"root"}});

    MorphDefinition boneMorph;
    boneMorph.name = "bone";
    boneMorph.kind = MorphKind::Bone;
    boneMorph.boneOffsets.push_back(BoneMorphOffset{
        0U,
        glm::vec3(2.0f, 0.0f, 0.0f),
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))
    });

    MorphDefinition uvMorph;
    uvMorph.name = "uv";
    uvMorph.kind = MorphKind::Uv;

    MaterialMorphOffset multiplyOffset;
    multiplyOffset.materialIndex = AllMaterialMorphTargets;
    multiplyOffset.operation = MaterialMorphOperation::Multiply;
    multiplyOffset.diffuse = glm::vec4(0.5f, 1.0f, 1.0f, 1.0f);
    multiplyOffset.specular = glm::vec3(1.0f);
    multiplyOffset.shininess = 1.0f;
    multiplyOffset.ambient = glm::vec3(1.0f);
    multiplyOffset.edgeColor = glm::vec4(1.0f);
    multiplyOffset.edgeSize = 2.0f;
    multiplyOffset.textureFactor = glm::vec4(0.5f, 1.0f, 1.0f, 1.0f);
    multiplyOffset.sphereTextureFactor = glm::vec4(1.0f);
    multiplyOffset.toonTextureFactor = glm::vec4(1.0f);
    MorphDefinition multiplyMaterialMorph;
    multiplyMaterialMorph.name = "materialMultiply";
    multiplyMaterialMorph.kind = MorphKind::Material;
    multiplyMaterialMorph.materialOffsets.push_back(multiplyOffset);

    MaterialMorphOffset addOffset;
    addOffset.materialIndex = 3U;
    addOffset.operation = MaterialMorphOperation::Add;
    addOffset.diffuse = glm::vec4(0.2f, 0.0f, 0.0f, 0.0f);
    addOffset.edgeSize = 0.4f;
    addOffset.textureFactor = glm::vec4(0.2f, 0.0f, 0.0f, 0.0f);
    MorphDefinition addMaterialMorph;
    addMaterialMorph.name = "materialAdd";
    addMaterialMorph.kind = MorphKind::Material;
    addMaterialMorph.materialOffsets.push_back(addOffset);

    MorphDefinition combinedMorph;
    combinedMorph.name = "combined";
    combinedMorph.kind = MorphKind::Group;
    combinedMorph.groupMembers = {
        GroupMorphMember{0U, 0.5f},
        GroupMorphMember{1U, 2.0f},
        GroupMorphMember{2U, 1.0f},
        GroupMorphMember{3U, 0.5f}
    };

    MorphSet morphSet({
        std::move(boneMorph),
        std::move(uvMorph),
        std::move(multiplyMaterialMorph),
        std::move(addMaterialMorph),
        std::move(combinedMorph)
    });
    Require(
        morphSet.HasKind(MorphKind::Bone) &&
        morphSet.HasKind(MorphKind::Uv) &&
        morphSet.HasKind(MorphKind::Material),
        "MorphSet did not retain extended MMD morph kinds"
    );

    MorphState state(morphSet);
    state.SetWeight("combined", 1.0f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.5f) &&
        NearlyEqual(effective[1U], 2.0f) &&
        NearlyEqual(effective[2U], 1.0f) &&
        NearlyEqual(effective[3U], 0.5f) &&
        NearlyEqual(effective[4U], 0.0f),
        "Group Morph did not expand Bone, UV, and Material leaves"
    );

    PoseBuffer poseBuffer(skeleton);
    morphSet.ApplyBoneMorphs(effective, poseBuffer);
    const BoneTransform& boneTransform = poseBuffer.TransformAt(0U);
    Require(
        NearlyEqual(boneTransform.translation, glm::vec3(1.0f, 0.0f, 0.0f)) &&
        NearlySameRotation(
            boneTransform.rotation,
            glm::angleAxis(
                glm::radians(45.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            )
        ),
        "Bone Morph did not apply weighted translation and rotation"
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
                1U,
                {},
                {
                    UvMorphOffset{
                        1U,
                        0U,
                        glm::vec4(0.25f, 0.5f, 0.0f, 0.0f)
                    },
                    UvMorphOffset{
                        2U,
                        1U,
                        glm::vec4(0.1f, 0.2f, 0.3f, 0.4f)
                    }
                }
            }
        }
    );
    std::vector<MorphVertexDelta> deltas;
    Require(
        mesh.CalculateMorphDeltas(effective, deltas) &&
        deltas.size() == 3U &&
        NearlyEqual(
            deltas[1U].uv[0U],
            glm::vec4(0.5f, 1.0f, 0.0f, 0.0f)
        ) &&
        NearlyEqual(
            deltas[2U].uv[1U],
            glm::vec4(0.2f, 0.4f, 0.6f, 0.8f)
        ),
        "UV Morph did not accumulate the correct channel deltas"
    );

    MaterialMorphValues targetValues;
    targetValues.edgeSize = 1.0f;
    morphSet.ApplyMaterialMorphs(3U, effective, targetValues);
    Require(
        NearlyEqual(targetValues.diffuse.x, 0.6f) &&
        NearlyEqual(targetValues.edgeSize, 2.2f) &&
        NearlyEqual(targetValues.textureFactor.x, 0.6f),
        "Material Morph multiply/add composition is incorrect"
    );

    MaterialMorphValues otherValues;
    otherValues.edgeSize = 1.0f;
    morphSet.ApplyMaterialMorphs(4U, effective, otherValues);
    Require(
        NearlyEqual(otherValues.diffuse.x, 0.5f) &&
        NearlyEqual(otherValues.edgeSize, 2.0f) &&
        NearlyEqual(otherValues.textureFactor.x, 0.5f),
        "All-material Morph did not apply independently of targeted offsets"
    );

    Pose animatedPose(skeleton);
    MorphState animatedState(morphSet);
    Animator animator(animatedPose, &animatedState);
    animatedState.SetWeight("combined", 1.0f);
    animator.Update(0.0f);
    const BoneTransform animatedTransform = BoneTransform::FromMatrix(
        animatedPose.LocalMatrix(0U)
    );
    Require(
        NearlyEqual(
            animatedTransform.translation,
            glm::vec3(1.0f, 0.0f, 0.0f)
        ) &&
        NearlySameRotation(
            animatedTransform.rotation,
            glm::angleAxis(
                glm::radians(45.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            )
        ),
        "Animator did not re-evaluate a manually changed Bone Morph"
    );

    const AnimationClip heldClip(
        "held",
        1.0f,
        {
            AnimationTrack(
                0U,
                {VectorKeyframe{0.0f, glm::vec3(3.0f, 0.0f, 0.0f)}}
            )
        }
    );
    animatedState.Reset();
    animator.Play(heldClip);
    animator.Stop(false);
    animatedState.SetWeight("combined", 1.0f);
    animator.Update(0.0f);
    Require(
        NearlyEqual(
            BoneTransform::FromMatrix(animatedPose.LocalMatrix(0U))
                .translation,
            glm::vec3(4.0f, 0.0f, 0.0f)
        ),
        "Manual Bone Morph discarded a stopped animation pose"
    );

    animator.Stop(true);
    animatedState.SetWeight("combined", 1.0f);
    animator.Update(0.0f);
    Require(
        NearlyEqual(
            BoneTransform::FromMatrix(animatedPose.LocalMatrix(0U))
                .translation,
            glm::vec3(1.0f, 0.0f, 0.0f)
        ),
        "Animator Stop(true) retained a stale animation pose baseline"
    );
}



struct PhysicsLifecycleCounters
{
    int prepareCount = 0;
    int substepCount = 0;
    int finishCount = 0;
    int resetCount = 0;
    float lastDeltaTime = 0.0f;
    float lastSubstepAlpha = 0.0f;
    float lastFixedTimeStep = 0.0f;
    std::vector<float> substepAlphas;
};

class CountingPhysicsInstance final : public PhysicsInstance
{
public:
    explicit CountingPhysicsInstance(PhysicsLifecycleCounters& counters)
        : counters(&counters)
    {
    }

    void PrepareSimulation(float deltaTime) override
    {
        ++this->counters->prepareCount;
        this->counters->lastDeltaTime = deltaTime;
    }

    void PrepareSimulationSubstep(
        float alpha,
        float fixedTimeStep
    ) override
    {
        ++this->counters->substepCount;
        this->counters->lastSubstepAlpha = alpha;
        this->counters->lastFixedTimeStep = fixedTimeStep;
        this->counters->substepAlphas.push_back(alpha);
    }

    void FinishSimulation() override
    {
        ++this->counters->finishCount;
    }

    void ResetSimulation() override
    {
        ++this->counters->resetCount;
    }

private:
    PhysicsLifecycleCounters* counters = nullptr;
};

void TestGenericPhysicsInstanceLifecycle()
{
    Entity entity;
    PhysicsLifecycleCounters counters;
    entity.SetPhysicsInstance(
        std::make_unique<CountingPhysicsInstance>(counters)
    );
    Require(
        entity.HasPhysicsInstance() &&
        entity.TryGetPhysicsInstance() != nullptr &&
        &entity.GetPhysicsInstance() == entity.TryGetPhysicsInstance() &&
        !entity.HasMmdPhysics(),
        "Entity generic physics slot still assumes an MMD runtime"
    );

    entity.PrePhysicsUpdate(0.25f);
    entity.PreparePhysicsSubstep(0.5f, 1.0f / 60.0f);
    entity.PostPhysicsUpdate();
    entity.ResetPhysicsToCurrentPose();
    Require(
        counters.prepareCount == 1 &&
        counters.substepCount == 1 &&
        counters.finishCount == 1 &&
        counters.resetCount == 1 &&
        NearlyEqual(counters.lastDeltaTime, 0.25f) &&
        NearlyEqual(counters.lastSubstepAlpha, 0.5f) &&
        NearlyEqual(counters.lastFixedTimeStep, 1.0f / 60.0f),
        "Entity did not route simulation lifecycle through PhysicsInstance"
    );

    bool duplicateRejected = false;
    try
    {
        entity.SetPhysicsInstance(
            std::make_unique<CountingPhysicsInstance>(counters)
        );
    }
    catch (const std::logic_error&)
    {
        duplicateRejected = true;
    }
    Require(
        duplicateRejected,
        "Entity accepted a second physics runtime without explicit removal"
    );
}

void TestMmdFullBodyDemoAnimation()
{
    const std::array<std::string_view, 17U> boneNames{
        "全ての親", "センター", "グルーブ", "下半身", "上半身",
        "上半身2", "首", "頭", "左肩", "右肩", "左腕", "右腕",
        "左ひじ", "右ひじ", "左手首", "左足ＩＫ", "右足ＩＫ"
    };
    std::vector<Bone> bones;
    bones.reserve(boneNames.size());
    for (std::size_t index = 0; index < boneNames.size(); ++index)
    {
        Bone bone;
        bone.name = std::string(boneNames[index]);
        bone.parentIndex = index == 0U
            ? InvalidBoneIndex
            : static_cast<BoneIndex>(index - 1U);
        bone.bindLocalMatrix = glm::mat4(1.0f);
        bone.inverseBindMatrix = glm::mat4(1.0f);
        bones.push_back(std::move(bone));
    }

    ModelAsset model("fullBodyDemoRig");
    model.SetSkeleton(Skeleton(std::move(bones)));
    model.SetMorphs({
        MorphDefinition{"まばたき", MorphCategory::Eye},
        MorphDefinition{"笑い", MorphCategory::Mouth}
    });

    const AnimationClip& clip = CreateMmdFullBodyDemoAnimation(model);
    Require(
        &CreateMmdFullBodyDemoAnimation(model) == &clip &&
        clip.Name() == "demoFullBodyAction" &&
        NearlyEqual(clip.Duration(), 8.0f) &&
        clip.TrackCount() >= 15U &&
        clip.MorphWeightTrackCount() == 2U,
        "Full-body demo clip is incomplete or not idempotent"
    );

    const Skeleton& skeleton = model.GetSkeleton();
    PoseBuffer sampled(skeleton);
    clip.Sample(2.0f, sampled);
    const BoneIndex center = *skeleton.FindBone("センター");
    const BoneIndex head = *skeleton.FindBone("頭");
    const BoneIndex leftArm = *skeleton.FindBone("左腕");
    const BoneIndex leftFootIk = *skeleton.FindBone("左足ＩＫ");
    Require(
        glm::length(sampled.TransformAt(center).translation) > 0.1f &&
        !NearlySameRotation(
            sampled.TransformAt(head).rotation,
            BoneTransform::FromMatrix(
                skeleton.BoneAt(head).bindLocalMatrix
            ).rotation
        ) &&
        !NearlySameRotation(
            sampled.TransformAt(leftArm).rotation,
            BoneTransform::FromMatrix(
                skeleton.BoneAt(leftArm).bindLocalMatrix
            ).rotation
        ) &&
        glm::length(sampled.TransformAt(leftFootIk).translation) > 0.1f,
        "Full-body demo clip still moves only a local head region"
    );

    std::vector<float> morphWeights(model.GetMorphSet().MorphCount(), 0.0f);
    clip.SampleMorphWeights(3.0f, morphWeights);
    Require(
        morphWeights[1U] > 0.3f,
        "Full-body demo clip did not include facial Morph animation"
    );
}

void TestMmdFullBodyDemoIntegrationWhenAvailable()
{
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("fullBodyDemoIntegration", modelPath);
    const AnimationClip& clip = CreateMmdFullBodyDemoAnimation(model);
    Scene scene;
    Entity& entity = scene.InstantiateModel(model);
    entity.GetAnimator().Play(clip, true);
    const BoneIndex center = *model.GetSkeleton().FindBone("センター");
    const glm::mat4 startCenter = entity.GetPose().GlobalMatrix(center);
    std::vector<glm::vec3> initialBodyPositions;
    initialBodyPositions.reserve(entity.GetMmdPhysics().RigidBodyCount());
    for (RigidBodyIndex index = 0U;
         index < entity.GetMmdPhysics().RigidBodyCount();
         ++index)
    {
        initialBodyPositions.push_back(
            entity.GetMmdPhysics().BodyStateAt(index).position
        );
    }

    for (int frame = 0; frame < 180; ++frame)
        scene.Update(1.0f / 60.0f);

    std::size_t movedBodies = 0U;
    for (RigidBodyIndex index = 0U;
         index < entity.GetMmdPhysics().RigidBodyCount();
         ++index)
    {
        const PhysicsBodyState state =
            entity.GetMmdPhysics().BodyStateAt(index);
        Require(
            std::isfinite(state.position.x) &&
            std::isfinite(state.position.y) &&
            std::isfinite(state.position.z) &&
            std::isfinite(state.rotation.w),
            "Full-body MMD demo produced non-finite Bullet state"
        );
        if (glm::length(
                state.position - initialBodyPositions[index]
            ) > 0.01f)
        {
            ++movedBodies;
        }
    }
    Require(
        MatrixIdentityDeviation(
            glm::inverse(startCenter) * entity.GetPose().GlobalMatrix(center)
        ) > 0.02f && movedBodies > 20U,
        "Full-body action did not drive animation and rigid-body physics together"
    );
}

void TestMorphLabDemoAsset()
{
    ResourceManager resources;
    ModelAsset& model = CreateMorphLabModel(resources);
    Require(
        &CreateMorphLabModel(resources) == &model,
        "Morph Lab model creation is not idempotent"
    );
    Require(
        model.PartCount() == 1U &&
        model.HasSkeleton() &&
        model.GetSkeleton().BoneCount() == 2U &&
        model.HasMorphs() &&
        model.GetMorphSet().MorphCount() == 7U &&
        model.MmdRigidBodyCount() == 2U &&
        model.GetMmdPhysics().JointCount() == 1U,
        "Morph Lab model structure is incomplete"
    );
    for (MorphKind kind : {
             MorphKind::Vertex,
             MorphKind::Bone,
             MorphKind::Uv,
             MorphKind::Material,
             MorphKind::Group,
             MorphKind::Flip,
             MorphKind::Impulse})
    {
        Require(
            model.GetMorphSet().HasKind(kind),
            "Morph Lab model is missing a supported Morph kind"
        );
    }

    Scene scene;
    Entity& reference = scene.InstantiateModel(
        model,
        Transform{},
        ModelInstantiationOptions{.enablePhysics = false}
    );
    Entity& active = scene.InstantiateModel(model);
    Require(
        !reference.HasMmdPhysics() && active.HasMmdPhysics() &&
        scene.Physics().BodyCount() == 2U &&
        scene.Physics().ConstraintCount() == 1U,
        "Morph Lab physics opt-out or active runtime is incorrect"
    );
    MorphState& state = active.GetMorphState();
    const MorphSet& morphSet = model.GetMorphSet();

    state.SetWeight(0U, 1.0f);
    std::vector<MorphVertexDelta> deltas;
    Require(
        active.GetMesh().CalculateMorphDeltas(
            state.EffectiveWeights(),
            deltas
        ) &&
        !deltas.empty() &&
        glm::length(deltas[7U].position) > Epsilon,
        "Morph Lab Vertex Morph has no renderable displacement"
    );

    state.Reset();
    state.SetWeight(2U, 1.0f);
    Require(
        active.GetMesh().CalculateMorphDeltas(
            state.EffectiveWeights(),
            deltas
        ) &&
        glm::length(glm::vec2(deltas[0U].uv[0U])) > Epsilon,
        "Morph Lab UV Morph has no renderable UV displacement"
    );

    state.Reset();
    state.SetWeight(1U, 1.0f);
    active.Update(0.0f);
    Require(
        MatrixIdentityDeviation(active.GetPose().LocalMatrix(1U)) > 0.01f,
        "Morph Lab Bone Morph did not update the skinned pose"
    );

    state.Reset();
    state.SetWeight(3U, 1.0f);
    MaterialMorphValues materialValues;
    materialValues.diffuse = active.GetMaterial().BaseColorFactor();
    materialValues.specular = active.GetMaterial().SpecularColor();
    materialValues.shininess = active.GetMaterial().Shininess();
    materialValues.ambient = active.GetMaterial().AmbientColor();
    materialValues.edgeColor = active.GetMaterial().EdgeColor();
    materialValues.edgeSize = active.GetMaterial().EdgeSize();
    morphSet.ApplyMaterialMorphs(
        0U,
        state.EffectiveWeights(),
        materialValues
    );
    Require(
        !NearlyEqual(
            materialValues.diffuse,
            active.GetMaterial().BaseColorFactor()
        ) && materialValues.edgeSize > active.GetMaterial().EdgeSize(),
        "Morph Lab Material Morph has no visible material effect"
    );

    state.Reset();
    state.SetWeight(4U, 1.0f);
    const std::span<const float> groupWeights = state.EffectiveWeights();
    Require(
        groupWeights[0U] > 0.0f &&
        groupWeights[1U] > 0.0f &&
        groupWeights[2U] > 0.0f &&
        groupWeights[3U] > 0.0f,
        "Morph Lab Group Morph does not combine all visible channels"
    );

    state.Reset();
    state.SetWeight(5U, 0.30f);
    Require(
        NearlyEqual(state.EffectiveWeights()[0U], 1.0f),
        "Morph Lab Flip Morph did not select its Vertex member"
    );
    state.SetWeight(5U, 0.55f);
    Require(
        NearlyEqual(state.EffectiveWeights()[3U], 1.0f),
        "Morph Lab Flip Morph did not select its Material member"
    );
    state.SetWeight(5U, 0.80f);
    Require(
        state.EffectiveWeights()[0U] > 0.0f &&
        state.EffectiveWeights()[1U] > 0.0f &&
        state.EffectiveWeights()[2U] > 0.0f &&
        state.EffectiveWeights()[3U] > 0.0f,
        "Morph Lab Flip Morph did not select its Group member"
    );

    state.Reset();
    state.SetWeight(6U, 1.0f);
    std::vector<MmdRigidBodyImpulse> impulses;
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.size() == 1U &&
        impulses[0U].rigidBodyIndex == 1U &&
        NearlyEqual(
            impulses[0U].globalLinearImpulse,
            glm::vec3(1.5f, 2.0f, 0.0f)
        ),
        "Morph Lab Impulse Morph does not produce its physics request"
    );
    Require(
        reference.GetMorphState().EffectiveWeights()[0U] == 0.0f,
        "Morph Lab reference and active instances share Morph state"
    );

    state.Reset();
    active.Update(0.0f);
    active.ResetPhysicsToCurrentPose();
    const glm::mat4 physicsStart = active.GetPose().GlobalMatrix(1U);
    active.GetMmdPhysics().ApplyTorqueImpulse(
        1U,
        glm::vec3(0.0f, 0.0f, 0.75f)
    );
    for (int frame = 0; frame < 45; ++frame)
        scene.Update(1.0f / 60.0f);
    Require(
        MatrixIdentityDeviation(
            glm::inverse(physicsStart) * active.GetPose().GlobalMatrix(1U)
        ) > 0.02f,
        "Morph Lab dynamic tip did not produce a visible Bullet motion"
    );
}

void TestRenderPartAndModelAsset()
{
    Mesh mesh(DefaultModelData{});
    Material material(MaterialData{});
    const glm::mat4 localTransform = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 2.0f, 3.0f)
    );

    ModelAsset model("testModel");
    std::vector<MmdRigidBodyDefinition> bodies(3U);
    for (std::size_t index = 0; index < bodies.size(); ++index)
        bodies[index].name = "body" + std::to_string(index);
    model.SetMmdPhysics(MmdPhysicsAsset(std::move(bodies), {}));
    model.AddPart(mesh, material, localTransform);

    Require(model.Name() == "testModel", "ModelAsset name was not preserved");
    Require(
        model.HasMmdPhysics() &&
        model.TryGetMmdPhysics() == &model.GetMmdPhysics() &&
        model.MmdRigidBodyCount() == 3U &&
        model.GetMmdPhysics().JointCount() == 0U,
        "ModelAsset did not preserve PMX physics metadata"
    );
    Require(model.PartCount() == 1, "ModelAsset did not store its part");
    Require(&model.Parts()[0].GetMesh() == &mesh, "ModelAsset mesh reference changed");
    Require(
        &model.Parts()[0].GetMaterial() == &material,
        "ModelAsset material reference changed"
    );
    Require(
        NearlyEqual(model.Parts()[0].LocalTransform()[3].x, 1.0f) &&
        NearlyEqual(model.Parts()[0].LocalTransform()[3].y, 2.0f) &&
        NearlyEqual(model.Parts()[0].LocalTransform()[3].z, 3.0f),
        "RenderPart local transform changed"
    );
}

void TestBuiltInCubeTangents()
{
    constexpr std::size_t VertexCount = 24;
    constexpr std::size_t VertexStride = 15;
    Require(cubeData.layout.size() == 5, "Cube tangent layout is missing");
    Require(
        cubeData.vertices.size() == VertexCount * VertexStride,
        "Cube vertex stride does not match its layout"
    );

    for (std::size_t vertex = 0; vertex < VertexCount; ++vertex)
    {
        const std::size_t offset = vertex * VertexStride;
        const glm::vec3 normal(
            cubeData.vertices[offset + 8],
            cubeData.vertices[offset + 9],
            cubeData.vertices[offset + 10]
        );
        const glm::vec3 tangent(
            cubeData.vertices[offset + 11],
            cubeData.vertices[offset + 12],
            cubeData.vertices[offset + 13]
        );
        Require(NearlyEqual(glm::length(tangent), 1.0f), "Cube tangent is not normalized");
        Require(NearlyEqual(glm::dot(normal, tangent), 0.0f), "Cube tangent is not orthogonal");
        Require(
            NearlyEqual(std::abs(cubeData.vertices[offset + 14]), 1.0f),
            "Cube tangent handedness is invalid"
        );
    }
}

void TestMeshBoundsCenter()
{
    DefaultModelData data{
        {
            -4.0f, 2.0f, -1.0f,
             2.0f, 8.0f,  5.0f,
             0.0f, 3.0f,  1.0f
        },
        {0U, 1U, 2U},
        {{"position", 3, FLOAT}}
    };
    const Mesh mesh(std::move(data));
    const glm::vec3 center = mesh.LocalBoundsCenter();
    Require(
        NearlyEqual(center.x, -1.0f) &&
        NearlyEqual(center.y, 5.0f) &&
        NearlyEqual(center.z, 2.0f),
        "Mesh local bounds center is incorrect"
    );
}

void TestModelInstantiation()
{
    Mesh firstMesh(DefaultModelData{});
    Mesh secondMesh(DefaultModelData{});
    Material firstMaterial(MaterialData{});
    Material secondMaterial(MaterialData{});

    ModelAsset model("multiPartModel");
    model.AddPart(firstMesh, firstMaterial);
    model.AddPart(
        secondMesh,
        secondMaterial,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f))
    );

    Scene scene;
    Entity& instance = scene.InstantiateModel(
        model,
        Transform(glm::vec3(5.0f, 0.0f, 0.0f))
    );

    Require(scene.EntityCount() == 1, "Scene did not create one model Entity");
    Require(instance.RenderPartCount() == 2, "Entity did not receive all model parts");
    Require(
        &instance.RenderParts()[1].GetMesh() == &secondMesh,
        "Second model part references the wrong mesh"
    );
    Require(
        NearlyEqual(instance.GetTransform().Position().x, 5.0f),
        "Model instance root transform changed"
    );
}

void TestFrameRateIndependentBehaviours()
{
    Mesh mesh(DefaultModelData{});
    Material material(MaterialData{});
    Entity entity(mesh, material);

    entity.AddBehaviour<MoveBehaviour>(glm::vec3(2.0f, 0.0f, 0.0f));
    entity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f, 90.0f, 0.0f));
    entity.AddBehaviour<ScaleBehaviour>(glm::vec3(2.0f, 1.0f, 0.5f));

    entity.UpdateBehaviours(0.5f);
    entity.UpdateBehaviours(0.5f);

    Require(NearlyEqual(entity.GetTransform().Position().x, 2.0f), "MoveBehaviour is frame dependent");
    Require(NearlyEqual(entity.GetTransform().Rotation().y, 90.0f), "RotateBehaviour is frame dependent");
    Require(NearlyEqual(entity.GetTransform().Scale().x, 2.0f), "ScaleBehaviour X result is incorrect");
    Require(NearlyEqual(entity.GetTransform().Scale().z, 0.5f), "ScaleBehaviour Z result is incorrect");
}

void TestInputFrameTransitions()
{
    Input input;

    input.BeginFrame();
    input.HandleKey(InputKey::W, true);
    Require(input.IsKeyDown(InputKey::W), "Pressed key was not held");
    Require(input.WasKeyPressed(InputKey::W), "Key press transition was lost");
    Require(!input.WasKeyReleased(InputKey::W), "Pressed key was reported released");

    input.BeginFrame();
    Require(input.IsKeyDown(InputKey::W), "BeginFrame cleared held key state");
    Require(!input.WasKeyPressed(InputKey::W), "Key press leaked into the next frame");

    input.HandleKey(InputKey::W, false);
    Require(!input.IsKeyDown(InputKey::W), "Released key remained held");
    Require(input.WasKeyReleased(InputKey::W), "Key release transition was lost");

    input.HandleKey(InputKey::Right, true);
    input.HandleKey(InputKey::Space, true);
    Require(
        input.WasKeyPressed(InputKey::Right) &&
        input.WasKeyPressed(InputKey::Space),
        "Morph Lab navigation keys were not tracked"
    );

    input.HandleCursorPosition(10.0, 20.0);
    input.HandleCursorPosition(14.0, 17.0);
    input.HandleScroll(2.0);
    Require(NearlyEqual(static_cast<float>(input.CursorDelta().x), 4.0f), "Mouse X delta is incorrect");
    Require(NearlyEqual(static_cast<float>(input.CursorDelta().y), -3.0f), "Mouse Y delta is incorrect");
    Require(NearlyEqual(static_cast<float>(input.ScrollDeltaY()), 2.0f), "Scroll delta is incorrect");

    input.BeginFrame();
    Require(NearlyEqual(static_cast<float>(input.CursorDelta().x), 0.0f), "Mouse delta was not cleared");
    Require(NearlyEqual(static_cast<float>(input.ScrollDeltaY()), 0.0f), "Scroll delta was not cleared");
}

void TestFreeCameraController()
{
    Camera camera(CameraParam{
        .Position = {0.0f, 0.0f, 3.0f},
        .Target = {0.0f, 0.0f, 0.0f},
        .Up = {0.0f, 1.0f, 0.0f},
        .VerticalFovDegrees = 45.0f
    });
    Input input;
    FreeCameraControllerBehaviour controller(
        camera,
        input,
        FreeCameraControllerSettings{
            .moveSpeed = 2.0f,
            .sprintMultiplier = 2.0f,
            .mouseSensitivity = 1.0f,
            .scrollSensitivity = 5.0f
        }
    );

    input.BeginFrame();
    input.HandleKey(InputKey::W, true);
    controller.Update(0.5f);
    Require(NearlyEqual(camera.Position().z, 2.0f), "Free camera forward movement is incorrect");

    input.BeginFrame();
    input.HandleKey(InputKey::LeftShift, true);
    controller.Update(0.5f);
    Require(NearlyEqual(camera.Position().z, 0.0f), "Free camera sprint movement is incorrect");
    input.HandleKey(InputKey::W, false);
    input.HandleKey(InputKey::LeftShift, false);

    input.BeginFrame();
    input.HandleScroll(2.0);
    controller.Update(0.0f);
    Require(NearlyEqual(camera.VerticalFovDegrees(), 35.0f), "Free camera zoom is incorrect");

    input.BeginFrame();
    input.HandleMouseButton(InputMouseButton::Right, true);
    controller.Update(0.0f);
    Require(input.IsCursorCaptured(), "Right mouse button did not capture the cursor");

    input.BeginFrame();
    input.HandleMouseButton(InputMouseButton::Right, false);
    input.HandleCursorPosition(100.0, 100.0);
    input.HandleCursorPosition(110.0, 100.0);
    controller.Update(0.0f);
    Require(camera.Target().x > camera.Position().x, "Mouse movement did not rotate the camera");

    input.BeginFrame();
    input.HandleKey(InputKey::Escape, true);
    controller.Update(0.0f);
    Require(!input.IsCursorCaptured(), "Escape did not release the cursor");

    input.BeginFrame();
    input.HandleKey(InputKey::R, true);
    controller.Update(0.0f);
    Require(NearlyEqual(camera.Position().z, 3.0f), "Camera reset did not restore position");
    Require(NearlyEqual(camera.Target().x, 0.0f), "Camera reset did not restore direction");
    Require(NearlyEqual(camera.VerticalFovDegrees(), 45.0f), "Camera reset did not restore FOV");
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

void TestResourceManagerModelRegistry()
{
    ResourceManager resources;
    ModelAsset& model = resources.CreateModel("registeredModel");

    Require(resources.FindModel("registeredModel") == &model, "FindModel failed");
    Require(&resources.GetModel("registeredModel") == &model, "GetModel failed");

    bool duplicateRejected = false;
    try
    {
        resources.CreateModel("registeredModel");
    }
    catch (const std::invalid_argument&)
    {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "Duplicate model name was accepted");
}

void TestEnvironmentResourceAndSceneBinding()
{
    EnvironmentMapData data = EnvironmentMapData::ProceduralSky();
    data.environmentResolution = 64;
    data.irradianceResolution = 16;
    data.prefilterResolution = 64;
    data.prefilterMipLevels = 4;
    data.brdfResolution = 64;
    data.intensity = 1.5f;

    ResourceManager resources;
    EnvironmentMap& environment = resources.CreateEnvironment(
        "testEnvironment",
        data
    );
    Scene scene;
    scene.SetEnvironment(&environment);

    Require(
        scene.Environment() == &environment,
        "Scene did not preserve its environment reference"
    );
    Require(
        resources.FindEnvironment("testEnvironment") == &environment &&
        &resources.GetEnvironment("testEnvironment") == &environment,
        "ResourceManager environment lookup failed"
    );
    Require(
        resources.EnvironmentCount() == 1,
        "ResourceManager environment count is incorrect"
    );
    Require(!environment.IsAttached(), "CPU test unexpectedly created OpenGL IBL resources");
    Require(NearlyEqual(environment.Intensity(), 1.5f), "Environment intensity changed");
    Require(NearlyEqual(environment.MaxReflectionLod(), 3.0f), "Environment mip range changed");

    environment.SetIntensity(0.75f);
    environment.SetDrawSkybox(false);
    Require(NearlyEqual(environment.Intensity(), 0.75f), "Environment intensity setter failed");
    Require(!environment.ShouldDrawSkybox(), "Environment skybox setter failed");

    scene.ClearEnvironment();
    Require(scene.Environment() == nullptr, "Scene environment was not cleared");

    bool duplicateRejected = false;
    try
    {
        resources.CreateEnvironment("testEnvironment", data);
    }
    catch (const std::invalid_argument&)
    {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "Duplicate environment name was accepted");

    bool invalidMipCountRejected = false;
    try
    {
        EnvironmentMapData invalid = data;
        invalid.prefilterMipLevels = 8;
        EnvironmentMap invalidEnvironment(invalid);
    }
    catch (const std::invalid_argument&)
    {
        invalidMipCountRejected = true;
    }
    Require(invalidMipCountRejected, "Invalid environment mip count was accepted");
}

void TestStaticModelImporter()
{
    const ImportedModelData imported = ModelImporter().Import(
        TestAssetDirectory / "models" / "embedded_triangle.gltf"
    );

    Require(imported.meshes.size() == 1, "Importer mesh count is incorrect");
    Require(imported.materials.size() == 1, "Importer material count is incorrect");
    Require(
        imported.textures.size() == 4,
        "Importer embedded texture count is incorrect: " +
            std::to_string(imported.textures.size())
    );
    Require(imported.parts.size() == 2, "Importer did not preserve both node instances");

    const ImportedMeshData& mesh = imported.meshes[0];
    Require(mesh.data.vertices.size() == 45, "Importer vertex layout is incorrect");
    Require(mesh.data.indices.size() == 3, "Importer index count is incorrect");
    Require(mesh.data.layout.size() == 5, "Importer layout field count is incorrect");
    Require(mesh.materialIndex == 0, "Importer mesh material index is incorrect");
    Require(
        NearlyEqual(mesh.data.vertices[6], 0.0f) &&
        NearlyEqual(mesh.data.vertices[7], 0.0f) &&
        NearlyEqual(mesh.data.vertices[21], 1.0f) &&
        NearlyEqual(mesh.data.vertices[22], 0.0f) &&
        NearlyEqual(mesh.data.vertices[36], 0.0f) &&
        NearlyEqual(mesh.data.vertices[37], 1.0f),
        "Importer vertically flipped glTF texture coordinates"
    );
    for (std::size_t vertex = 0; vertex < 3; ++vertex)
    {
        const std::size_t tangentOffset = vertex * 15 + 11;
        const glm::vec3 tangent(
            mesh.data.vertices[tangentOffset],
            mesh.data.vertices[tangentOffset + 1],
            mesh.data.vertices[tangentOffset + 2]
        );
        Require(
            NearlyEqual(glm::length(tangent), 1.0f),
            "Importer produced a non-unit tangent"
        );
        Require(
            NearlyEqual(std::abs(mesh.data.vertices[tangentOffset + 3]), 1.0f),
            "Importer produced invalid tangent handedness"
        );
    }

    const ImportedMaterialData& material = imported.materials[0];
    Require(material.baseColorTexture == 0, "Importer lost base-color texture binding");
    Require(material.normalTexture == 1, "Importer lost normal texture binding");
    Require(
        material.metallicRoughnessTexture == 2,
        "Importer lost metallic-roughness texture binding"
    );
    Require(material.occlusionTexture == 2, "Importer lost occlusion texture binding");
    Require(material.emissiveTexture == 3, "Importer lost emissive texture binding");
    Require(NearlyEqual(material.normalScale, 0.75f), "Importer changed normal scale");
    Require(NearlyEqual(material.metallicFactor, 0.6f), "Importer changed metallic factor");
    Require(NearlyEqual(material.roughnessFactor, 0.4f), "Importer changed roughness factor");
    Require(NearlyEqual(material.occlusionStrength, 0.8f), "Importer changed AO strength");
    Require(NearlyEqual(material.emissiveFactor.r, 0.1f), "Importer changed emissive factor");
    Require(material.alphaMode == MaterialAlphaMode::Blend, "Importer lost alpha mode");
    Require(material.doubleSided, "Importer lost double-sided material state");
    Require(NearlyEqual(material.alphaCutoff, 0.35f), "Importer alpha cutoff changed");
    Require(NearlyEqual(material.baseColorFactor.r, 0.25f), "Importer base color changed");
    Require(NearlyEqual(material.baseColorFactor.a, 0.8f), "Importer base alpha changed");

    for (const ImportedTextureData& texture : imported.textures)
    {
        Require(
            texture.source.IsEncoded(),
            "Importer did not preserve embedded compressed texture bytes"
        );
        Require(
            !texture.source.data.empty(),
            "Importer produced an empty embedded texture"
        );
    }
    Require(
        imported.textures[0].source.colorSpace == TextureColorSpace::Srgb &&
        imported.textures[1].source.colorSpace == TextureColorSpace::Linear &&
        imported.textures[2].source.colorSpace == TextureColorSpace::Linear &&
        imported.textures[3].source.colorSpace == TextureColorSpace::Srgb,
        "Importer assigned an incorrect PBR texture color space"
    );
    Require(
        NearlyEqual(imported.parts[0].localTransform[3].x, 1.0f) &&
        NearlyEqual(imported.parts[0].localTransform[3].y, 2.0f) &&
        NearlyEqual(imported.parts[0].localTransform[3].z, 3.0f),
        "Importer changed first node transform"
    );
    Require(
        NearlyEqual(imported.parts[1].localTransform[3].x, -1.0f),
        "Importer changed second node transform"
    );
}

void TestImportedResourceCreation()
{
    ResourceManager resources;
    const std::filesystem::path modelPath =
        TestAssetDirectory / "models" / "embedded_triangle.gltf";
    ModelAsset& model = resources.LoadModel(
        "embeddedTriangle",
        modelPath
    );

    Require(model.PartCount() == 2, "Loaded ModelAsset part count is incorrect");
    Require(resources.ModelCount() == 1, "Imported model was not registered");
    Require(resources.MeshCount() == 1, "Imported shared mesh was duplicated");
    Require(resources.MaterialCount() == 1, "Imported material count is incorrect");
    Require(
        resources.TextureCount() == 4,
        "Imported texture count is incorrect: " +
            std::to_string(resources.TextureCount())
    );
    Require(
        &model.Parts()[0].GetMesh() == &model.Parts()[1].GetMesh(),
        "Node instances do not share their Mesh resource"
    );
    Require(
        &model.Parts()[0].GetMaterial() == &model.Parts()[1].GetMaterial(),
        "Node instances do not share their Material resource"
    );
    Require(
        !resources.GetTexture("embeddedTriangle::texture::0").IsAttached(),
        "CPU import unexpectedly created an OpenGL texture"
    );
    const Material& importedMaterial =
        resources.GetMaterial("embeddedTriangle::material::0");
    Require(
        importedMaterial.HasTexture(importedMaterial.Interface().normalTexture),
        "ResourceManager lost imported normal texture binding"
    );
    Require(
        NearlyEqual(importedMaterial.NormalScale(), 0.75f),
        "ResourceManager changed imported normal scale"
    );
    Require(
        importedMaterial.HasTexture(
            importedMaterial.Interface().metallicRoughnessTexture
        ) &&
        importedMaterial.HasTexture(importedMaterial.Interface().emissiveTexture) &&
        importedMaterial.HasTexture(importedMaterial.Interface().occlusionTexture),
        "ResourceManager lost imported PBR texture bindings"
    );
    Require(
        NearlyEqual(importedMaterial.MetallicFactor(), 0.6f) &&
        NearlyEqual(importedMaterial.RoughnessFactor(), 0.4f) &&
        NearlyEqual(importedMaterial.OcclusionStrength(), 0.8f) &&
        NearlyEqual(importedMaterial.EmissiveFactor().g, 0.2f),
        "ResourceManager changed imported PBR factors"
    );

    const std::filesystem::path equivalentPath =
        modelPath.parent_path() / "." / modelPath.filename();
    ModelAsset& cachedModel = resources.LoadModel(
        "embeddedTriangle",
        equivalentPath
    );
    Require(&cachedModel == &model, "Equivalent model path was imported twice");
    Require(
        resources.FindModelByPath(equivalentPath) == &model,
        "FindModelByPath did not use normalized paths"
    );
    Require(resources.ModelCount() == 1, "Model path cache added a duplicate model");
    Require(resources.MeshCount() == 1, "Model path cache added duplicate meshes");
    Require(resources.MaterialCount() == 1, "Model path cache added duplicate materials");
    Require(resources.TextureCount() == 4, "Model path cache added duplicate textures");

    bool aliasRejected = false;
    try
    {
        resources.LoadModel(
            "triangleAlias",
            equivalentPath
        );
    }
    catch (const std::invalid_argument&)
    {
        aliasRejected = true;
    }
    Require(aliasRejected, "One model file was accepted under two resource names");

    bool nameCollisionRejected = false;
    try
    {
        resources.LoadModel(
            "embeddedTriangle",
            TestAssetDirectory / "models" / "Box.glb"
        );
    }
    catch (const std::invalid_argument&)
    {
        nameCollisionRejected = true;
    }
    Require(
        nameCollisionRejected,
        "One resource name was accepted for two different model files"
    );
    Require(resources.ModelCount() == 1, "Rejected model changed the model registry");
}

void TestImporterRejectsMissingFile()
{
    bool rejected = false;
    try
    {
        ModelImporter().Import(TestAssetDirectory / "models" / "missing.glb");
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Importer accepted a missing model file");
}

void TestImportResourceCollisionIsTransactional()
{
    ResourceManager resources;
    resources.CreateTexture(
        "collision::texture::0",
        TextureData::FromEncoded({1})
    );

    bool rejected = false;
    try
    {
        resources.LoadModel(
            "collision",
            TestAssetDirectory / "models" / "embedded_triangle.gltf"
        );
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    Require(rejected, "Importer accepted a generated resource-name collision");
    Require(resources.ModelCount() == 0, "Failed import left a model resource behind");
    Require(resources.MeshCount() == 0, "Failed import left mesh resources behind");
    Require(resources.MaterialCount() == 0, "Failed import left material resources behind");
    Require(resources.TextureCount() == 1, "Failed import changed existing textures");
}

void TestConvertedMmdGlbWhenAvailable()
{
    const std::filesystem::path modelPath =
        TestAssetDirectory / "models" / u8"仪玄" / u8"仪玄.glb";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.meshes.size() == 21, "Converted MMD mesh primitive count changed");
    Require(imported.materials.size() == 21, "Converted MMD material count changed");
    Require(imported.textures.size() == 6, "Converted MMD embedded texture count changed");
    Require(imported.parts.size() == 21, "Converted MMD render-part count changed");

    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        Require(mesh.data.layout.size() == 5, "Converted MMD mesh layout is invalid");
        Require(!mesh.data.vertices.empty(), "Converted MMD mesh has no vertices");
        Require(!mesh.data.indices.empty(), "Converted MMD mesh has no indices");
        vertexCount += mesh.data.vertices.size() / 15;
        indexCount += mesh.data.indices.size();
    }
    Require(vertexCount >= 40000, "Converted MMD model lost too many vertices");
    Require(indexCount >= 100000, "Converted MMD model lost too many indices");

    std::size_t opaqueMaterialCount = 0;
    std::size_t blendMaterialCount = 0;
    for (const ImportedMaterialData& material : imported.materials)
    {
        opaqueMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Opaque ? 1U : 0U;
        blendMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Blend ? 1U : 0U;
    }
    Require(
        opaqueMaterialCount > 0 && blendMaterialCount > 0,
        "Converted MMD GLB alpha materials were not classified by texture content"
    );

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("yixuan", modelPath);
    Scene scene;
    Entity& instance = scene.InstantiateModel(model);

    Require(resources.ModelCount() == 1, "Converted MMD model was not registered");
    Require(resources.MeshCount() == 21, "Converted MMD mesh resources changed");
    Require(resources.MaterialCount() == 21, "Converted MMD material resources changed");
    Require(resources.TextureCount() == 6, "Converted MMD texture resources changed");
    Require(model.PartCount() == 21, "Converted MMD ModelAsset parts changed");
    Require(instance.RenderPartCount() == 21, "Converted MMD Entity parts changed");
}

void TestConvertedMmdObjWhenAvailable()
{
    const std::filesystem::path modelPath =
        TestAssetDirectory / "models" / u8"仪玄_obj" / u8"仪玄.obj";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.meshes.size() == 21, "Converted OBJ mesh count changed");
    Require(imported.materials.size() == 21, "Converted OBJ material count changed");
    Require(imported.textures.size() == 6, "Converted OBJ external texture count changed");
    Require(imported.parts.size() == 21, "Converted OBJ render-part count changed");

    std::size_t opaqueMaterialCount = 0;
    std::size_t blendMaterialCount = 0;
    for (const ImportedMaterialData& material : imported.materials)
    {
        opaqueMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Opaque ? 1U : 0U;
        blendMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Blend ? 1U : 0U;
    }
    Require(
        opaqueMaterialCount > 0 && blendMaterialCount > 0,
        "Converted OBJ alpha materials were not classified by texture content"
    );

    for (const ImportedTextureData& texture : imported.textures)
    {
        Require(texture.source.IsFile(), "OBJ texture was not kept as an external file");
        Require(
            std::filesystem::is_regular_file(texture.source.filePath),
            "OBJ external texture file was not resolved"
        );
        Require(
            texture.source.filePath.parent_path() == modelPath.parent_path(),
            "OBJ texture path was not resolved relative to its MTL"
        );
    }

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("yixuanObj", modelPath);
    Scene scene;
    Entity& instance = scene.InstantiateModel(model);

    Require(resources.ModelCount() == 1, "Converted OBJ model was not registered");
    Require(resources.MeshCount() == 21, "Converted OBJ mesh resources changed");
    Require(resources.MaterialCount() == 21, "Converted OBJ material resources changed");
    Require(resources.TextureCount() == 6, "Converted OBJ texture resources changed");
    Require(model.PartCount() == 21, "Converted OBJ ModelAsset parts changed");
    Require(instance.RenderPartCount() == 21, "Converted OBJ Entity parts changed");

    for (std::size_t index = 0; index < imported.textures.size(); ++index)
    {
        Texture& namedTexture = resources.GetTexture(
            "yixuanObj::texture::" + std::to_string(index)
        );
        Require(
            resources.FindTextureByPath(imported.textures[index].source.filePath) ==
                &namedTexture,
            "External texture path cache points to the wrong resource"
        );
    }

    const std::filesystem::path firstTexturePath =
        imported.textures[0].source.filePath;
    Texture& textureAlias = resources.CreateTexture(
        "yixuanObjTextureAlias",
        TextureData::FromFile(
            firstTexturePath.parent_path() / "." / firstTexturePath.filename()
        )
    );
    Require(
        &textureAlias == &resources.GetTexture("yixuanObj::texture::0"),
        "Equivalent external texture path created a second Texture object"
    );
    Require(
        resources.TextureCount() == 7,
        "Texture alias was not registered as a named resource"
    );

    Texture& linearTexture = resources.CreateTexture(
        "yixuanObjLinearTexture",
        TextureData::FromFile(firstTexturePath, TextureColorSpace::Linear)
    );
    Require(
        &linearTexture != &textureAlias,
        "Texture cache shared one GPU texture across different color spaces"
    );
    Require(
        resources.FindTextureByPath(
            firstTexturePath,
            TextureColorSpace::Linear
        ) == &linearTexture,
        "Linear texture path cache lookup failed"
    );
    Require(resources.TextureCount() == 8, "Linear texture alias was not registered");
}

void TestRiggedGlbImportWhenAvailable()
{
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "glb" /
        u8"仪玄_glb" / u8"仪玄.glb";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.skeleton.has_value(), "Rigged GLB lost its Skeleton");
    const Skeleton& skeleton = *imported.skeleton;
    Require(skeleton.BoneCount() >= 400, "Rigged GLB lost too many bones");
    const Pose bindPose(skeleton);
    const std::span<const glm::mat4> glbSkinMatrices =
        bindPose.SkinningMatrices();
    for (std::size_t index = 0; index < glbSkinMatrices.size(); ++index)
    {
        Require(
            MatrixIdentityDeviation(glbSkinMatrices[index]) <= 0.002f,
            "Rigged GLB bind pose changed bone " +
                std::to_string(index) + " (" +
                skeleton.BoneAt(static_cast<BoneIndex>(index)).name +
                "), deviation=" +
                std::to_string(MatrixIdentityDeviation(glbSkinMatrices[index]))
        );
    }

    std::size_t skinnedMeshCount = 0;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        if (mesh.requiredBoneCount == 0)
            continue;
        ++skinnedMeshCount;
        Require(mesh.data.layout.size() == 7, "Rigged GLB skin layout is invalid");
        Require(
            mesh.data.layout[5].name == "boneIndices" &&
            mesh.data.layout[5].location == 7U &&
            mesh.data.layout[6].name == "boneWeights" &&
            mesh.data.layout[6].location == 8U,
            "Rigged GLB skin attributes use incorrect locations"
        );
        constexpr std::size_t Stride = 23U;
        Require(
            mesh.data.vertices.size() % Stride == 0,
            "Rigged GLB vertex data does not match its skin layout"
        );
        for (std::size_t offset = 15U;
             offset < mesh.data.vertices.size();
             offset += Stride)
        {
            float weightSum = 0.0f;
            for (std::size_t influence = 0; influence < 4U; ++influence)
            {
                const float boneIndex = mesh.data.vertices[offset + influence];
                const float weight = mesh.data.vertices[offset + 4U + influence];
                Require(
                    std::isfinite(boneIndex) && boneIndex >= 0.0f &&
                    boneIndex < static_cast<float>(skeleton.BoneCount()),
                    "Rigged GLB contains an invalid vertex bone index"
                );
                Require(
                    std::isfinite(weight) && weight >= 0.0f,
                    "Rigged GLB contains an invalid vertex bone weight"
                );
                weightSum += weight;
            }
            Require(
                NearlyEqual(weightSum, 1.0f),
                "Rigged GLB vertex bone weights are not normalized"
            );
        }
    }
    Require(skinnedMeshCount > 0, "Rigged GLB produced no skinned Mesh");

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("riggedYixuan", modelPath);
    Require(model.HasSkeleton(), "ResourceManager lost the imported Skeleton");
    Scene scene;
    Entity& instance = scene.InstantiateModel(model);
    Require(instance.HasPose(), "Model instance did not create a Pose");
    Require(
        &instance.GetPose().GetSkeleton() == &model.GetSkeleton(),
        "Entity Pose does not reference the ModelAsset Skeleton"
    );
}

void TestDemoPmxPhysicsImportWhenAvailable()
{
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.mmdPhysics.has_value() && imported.skeleton.has_value(),
        "Demo PMX lost its Physics 1 metadata"
    );
    const MmdPhysicsAsset& physics = *imported.mmdPhysics;
    Require(
        physics.RigidBodyCount() == 495U && physics.JointCount() == 568U,
        "Demo PMX rigid-body or joint count changed"
    );

    std::array<std::size_t, 3U> shapeCounts{};
    std::array<std::size_t, 3U> modeCounts{};
    for (const MmdRigidBodyDefinition& body : physics.RigidBodies())
    {
        ++shapeCounts[static_cast<std::size_t>(body.shape)];
        ++modeCounts[static_cast<std::size_t>(body.mode)];
        Require(
            body.bone == InvalidBoneIndex ||
                static_cast<std::size_t>(body.bone) <
                    imported.skeleton->BoneCount(),
            "Demo PMX rigid body references an invalid runtime bone"
        );
    }
    Require(
        std::all_of(shapeCounts.begin(), shapeCounts.end(),
            [](std::size_t count) { return count > 0U; }) &&
        std::all_of(modeCounts.begin(), modeCounts.end(),
            [](std::size_t count) { return count > 0U; }),
        "Demo PMX did not exercise every rigid-body shape and mode"
    );
    Require(
        std::all_of(
            physics.Joints().begin(),
            physics.Joints().end(),
            [](const MmdJointDefinition& joint)
            {
                return joint.type == MmdJointType::Spring6Dof;
            }
        ),
        "Demo PMX joint types changed unexpectedly"
    );
}

void TestDirectPmxMaterialImportWhenAvailable()
{
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"仪玄_pmx" / u8"仪玄.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.meshes.size() == 21, "Direct PMX mesh count changed");
    Require(imported.materials.size() == 21, "Direct PMX material count changed");
    Require(imported.parts.size() == 21, "Direct PMX part count changed");
    Require(imported.skeleton.has_value(), "Direct PMX lost its Skeleton");
    Require(!imported.morphs.empty(), "Direct PMX lost its morph definitions");
    std::size_t meshMorphTargetCount = 0U;
    for (const ImportedMeshData& mesh : imported.meshes)
        meshMorphTargetCount += mesh.morphTargets.size();
    Require(
        meshMorphTargetCount > 0U,
        "Direct PMX vertex morphs were not mapped to imported Mesh vertices"
    );
    std::size_t appendConstraintCount = 0U;
    std::size_t ikConstraintCount = 0U;
    for (const Bone& bone : imported.skeleton->Bones())
    {
        appendConstraintCount += bone.appendTransform.has_value() ? 1U : 0U;
        ikConstraintCount += bone.ikConstraint.has_value() ? 1U : 0U;
    }
    Require(
        imported.skeleton->HasMmdConstraints() &&
        appendConstraintCount > 0U &&
        ikConstraintCount > 0U,
        "Direct PMX lost its Append/Grant or IK constraints"
    );
    const Pose pmxBindPose(*imported.skeleton);
    for (const glm::mat4& skinMatrix : pmxBindPose.SkinningMatrices())
    {
        Require(
            NearlyEqual(skinMatrix, glm::mat4(1.0f)),
            "Direct PMX bind pose changed vertex positions"
        );
    }
    Require(
        imported.meshes[0].data.vertices.size() >= 18U &&
        NearlyEqual(imported.meshes[0].data.vertices[6], 0.56001925f) &&
        NearlyEqual(imported.meshes[0].data.vertices[7], 0.56904125f),
        "Direct PMX texture coordinates were vertically flipped"
    );

    std::size_t sphereMaterialCount = 0;
    std::size_t toonMaterialCount = 0;
    std::size_t edgeMaterialCount = 0;
    std::size_t transparentMaterialCount = 0;
    std::size_t maskedMaterialCount = 0;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        Require(
            mesh.data.layout.size() == 9,
            "PMX mesh is missing MMD or skinning attributes"
        );
        const std::size_t stride = 26U;
        Require(
            mesh.data.vertices.size() % stride == 0,
            "PMX vertex data does not match its UV layout"
        );
        for (std::size_t offset = 17;
             offset < mesh.data.vertices.size();
             offset += stride)
        {
            Require(
                std::isfinite(mesh.data.vertices[offset]) &&
                    mesh.data.vertices[offset] >= 0.0f,
                "PMX vertex edge scale is invalid"
            );
        }
        Require(mesh.requiredBoneCount > 0, "PMX mesh lost its bone weights");
        for (std::size_t offset = 22;
             offset < mesh.data.vertices.size();
             offset += stride)
        {
            const float weightSum =
                mesh.data.vertices[offset] +
                mesh.data.vertices[offset + 1U] +
                mesh.data.vertices[offset + 2U] +
                mesh.data.vertices[offset + 3U];
            Require(
                NearlyEqual(weightSum, 1.0f),
                "PMX vertex bone weights are not normalized"
            );
        }
    }
    for (const ImportedMaterialData& material : imported.materials)
    {
        Require(
            material.shadingModel == MaterialShadingModel::MmdToon,
            "PMX material did not select MMD Toon shading"
        );
        sphereMaterialCount += material.sphereTexture.has_value() ? 1U : 0U;
        toonMaterialCount += material.toonTexture.has_value() ? 1U : 0U;
        edgeMaterialCount += material.edgeEnabled ? 1U : 0U;
        transparentMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Blend ? 1U : 0U;
        maskedMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Mask ? 1U : 0U;
    }
    Require(sphereMaterialCount > 0, "PMX sphere maps were not imported");
    Require(toonMaterialCount > 0, "PMX Toon ramps were not imported");
    Require(edgeMaterialCount > 0, "PMX edge flags were not imported");
    Require(transparentMaterialCount > 0, "PMX alpha materials were not imported");
    Require(maskedMaterialCount > 0, "PMX texture cutouts were not classified as Mask");

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("directPmx", modelPath);
    Require(model.PartCount() == 21, "Direct PMX ModelAsset part count changed");
    Require(model.HasSkeleton(), "ResourceManager lost the PMX Skeleton");
    Require(
        model.HasMorphs() &&
        model.GetMorphSet().MorphCount() == imported.morphs.size(),
        "ResourceManager lost the PMX MorphSet"
    );
    Scene morphScene;
    Entity& morphInstance = morphScene.InstantiateModel(model);
    Require(
        morphInstance.HasMorphState() &&
        morphInstance.GetMorphState().MorphCount() ==
            model.GetMorphSet().MorphCount(),
        "Scene did not create per-instance PMX MorphState"
    );
    for (std::size_t index = 0; index < resources.MaterialCount(); ++index)
    {
        const Material& material = resources.GetMaterial(
            "directPmx::material::" + std::to_string(index)
        );
        Require(
            material.ShadingModel() == MaterialShadingModel::MmdToon,
            "ResourceManager changed PMX shading model"
        );
        Require(
            !material.Interface().imageBasedLightingEnabled,
            "MMD Toon material unexpectedly enabled PBR IBL uniforms"
        );
    }
}

void TestDirectPmxGroupMorphImportWhenAvailable()
{
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"爱弥斯_pmx" / u8"爱弥斯.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    const auto group = std::find_if(
        imported.morphs.begin(),
        imported.morphs.end(),
        [](const MorphDefinition& morph)
        {
            return morph.kind == MorphKind::Group &&
                !morph.groupMembers.empty();
        }
    );
    Require(
        group != imported.morphs.end(),
        "PMX importer lost real Group Morph definitions"
    );

    const MorphSet morphSet(imported.morphs);
    const MorphIndex groupIndex = static_cast<MorphIndex>(
        std::distance(imported.morphs.begin(), group)
    );
    MorphState state(morphSet);
    state.SetWeight(groupIndex, 0.75f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[groupIndex], 0.0f),
        "Group Morph incorrectly remained a vertex deformation weight"
    );

    bool drivesImportedVertexTarget = false;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        for (const MeshMorphTarget& target : mesh.morphTargets)
        {
            if (std::abs(effective[target.morphIndex]) > Epsilon)
            {
                drivesImportedVertexTarget = true;
                break;
            }
        }
        if (drivesImportedVertexTarget)
            break;
    }
    Require(
        drivesImportedVertexTarget,
        "Imported Group Morph did not resolve to a Mesh vertex target"
    );
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

std::unique_ptr<ModelAsset> CreatePhysics2BModeModel()
{
    const glm::mat4 dynamicBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 4.0f, 0.0f)
    );
    const glm::mat4 hybridBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(3.0f, 3.0f, 0.0f)
    );
    auto model = std::make_unique<ModelAsset>("physics2bModes");
    model->SetSkeleton(Skeleton({
        Bone{"follow", InvalidBoneIndex, glm::mat4(1.0f), glm::mat4(1.0f)},
        Bone{"dynamic", InvalidBoneIndex, dynamicBind, glm::inverse(dynamicBind)},
        Bone{"hybrid", InvalidBoneIndex, hybridBind, glm::inverse(hybridBind)}
    }));

    std::vector<MmdRigidBodyDefinition> bodies(3U);
    bodies[0U].name = "followBody";
    bodies[0U].bone = 0U;
    bodies[0U].shape = MmdRigidBodyShape::Sphere;
    bodies[0U].size = glm::vec3(0.3f, 0.0f, 0.0f);
    bodies[0U].mode = MmdRigidBodyMode::FollowBone;
    bodies[0U].modelBindTransform = glm::mat4(1.0f);
    bodies[0U].boneToBody = glm::mat4(1.0f);
    bodies[0U].bodyToBone = glm::mat4(1.0f);

    bodies[1U].name = "dynamicBody";
    bodies[1U].bone = 1U;
    bodies[1U].shape = MmdRigidBodyShape::Sphere;
    bodies[1U].size = glm::vec3(0.3f, 0.0f, 0.0f);
    bodies[1U].mass = 1.0f;
    bodies[1U].linearDamping = 0.05f;
    bodies[1U].angularDamping = 0.05f;
    bodies[1U].mode = MmdRigidBodyMode::Physics;
    bodies[1U].position = glm::vec3(dynamicBind[3]);
    bodies[1U].modelBindTransform = dynamicBind;
    bodies[1U].boneToBody = glm::mat4(1.0f);
    bodies[1U].bodyToBone = glm::mat4(1.0f);

    bodies[2U].name = "hybridBody";
    bodies[2U].bone = 2U;
    bodies[2U].shape = MmdRigidBodyShape::Box;
    bodies[2U].size = glm::vec3(0.25f);
    bodies[2U].mass = 1.0f;
    bodies[2U].linearDamping = 0.05f;
    bodies[2U].angularDamping = 0.05f;
    bodies[2U].mode = MmdRigidBodyMode::PhysicsWithBone;
    bodies[2U].position = glm::vec3(hybridBind[3]);
    bodies[2U].modelBindTransform = hybridBind;
    bodies[2U].boneToBody = glm::mat4(1.0f);
    bodies[2U].bodyToBone = glm::mat4(1.0f);

    model->SetMmdPhysics(MmdPhysicsAsset(std::move(bodies), {}));
    return model;
}

std::unique_ptr<ModelAsset> CreatePhysics2BSpringModel()
{
    auto model = std::make_unique<ModelAsset>("physics2bSpring");
    model->SetSkeleton(Skeleton({Bone{"root"}}));

    std::vector<MmdRigidBodyDefinition> bodies(2U);
    const glm::vec3 positions[] = {
        glm::vec3(-0.5f, 3.0f, 0.0f),
        glm::vec3(0.5f, 3.0f, 0.0f)
    };
    for (std::size_t index = 0; index < bodies.size(); ++index)
    {
        bodies[index].name = "springBody" + std::to_string(index);
        bodies[index].shape = MmdRigidBodyShape::Sphere;
        bodies[index].size = glm::vec3(0.2f, 0.0f, 0.0f);
        bodies[index].position = positions[index];
        bodies[index].mass = 1.0f;
        bodies[index].mode = MmdRigidBodyMode::Physics;
        bodies[index].modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            positions[index]
        );
    }

    MmdJointDefinition joint;
    joint.name = "springJoint";
    joint.type = MmdJointType::Spring6Dof;
    joint.bodyA = 0U;
    joint.bodyB = 1U;
    joint.position = glm::vec3(0.0f, 3.0f, 0.0f);
    joint.modelBindTransform = glm::translate(
        glm::mat4(1.0f),
        joint.position
    );
    joint.linearLower = glm::vec3(0.0f);
    joint.linearUpper = glm::vec3(0.0f);
    joint.angularLower = glm::vec3(0.0f);
    joint.angularUpper = glm::vec3(0.0f);
    joint.linearSpring = glm::vec3(20.0f);
    joint.angularSpring = glm::vec3(5.0f);

    model->SetMmdPhysics(MmdPhysicsAsset(std::move(bodies), {joint}));
    return model;
}


std::unique_ptr<ModelAsset> CreatePhysics3AllJointModel()
{
    auto model = std::make_unique<ModelAsset>("physics3AllJoints");
    model->SetSkeleton(Skeleton({Bone{"root"}}));

    std::vector<MmdRigidBodyDefinition> bodies(2U);
    const glm::vec3 positions[] = {
        glm::vec3(-0.5f, 2.0f, 0.0f),
        glm::vec3(0.5f, 2.0f, 0.0f)
    };
    for (std::size_t index = 0; index < bodies.size(); ++index)
    {
        bodies[index].name = "jointBody" + std::to_string(index);
        bodies[index].shape = MmdRigidBodyShape::Sphere;
        bodies[index].size = glm::vec3(0.18f, 0.0f, 0.0f);
        bodies[index].position = positions[index];
        bodies[index].mass = 1.0f;
        bodies[index].mode = MmdRigidBodyMode::Physics;
        bodies[index].modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            positions[index]
        );
    }

    const std::array<MmdJointType, 6U> types{
        MmdJointType::Spring6Dof,
        MmdJointType::SixDof,
        MmdJointType::PointToPoint,
        MmdJointType::ConeTwist,
        MmdJointType::Slider,
        MmdJointType::Hinge
    };
    std::vector<MmdJointDefinition> joints;
    joints.reserve(types.size());
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        MmdJointDefinition joint;
        joint.name = "joint" + std::to_string(index);
        joint.type = types[index];
        joint.bodyA = 0U;
        joint.bodyB = 1U;
        joint.position = glm::vec3(0.0f, 2.0f, 0.0f);
        joint.modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            joint.position
        );
        joint.linearLower = glm::vec3(-0.05f);
        joint.linearUpper = glm::vec3(0.05f);
        joint.angularLower = glm::vec3(-0.2f);
        joint.angularUpper = glm::vec3(0.2f);
        if (joint.type == MmdJointType::Spring6Dof)
        {
            joint.linearSpring = glm::vec3(8.0f);
            joint.angularSpring = glm::vec3(3.0f);
        }
        joints.push_back(std::move(joint));
    }

    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::move(joints)
    ));
    return model;
}

std::unique_ptr<ModelAsset> CreatePhysics3ImpulseModel()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics2BModeModel();

    MorphDefinition global;
    global.name = "globalImpulse";
    global.kind = MorphKind::Impulse;
    global.impulseOffsets.push_back(ImpulseMorphOffset{
        1U,
        false,
        glm::vec3(2.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    });

    MorphDefinition local;
    local.name = "localImpulse";
    local.kind = MorphKind::Impulse;
    local.impulseOffsets.push_back(ImpulseMorphOffset{
        1U,
        true,
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    });

    MorphDefinition reset;
    reset.name = "resetImpulse";
    reset.kind = MorphKind::Impulse;
    reset.impulseOffsets.push_back(ImpulseMorphOffset{
        1U,
        false,
        glm::vec3(0.0f),
        glm::vec3(0.0f)
    });

    model->SetMorphs({
        std::move(global),
        std::move(local),
        std::move(reset)
    });
    return model;
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

void TestMmdPhysics2BModesAndPoseSync()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics2BModeModel();
    Scene scene;
    Entity& entity = scene.InstantiateModel(*model);
    Require(
        entity.HasMmdPhysics() &&
        entity.GetMmdPhysics().RigidBodyCount() == 3U &&
        scene.Physics().BodyCount() == 3U,
        "Scene did not instantiate per-Entity MMD rigid bodies"
    );

    entity.GetPose().SetLocalMatrix(
        0U,
        glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f))
    );
    const std::uint64_t revisionBeforePhysics = entity.GetPose().Revision();
    scene.Update(1.0f / 60.0f);
    Require(
        entity.GetPose().Revision() == revisionBeforePhysics + 1U,
        "MMD physics did not batch its Pose write-back into one revision"
    );
    Require(
        std::abs(entity.GetMmdPhysics().BodyStateAt(0U).position.x - 1.5f) < 0.02f,
        "FollowBone rigid body did not follow the animated bone"
    );

    const float dynamicStartY = entity.GetMmdPhysics().BodyStateAt(1U).position.y;
    for (int frame = 0; frame < 30; ++frame)
        scene.Update(1.0f / 60.0f);
    const PhysicsBodyState dynamicState = entity.GetMmdPhysics().BodyStateAt(1U);
    Require(
        dynamicState.position.y < dynamicStartY - 0.5f,
        "Physics rigid body did not fall under Bullet gravity"
    );
    Require(
        std::abs(entity.GetPose().GlobalMatrix(1U)[3].y - dynamicState.position.y) < 0.03f,
        "Physics rigid body did not write its transform back to Pose"
    );

    entity.GetPose().SetLocalMatrix(
        2U,
        glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 3.0f, 0.0f))
    );
    const glm::vec3 hybridAnimatedPosition(4.0f, 3.0f, 0.0f);
    const float hybridStartY =
        entity.GetMmdPhysics().BodyStateAt(2U).position.y;
    scene.Physics().ApplyTorqueImpulse(
        entity.GetMmdPhysics().BodyHandleAt(2U),
        glm::vec3(0.0f, 0.0f, 2.0f)
    );
    for (int frame = 0; frame < 20; ++frame)
        scene.Update(1.0f / 60.0f);
    const PhysicsBodyState hybridState = entity.GetMmdPhysics().BodyStateAt(2U);
    Require(
        hybridState.position.y < hybridStartY - 0.25f,
        "PhysicsWithBone rigid body did not remain fully dynamic under gravity"
    );
    const BoneTransform hybridPose = BoneTransform::FromMatrix(
        entity.GetPose().GlobalMatrix(2U)
    );
    Require(
        glm::distance(hybridPose.translation, hybridAnimatedPosition) < 0.03f,
        "PhysicsWithBone did not retain the animation-authored bone position"
    );
    Require(
        !NearlySameRotation(hybridState.rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) &&
        NearlySameRotation(hybridPose.rotation, hybridState.rotation),
        "PhysicsWithBone did not preserve Bullet rotation in Pose"
    );

    scene.Physics().ApplyCentralImpulse(
        entity.GetMmdPhysics().BodyHandleAt(1U),
        glm::vec3(3.0f, 0.0f, 0.0f)
    );
    scene.Update(1.0f / 60.0f);
    entity.ResetPhysicsToCurrentPose();
    const PhysicsBodyState resetState = entity.GetMmdPhysics().BodyStateAt(1U);
    Require(
        glm::length(resetState.linearVelocity) < 0.0001f &&
        glm::length(resetState.angularVelocity) < 0.0001f,
        "MMD physics Reset did not clear Bullet velocity"
    );
}



std::unique_ptr<ModelAsset> CreatePhysicsWithBoneFidelityModel()
{
    auto model = std::make_unique<ModelAsset>("physicsWithBoneFidelity");
    model->SetSkeleton(Skeleton({
        Bone{"mode2", InvalidBoneIndex, glm::mat4(1.0f), glm::mat4(1.0f)}
    }));

    MmdRigidBodyDefinition body;
    body.name = "offsetMode2Body";
    body.bone = 0U;
    body.shape = MmdRigidBodyShape::Box;
    body.size = glm::vec3(0.25f);
    body.mass = 1.0f;
    body.linearDamping = 0.05f;
    body.angularDamping = 0.05f;
    body.mode = MmdRigidBodyMode::PhysicsWithBone;
    body.position = glm::vec3(1.0f, 0.0f, 0.0f);
    body.modelBindTransform = glm::translate(
        glm::mat4(1.0f),
        body.position
    );
    body.boneToBody = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    body.bodyToBone = glm::inverse(body.boneToBody);
    model->SetMmdPhysics(MmdPhysicsAsset({body}, {}));
    return model;
}

void TestMmdPhysicsRuntimePolicyInjection()
{
    MmdPhysicsRuntimePolicy policy =
        MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults();
    policy.name = "test-profile";
    policy.recovery.enabled = false;
    policy.ccd.adaptive = false;
    policy.enableChainProfiles = false;
    policy.skirt.gravityScale = 0.42f;

    std::unique_ptr<ModelAsset> model = CreatePhysicsWithBoneFidelityModel();
    PhysicsWorld world;
    Entity entity;
    entity.SetSkeleton(model->GetSkeleton());
    entity.SetMmdPhysics(world, model->GetMmdPhysics(), policy);

    const MmdPhysicsRuntimePolicy& applied =
        entity.GetMmdPhysics().RuntimePolicy();
    Require(applied.name == "test-profile",
        "MMD runtime policy name was not preserved");
    Require(!applied.recovery.enabled && !applied.ccd.adaptive &&
            !applied.enableChainProfiles,
        "MMD runtime feature switches were not injected");
    Require(NearlyEqual(
            applied.ChainTuning(MmdPhysicsChainKind::Skirt).gravityScale,
            0.42f
        ),
        "MMD chain tuning was not injected");
}

void TestMmdPhysicsFidelityModesAndDebugLayers()
{
    std::unique_ptr<ModelAsset> model = CreatePhysicsWithBoneFidelityModel();
    Scene scene;
    Entity& entity = scene.InstantiateModel(*model);
    scene.Update(0.0f);

    MmdPhysicsInstance& physics = entity.GetMmdPhysics();
    const PhysicsBodyHandle body = physics.BodyHandleAt(0U);
    const glm::mat4 authoredBone = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.5f, 2.0f, 0.0f)
    );
    const glm::vec3 bodyPosition(3.0f, 4.0f, 0.0f);
    const glm::quat bodyRotation = glm::angleAxis(
        glm::half_pi<float>(),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );
    const glm::mat4 bodyMatrix = glm::translate(
        glm::mat4(1.0f),
        bodyPosition
    ) * glm::mat4_cast(bodyRotation);
    const glm::mat4 bodyToBone = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f)
    );

    const auto evaluate = [&](MmdPhysicsWithBoneSyncMode mode)
    {
        physics.SetPhysicsWithBoneSyncMode(mode);
        entity.GetPose().SetLocalMatrix(0U, authoredBone);
        scene.Physics().SetTransform(
            body,
            bodyPosition,
            bodyRotation,
            true
        );
        scene.Update(0.0f);
        return BoneTransform::FromMatrix(
            entity.GetPose().GlobalMatrix(0U)
        );
    };

    const BoneTransform rotationOnly = evaluate(
        MmdPhysicsWithBoneSyncMode::RotationOnly
    );
    Require(
        glm::distance(
            rotationOnly.translation,
            glm::vec3(authoredBone[3])
        ) < 0.0001f &&
        NearlySameRotation(rotationOnly.rotation, bodyRotation),
        "Mode 2 RotationOnly did not preserve authored translation and Bullet rotation"
    );
    const MmdPhysicsFidelityStatistics rotationOnlyStats =
        physics.FidelityStatistics();
    Require(
        rotationOnlyStats.drivenBoneCount == 1U &&
        rotationOnlyStats.physicsWithBoneCount == 1U &&
        rotationOnlyStats.maximumBulletToBonePositionError > 1.0f,
        "Mode 2 fidelity statistics missed RotationOnly position divergence"
    );

    const BoneTransform fullBody = evaluate(
        MmdPhysicsWithBoneSyncMode::FullBody
    );
    const BoneTransform expectedFullBody = BoneTransform::FromMatrix(
        bodyMatrix * bodyToBone
    );
    Require(
        glm::distance(
            fullBody.translation,
            expectedFullBody.translation
        ) < 0.0001f &&
        NearlySameRotation(fullBody.rotation, expectedFullBody.rotation),
        "Mode 2 FullBody did not write the complete Bullet-derived bone transform"
    );
    Require(
        physics.FidelityStatistics().maximumBulletToBonePositionError < 0.0001f,
        "FullBody mode retained an unexpected Bullet-to-bone position error"
    );

    const BoneTransform translationDelta = evaluate(
        MmdPhysicsWithBoneSyncMode::TranslationDelta
    );
    const glm::vec3 animatedBodyPosition =
        glm::vec3(authoredBone[3]) + glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 expectedDeltaPosition =
        glm::vec3(authoredBone[3]) +
        (bodyPosition - animatedBodyPosition);
    Require(
        glm::distance(
            translationDelta.translation,
            expectedDeltaPosition
        ) < 0.0001f &&
        NearlySameRotation(translationDelta.rotation, bodyRotation),
        "Mode 2 TranslationDelta did not preserve the authored pivot while applying body displacement"
    );
    Require(
        glm::distance(
            translationDelta.translation,
            fullBody.translation
        ) > 0.5f,
        "Mode 2 TranslationDelta unexpectedly collapsed to FullBody semantics"
    );
    const MmdPhysicsFidelityStatistics deltaStats =
        physics.FidelityStatistics();
    Require(
        std::abs(
            deltaStats.maximumMode2TranslationDelta -
            glm::distance(bodyPosition, animatedBodyPosition)
        ) < 0.0001f,
        "Mode 2 translation-delta statistics are inconsistent"
    );

    physics.SetPhysicsWithBoneSyncMode(
        MmdPhysicsWithBoneSyncMode::RotationOnly
    );
    Require(
        physics.CyclePhysicsWithBoneSyncMode() ==
            MmdPhysicsWithBoneSyncMode::FullBody &&
        physics.CyclePhysicsWithBoneSyncMode() ==
            MmdPhysicsWithBoneSyncMode::TranslationDelta &&
        physics.CyclePhysicsWithBoneSyncMode() ==
            MmdPhysicsWithBoneSyncMode::RotationOnly,
        "Mode 2 sync strategy cycle order changed"
    );

    physics.SetFidelityDebugLayer(MmdPhysicsFidelityDebugLayer::Off);
    Require(
        physics.CycleFidelityDebugLayer() ==
            MmdPhysicsFidelityDebugLayer::Bone &&
        physics.CycleFidelityDebugLayer() ==
            MmdPhysicsFidelityDebugLayer::Vertex &&
        physics.CycleFidelityDebugLayer() ==
            MmdPhysicsFidelityDebugLayer::All &&
        physics.CycleFidelityDebugLayer() ==
            MmdPhysicsFidelityDebugLayer::Off,
        "Physics fidelity debug-layer cycle order changed"
    );

    physics.SetFidelityDebugLayer(MmdPhysicsFidelityDebugLayer::Bone);
    physics.SetDebugOverlay(MmdPhysicsDebugOverlay::Off);
    std::vector<PhysicsDebugLine> lines;
    physics.AppendDebugLines(lines);
    const bool hasOrangeBoneAxis = std::any_of(
        lines.begin(),
        lines.end(),
        [](const PhysicsDebugLine& line)
        {
            return NearlyEqual(line.color, glm::vec3(1.0f, 0.30f, 0.0f)) ||
                NearlyEqual(line.color, glm::vec3(1.0f, 0.62f, 0.08f)) ||
                NearlyEqual(line.color, glm::vec3(1.0f, 0.88f, 0.32f));
        }
    );
    Require(
        hasOrangeBoneAxis,
        "Bone fidelity layer did not draw post-physics bone axes"
    );
}

void TestMeshSkinningFidelityDebugSamples()
{
    Skeleton skeleton({
        Bone{"root", InvalidBoneIndex, glm::mat4(1.0f), glm::mat4(1.0f)}
    });
    Pose pose(skeleton);
    pose.SetLocalMatrix(
        0U,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f))
    );

    DefaultModelData data{
        {
            1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f
        },
        {0U},
        {
            {"position", 3U, FLOAT},
            {"boneIndices", 4U, FLOAT},
            {"boneWeights", 4U, FLOAT}
        }
    };
    Mesh mesh(std::move(data), 1U);
    std::vector<std::uint8_t> drivenModes{3U};
    std::vector<PhysicsDebugLine> lines;
    const glm::mat4 modelMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(5.0f, 0.0f, 0.0f)
    );
    const std::size_t sampled = mesh.AppendSkinningDebugLines(
        lines,
        pose,
        drivenModes,
        modelMatrix,
        nullptr,
        8U
    );
    Require(
        sampled == 1U && lines.size() == 4U,
        "Vertex fidelity layer did not sample the skinned vertex"
    );

    const glm::vec3 expectedWorldPosition(8.0f, 0.0f, 0.0f);
    const bool hasPurpleVertexCross = std::any_of(
        lines.begin(),
        lines.end(),
        [&expectedWorldPosition](const PhysicsDebugLine& line)
        {
            return NearlyEqual(line.color, glm::vec3(0.82f, 0.18f, 1.0f)) &&
                glm::distance(
                    (line.from + line.to) * 0.5f,
                    expectedWorldPosition
                ) < 0.0001f;
        }
    );
    const bool hasBoneToVertexLink = std::any_of(
        lines.begin(),
        lines.end(),
        [](const PhysicsDebugLine& line)
        {
            return NearlyEqual(line.color, glm::vec3(0.48f, 0.08f, 0.68f));
        }
    );
    Require(
        hasPurpleVertexCross && hasBoneToVertexLink,
        "Vertex fidelity layer did not match the GPU linear-blend skinning transform"
    );

    lines.clear();
    drivenModes[0U] = 0U;
    Require(
        mesh.AppendSkinningDebugLines(
            lines,
            pose,
            drivenModes,
            modelMatrix,
            nullptr,
            8U
        ) == 0U && lines.empty(),
        "Vertex fidelity layer sampled a vertex without physics-driven influence"
    );
}

void TestMmdPhysicsBindAlignmentOverlay()
{
    const glm::mat4 skeletonRootBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(5.0f, 0.0f, 0.0f)
    );
    const glm::mat4 inverseRoot = glm::inverse(skeletonRootBind);
    const glm::mat4 bodyModelBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 2.0f, 0.0f)
    );
    const glm::mat4 boneModelBind = inverseRoot * skeletonRootBind;

    auto model = std::make_unique<ModelAsset>("bindAlignmentOverlay");
    model->SetSkeleton(Skeleton(
        {
            Bone{
                "root",
                InvalidBoneIndex,
                skeletonRootBind,
                glm::mat4(1.0f)
            }
        },
        inverseRoot
    ));

    MmdRigidBodyDefinition body;
    body.name = "bindBody";
    body.bone = 0U;
    body.shape = MmdRigidBodyShape::Box;
    body.size = glm::vec3(0.2f, 0.4f, 0.3f);
    body.mode = MmdRigidBodyMode::FollowBone;
    body.modelBindTransform = bodyModelBind;
    body.boneToBody = glm::inverse(boneModelBind) * bodyModelBind;
    body.bodyToBone = glm::inverse(bodyModelBind) * boneModelBind;
    model->SetMmdPhysics(MmdPhysicsAsset({body}, {}));

    Scene scene;
    Entity& entity = scene.InstantiateModel(*model);
    MmdPhysicsInstance& physics = entity.GetMmdPhysics();
    const MmdPhysicsAlignmentSummary& summary = physics.AlignmentSummary();
    Require(summary.nonIdentityBindSpace, "Bind overlay missed non-identity root space");
    Require(
        summary.maximumSkinningBindError < 0.0001f &&
        summary.maximumBindPositionError < 0.0001f &&
        summary.maximumBindRotationErrorDegrees < 0.001f &&
        summary.maximumBulletPositionError < 0.0001f &&
        summary.maximumBulletRotationErrorDegrees < 0.001f,
        "Bind overlay reported an error for an aligned non-identity root"
    );

    const PhysicsBodyState bindState = physics.BodyStateAt(0U);
    Require(
        NearlyEqual(bindState.position, glm::vec3(0.0f, 2.0f, 0.0f)),
        "MMD body bind transform was not converted into model space"
    );

    const glm::mat4 animatedLocal = skeletonRootBind *
        glm::rotate(
            glm::mat4(1.0f),
            glm::half_pi<float>(),
            glm::vec3(0.0f, 0.0f, 1.0f)
        );
    entity.GetPose().SetLocalMatrix(0U, animatedLocal);
    scene.Update(0.0f);
    const PhysicsBodyState animatedState = physics.BodyStateAt(0U);
    Require(
        glm::distance(animatedState.position, glm::vec3(-2.0f, 0.0f, 0.0f)) < 0.001f,
        "MMD animated rigid-body mapping reused skeleton-root space"
    );

    std::vector<PhysicsDebugLine> lines;
    physics.SetDebugOverlay(MmdPhysicsDebugOverlay::BindPose);
    physics.AppendDebugLines(lines);
    Require(!lines.empty(), "Bind-pose debug overlay produced no lines");
    const bool hasSourceBind = std::any_of(
        lines.begin(),
        lines.end(),
        [](const PhysicsDebugLine& line)
        {
            return NearlyEqual(line.color, glm::vec3(0.0f, 0.95f, 1.0f));
        }
    );
    const bool hasBulletBind = std::any_of(
        lines.begin(),
        lines.end(),
        [](const PhysicsDebugLine& line)
        {
            return NearlyEqual(line.color, glm::vec3(0.15f, 0.45f, 1.0f));
        }
    );
    Require(
        hasSourceBind && hasBulletBind,
        "Bind overlay did not draw both PMX and CreateBody bind poses"
    );

    lines.clear();
    physics.SetDebugOverlay(MmdPhysicsDebugOverlay::ResetPose);
    physics.AppendDebugLines(lines);
    Require(!lines.empty(), "Reset-pose debug overlay produced no lines");
    lines.clear();
    physics.SetDebugOverlay(MmdPhysicsDebugOverlay::Runtime);
    physics.AppendDebugLines(lines);
    Require(!lines.empty(), "Runtime debug overlay produced no lines");
    physics.SetDebugOverlay(MmdPhysicsDebugOverlay::BindPose);
    Require(
        physics.CycleDebugOverlay() == MmdPhysicsDebugOverlay::ResetPose &&
        physics.CycleDebugOverlay() == MmdPhysicsDebugOverlay::Runtime &&
        physics.CycleDebugOverlay() == MmdPhysicsDebugOverlay::All &&
        physics.CycleDebugOverlay() == MmdPhysicsDebugOverlay::Off,
        "MMD debug overlay cycle order changed"
    );
}


std::unique_ptr<ModelAsset> CreateMmdInitializationStabilizationModel(
    bool impossibleKinematicPair
)
{
    auto model = std::make_unique<ModelAsset>(
        impossibleKinematicPair
            ? "impossibleStabilization"
            : "initializationStabilization"
    );
    const glm::mat4 secondBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    model->SetSkeleton(Skeleton({
        Bone{"anchor", InvalidBoneIndex, glm::mat4(1.0f), glm::mat4(1.0f)},
        Bone{"tip", InvalidBoneIndex, secondBind, glm::inverse(secondBind)}
    }));

    std::vector<MmdRigidBodyDefinition> bodies(2U);
    bodies[0U].name = "anchorBody";
    bodies[0U].bone = 0U;
    bodies[0U].shape = MmdRigidBodyShape::Sphere;
    bodies[0U].size = glm::vec3(0.2f, 0.0f, 0.0f);
    bodies[0U].mode = MmdRigidBodyMode::FollowBone;
    bodies[0U].modelBindTransform = glm::mat4(1.0f);
    bodies[0U].boneToBody = glm::mat4(1.0f);
    bodies[0U].bodyToBone = glm::mat4(1.0f);

    bodies[1U].name = "tipBody";
    bodies[1U].bone = 1U;
    bodies[1U].shape = MmdRigidBodyShape::Sphere;
    bodies[1U].size = glm::vec3(0.2f, 0.0f, 0.0f);
    bodies[1U].mass = impossibleKinematicPair ? 0.0f : 1.0f;
    bodies[1U].mode = impossibleKinematicPair
        ? MmdRigidBodyMode::FollowBone
        : MmdRigidBodyMode::PhysicsWithBone;
    bodies[1U].modelBindTransform = secondBind;
    bodies[1U].boneToBody = glm::mat4(1.0f);
    bodies[1U].bodyToBone = glm::mat4(1.0f);

    MmdJointDefinition joint;
    joint.name = "lockedInitializationJoint";
    joint.bodyA = 0U;
    joint.bodyB = 1U;
    joint.position = glm::vec3(0.5f, 0.0f, 0.0f);
    joint.modelBindTransform = glm::translate(
        glm::mat4(1.0f),
        joint.position
    );
    joint.linearLower = glm::vec3(0.0f);
    joint.linearUpper = glm::vec3(0.0f);
    joint.angularLower = glm::vec3(0.0f);
    joint.angularUpper = glm::vec3(0.0f);
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        {joint}
    ));
    return model;
}


std::unique_ptr<ModelAsset> CreateMmdLocalRecoveryModel()
{
    auto model = std::make_unique<ModelAsset>("localRecovery");
    const glm::mat4 anchorABind = glm::mat4(1.0f);
    const glm::mat4 tipABind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    const glm::mat4 anchorBBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(10.0f, 0.0f, 0.0f)
    );
    const glm::mat4 tipBBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(11.0f, 0.0f, 0.0f)
    );
    model->SetSkeleton(Skeleton({
        Bone{"anchorA", InvalidBoneIndex, anchorABind, glm::inverse(anchorABind)},
        Bone{"tipA", InvalidBoneIndex, tipABind, glm::inverse(tipABind)},
        Bone{"anchorB", InvalidBoneIndex, anchorBBind, glm::inverse(anchorBBind)},
        Bone{"tipB", InvalidBoneIndex, tipBBind, glm::inverse(tipBBind)}
    }));

    std::vector<MmdRigidBodyDefinition> bodies(4U);
    const glm::mat4 binds[] = {
        anchorABind, tipABind, anchorBBind, tipBBind
    };
    for (std::size_t index = 0U; index < bodies.size(); ++index)
    {
        bodies[index].name = "recoveryBody" + std::to_string(index);
        bodies[index].bone = static_cast<BoneIndex>(index);
        bodies[index].shape = MmdRigidBodyShape::Sphere;
        bodies[index].size = glm::vec3(0.2f, 0.0f, 0.0f);
        bodies[index].modelBindTransform = binds[index];
        bodies[index].boneToBody = glm::mat4(1.0f);
        bodies[index].bodyToBone = glm::mat4(1.0f);
        const bool anchor = index == 0U || index == 2U;
        bodies[index].mode = anchor
            ? MmdRigidBodyMode::FollowBone
            : MmdRigidBodyMode::PhysicsWithBone;
        bodies[index].mass = anchor ? 0.0f : 1.0f;
    }

    std::vector<MmdJointDefinition> joints(2U);
    for (std::size_t index = 0U; index < joints.size(); ++index)
    {
        const std::size_t base = index * 2U;
        joints[index].name = "recoveryJoint" + std::to_string(index);
        joints[index].bodyA = static_cast<RigidBodyIndex>(base);
        joints[index].bodyB = static_cast<RigidBodyIndex>(base + 1U);
        joints[index].position = glm::vec3(
            index == 0U ? 0.5f : 10.5f,
            0.0f,
            0.0f
        );
        joints[index].modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            joints[index].position
        );
        joints[index].linearLower = glm::vec3(0.0f);
        joints[index].linearUpper = glm::vec3(0.0f);
        joints[index].angularLower = glm::vec3(0.0f);
        joints[index].angularUpper = glm::vec3(0.0f);
    }
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::move(joints)
    ));
    return model;
}

std::unique_ptr<ModelAsset> CreateMmdCollisionTopologyModel()
{
    auto model = std::make_unique<ModelAsset>("collisionTopology");
    std::vector<Bone> bones;
    std::vector<MmdRigidBodyDefinition> bodies(3U);
    for (std::size_t index = 0U; index < bodies.size(); ++index)
    {
        const glm::mat4 bind = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(static_cast<float>(index) * 0.15f, 0.0f, 0.0f)
        );
        bones.push_back(Bone{
            "collisionBone" + std::to_string(index),
            InvalidBoneIndex,
            bind,
            glm::inverse(bind)
        });
        bodies[index].name = "collisionBody" + std::to_string(index);
        bodies[index].bone = static_cast<BoneIndex>(index);
        bodies[index].shape = MmdRigidBodyShape::Box;
        bodies[index].size = glm::vec3(0.2f);
        bodies[index].mode = MmdRigidBodyMode::PhysicsWithBone;
        bodies[index].mass = 1.0f;
        bodies[index].modelBindTransform = bind;
        bodies[index].boneToBody = glm::mat4(1.0f);
        bodies[index].bodyToBone = glm::mat4(1.0f);
    }
    model->SetSkeleton(Skeleton(std::move(bones)));

    std::vector<MmdJointDefinition> joints(2U);
    for (std::size_t index = 0U; index < joints.size(); ++index)
    {
        joints[index].name = "collisionJoint" + std::to_string(index);
        joints[index].bodyA = static_cast<RigidBodyIndex>(index);
        joints[index].bodyB = static_cast<RigidBodyIndex>(index + 1U);
        joints[index].position = glm::vec3(
            static_cast<float>(index) * 0.15f + 0.075f,
            0.0f,
            0.0f
        );
        joints[index].modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            joints[index].position
        );
        joints[index].linearLower = glm::vec3(0.0f);
        joints[index].linearUpper = glm::vec3(0.0f);
        joints[index].angularLower = glm::vec3(0.0f);
        joints[index].angularUpper = glm::vec3(0.0f);
    }
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::move(joints)
    ));
    return model;
}

std::unique_ptr<ModelAsset> CreateMmdGravityBalanceModel()
{
    auto model = std::make_unique<ModelAsset>("gravityBalance");
    constexpr std::size_t BodyCount = 5U;
    std::vector<Bone> bones;
    std::vector<MmdRigidBodyDefinition> bodies(BodyCount);
    for (std::size_t index = 0U; index < BodyCount; ++index)
    {
        const glm::mat4 bind = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(static_cast<float>(index) * 0.12f, 2.0f, 0.0f)
        );
        bones.push_back(Bone{
            "SkirtBone_" + std::to_string(index),
            InvalidBoneIndex,
            bind,
            glm::inverse(bind)
        });
        bodies[index].name = "Skirt_" + std::to_string(index);
        bodies[index].bone = static_cast<BoneIndex>(index);
        bodies[index].collisionGroup = 8U;
        bodies[index].shape = MmdRigidBodyShape::Box;
        bodies[index].size = glm::vec3(0.2f);
        bodies[index].mode = MmdRigidBodyMode::PhysicsWithBone;
        bodies[index].mass = 1.0f;
        bodies[index].linearDamping = 0.01f;
        bodies[index].angularDamping = 0.02f;
        bodies[index].modelBindTransform = bind;
        bodies[index].boneToBody = glm::mat4(1.0f);
        bodies[index].bodyToBone = glm::mat4(1.0f);
    }
    model->SetSkeleton(Skeleton(std::move(bones)));

    std::vector<MmdJointDefinition> joints(BodyCount - 1U);
    for (std::size_t index = 0U; index < joints.size(); ++index)
    {
        joints[index].name = "SkirtJoint_" + std::to_string(index);
        joints[index].bodyA = static_cast<RigidBodyIndex>(index);
        joints[index].bodyB = static_cast<RigidBodyIndex>(index + 1U);
        joints[index].position = glm::vec3(
            static_cast<float>(index) * 0.12f + 0.06f,
            2.0f,
            0.0f
        );
        joints[index].modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            joints[index].position
        );
        joints[index].linearLower = glm::vec3(0.0f);
        joints[index].linearUpper = glm::vec3(0.0f);
        joints[index].angularLower = glm::vec3(-0.2f);
        joints[index].angularUpper = glm::vec3(0.2f);
    }
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::move(joints)
    ));
    return model;
}

std::unique_ptr<ModelAsset> CreateMmdCrossChainContactModel()
{
    auto model = std::make_unique<ModelAsset>("crossChainContact");
    std::vector<Bone> bones;
    std::vector<MmdRigidBodyDefinition> bodies(2U);
    const glm::vec3 positions[] = {
        glm::vec3(-0.1f, 0.0f, 0.0f),
        glm::vec3(0.1f, 0.0f, 0.0f)
    };
    for (std::size_t index = 0U; index < bodies.size(); ++index)
    {
        const glm::mat4 bind = glm::translate(
            glm::mat4(1.0f),
            positions[index]
        );
        bones.push_back(Bone{
            "crossBone" + std::to_string(index),
            InvalidBoneIndex,
            bind,
            glm::inverse(bind)
        });
        bodies[index].name = "crossBody" + std::to_string(index);
        bodies[index].bone = static_cast<BoneIndex>(index);
        bodies[index].shape = MmdRigidBodyShape::Sphere;
        bodies[index].size = glm::vec3(0.5f, 0.0f, 0.0f);
        bodies[index].mode = MmdRigidBodyMode::PhysicsWithBone;
        bodies[index].mass = 1.0f;
        bodies[index].modelBindTransform = bind;
        bodies[index].boneToBody = glm::mat4(1.0f);
        bodies[index].bodyToBone = glm::mat4(1.0f);
    }
    model->SetSkeleton(Skeleton(std::move(bones)));
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::vector<MmdJointDefinition>{}
    ));
    return model;
}


std::unique_ptr<ModelAsset> CreateMmdP13SemanticModel()
{
    constexpr std::size_t BodyCount = 13U;
    auto model = std::make_unique<ModelAsset>("p13Semantic");
    std::vector<Bone> bones;
    std::vector<MmdRigidBodyDefinition> bodies(BodyCount);
    for (std::size_t index = 0U; index < BodyCount; ++index)
    {
        const glm::mat4 bind = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(static_cast<float>(index) * 0.25f, 1.0f, 0.0f)
        );
        bones.push_back(Bone{
            "mysteryBone" + std::to_string(index),
            InvalidBoneIndex,
            bind,
            glm::inverse(bind)
        });
        MmdRigidBodyDefinition& body = bodies[index];
        body.name = "mysteryBody" + std::to_string(index);
        body.bone = static_cast<BoneIndex>(index);
        body.shape = MmdRigidBodyShape::Box;
        body.size = glm::vec3(0.12f);
        body.mode = index == 0U
            ? MmdRigidBodyMode::FollowBone
            : MmdRigidBodyMode::PhysicsWithBone;
        body.mass = index == 0U ? 0.0f : 1.0f;
        body.modelBindTransform = bind;
        body.boneToBody = glm::mat4(1.0f);
        body.bodyToBone = glm::mat4(1.0f);
    }
    model->SetSkeleton(Skeleton(std::move(bones)));

    std::vector<MmdJointDefinition> joints(BodyCount - 1U);
    for (std::size_t index = 0U; index < joints.size(); ++index)
    {
        MmdJointDefinition& joint = joints[index];
        joint.name = "mysteryJoint" + std::to_string(index);
        joint.bodyA = static_cast<RigidBodyIndex>(index);
        joint.bodyB = static_cast<RigidBodyIndex>(index + 1U);
        joint.position = glm::vec3(
            static_cast<float>(index) * 0.25f + 0.125f,
            1.0f,
            0.0f
        );
        joint.modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            joint.position
        );
        joint.linearLower = glm::vec3(0.0f);
        joint.linearUpper = glm::vec3(0.0f);
        joint.angularLower = glm::vec3(-0.4f);
        joint.angularUpper = glm::vec3(0.4f);
    }
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::move(joints)
    ));
    return model;
}

std::unique_ptr<ModelAsset> CreateMmdP13SkirtSemanticModel()
{
    constexpr std::size_t BodyCount = 6U;
    auto model = std::make_unique<ModelAsset>("p13SkirtSemantic");
    std::vector<Bone> bones;
    std::vector<MmdRigidBodyDefinition> bodies(BodyCount);
    const char* names[] = {
        "Skirt_0_1", "Skirt_10_2", "Skirt_11_2",
        "Skirt_12_2", "Skirt_13_2", "Skirt_2_1B"
    };
    for (std::size_t index = 0U; index < BodyCount; ++index)
    {
        const glm::mat4 bind = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(static_cast<float>(index) * 0.05f, 2.0f, 0.0f)
        );
        bones.push_back(Bone{
            "semanticSkirtBone" + std::to_string(index),
            InvalidBoneIndex,
            bind,
            glm::inverse(bind)
        });
        MmdRigidBodyDefinition& body = bodies[index];
        body.name = names[index];
        body.bone = static_cast<BoneIndex>(index);
        body.collisionGroup = 8U;
        body.shape = MmdRigidBodyShape::Box;
        body.size = glm::vec3(0.10f);
        body.mode = MmdRigidBodyMode::PhysicsWithBone;
        body.mass = 1.0f;
        body.modelBindTransform = bind;
        body.boneToBody = glm::mat4(1.0f);
        body.bodyToBone = glm::mat4(1.0f);
    }
    model->SetSkeleton(Skeleton(std::move(bones)));

    std::vector<MmdJointDefinition> joints(BodyCount - 1U);
    for (std::size_t index = 0U; index < joints.size(); ++index)
    {
        MmdJointDefinition& joint = joints[index];
        joint.name = "semanticSkirtJoint" + std::to_string(index);
        joint.bodyA = static_cast<RigidBodyIndex>(index);
        joint.bodyB = static_cast<RigidBodyIndex>(index + 1U);
        joint.position = glm::vec3(
            static_cast<float>(index) * 0.05f + 0.025f,
            2.0f,
            0.0f
        );
        joint.modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            joint.position
        );
        joint.linearLower = glm::vec3(0.0f);
        joint.linearUpper = glm::vec3(0.0f);
        joint.angularLower = glm::vec3(-0.2f);
        joint.angularUpper = glm::vec3(0.2f);
    }
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::move(joints)
    ));
    return model;
}

void TestMmdP13ChainSemanticsAndAnchorDiagnostics()
{
    {
        std::unique_ptr<ModelAsset> model = CreateMmdP13SkirtSemanticModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        Require(
            physics.GravityStatistics().skirtSemanticIgnoredPairCount >= 1U,
            "P1.3 did not identify a skirt main/auxiliary semantic conflict"
        );
        scene.Update(1.0f / 60.0f);
        bool forbiddenPairVisible = false;
        for (const MmdPhysicsContactDiagnostic& contact :
             physics.ContactDiagnostics())
        {
            const bool pair05 =
                (contact.bodyAIndex == 0U && contact.bodyBIndex == 5U) ||
                (contact.bodyAIndex == 5U && contact.bodyBIndex == 0U);
            forbiddenPairVisible = forbiddenPairVisible || pair05;
        }
        Require(
            !forbiddenPairVisible,
            "P1.3 skirt semantic conflict still reached Bullet contacts"
        );
    }

    {
        std::unique_ptr<ModelAsset> model = CreateMmdP13SemanticModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        const auto initialChains = physics.ChainBalanceStatistics();
        Require(
            initialChains.size() == 1U &&
            initialChains.front().kind ==
                MmdPhysicsChainKind::DecorativeFallback,
            "P1.3 did not classify an unnamed anchored long chain safely"
        );
        const PhysicsBodyRuntimeSettings settings =
            scene.Physics().RuntimeSettings(physics.BodyHandleAt(6U));
        Require(
            settings.linearDamping >= 0.18f &&
            settings.angularDamping >= 0.28f,
            "P1.3 decorative fallback did not receive minimum damping"
        );

        scene.Update(0.0f);
        scene.Physics().ApplyCentralImpulse(
            physics.BodyHandleAt(12U),
            glm::vec3(12.0f, 0.0f, 0.0f)
        );
        scene.Update(1.0f / 60.0f);
        const auto chains = physics.ChainBalanceStatistics();
        Require(
            chains.front().anchorBodyIndex == 0U &&
            chains.front().maximumAnchorDistance > 0.0f &&
            chains.front().maximumNormalizedExtension > 1.0f &&
            chains.front().totalConstraintImpulse > 0.0f &&
            chains.front().maximumConstraintImpulse > 0.0f,
            "P1.3 anchor distance or constraint impulse diagnostics were empty"
        );
    }

    {
        std::unique_ptr<ModelAsset> model = CreateMmdP13SemanticModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        scene.Update(0.0f);
        MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        const glm::quat quarterTurn = glm::angleAxis(
            glm::half_pi<float>(),
            glm::vec3(0.0f, 0.0f, 1.0f)
        );
        for (std::size_t index = 0U; index < 13U; ++index)
        {
            scene.Physics().SetTransform(
                physics.BodyHandleAt(static_cast<RigidBodyIndex>(index)),
                quarterTurn * glm::vec3(
                    static_cast<float>(index) * 0.25f,
                    1.0f,
                    0.0f
                ),
                quarterTurn,
                true
            );
        }
        const std::size_t recoveriesBefore =
            physics.RecoveryStatistics().totalRecoveries;
        for (int tick = 0; tick < 40; ++tick)
            physics.ObserveSimulationSubstep(1.0f / 60.0f);
        Require(
            physics.RecoveryStatistics().totalRecoveries == recoveriesBefore,
            "P1.3 treated a rigidly rotated, length-preserving chain as runaway"
        );
    }
}

void TestMmdGravityConstraintBalance()
{
    std::unique_ptr<ModelAsset> model = CreateMmdGravityBalanceModel();
    Scene scene;
    Entity& entity = scene.InstantiateModel(*model);
    MmdPhysicsInstance& physics = entity.GetMmdPhysics();

    Require(
        physics.GravityMode() == MmdPhysicsGravityMode::Balanced100 &&
        std::string(physics.GravityModeName()) == "BALANCED_1.00G",
        "MMD gravity balance did not default to the balanced 1g profile"
    );
    const PhysicsBodyRuntimeSettings balanced = scene.Physics().RuntimeSettings(
        physics.BodyHandleAt(2U)
    );
    Require(
        balanced.gravityOverride &&
        glm::distance(balanced.gravity, glm::vec3(0.0f, -5.39f, 0.0f)) < 0.01f &&
        balanced.linearDamping >= 0.25f &&
        balanced.angularDamping >= 0.35f,
        "Skirt gravity and damping profile was not applied"
    );
    Require(
        physics.GravityStatistics().skirtLayerIgnoredPairCount >= 1U &&
        physics.CollisionStatistics().ignoredNearNeighborPairCount >= 3U,
        "Layered skirt self-collision filtering was not configured"
    );
    const auto chains = physics.ChainBalanceStatistics();
    Require(
        chains.size() == 1U &&
        chains.front().kind == MmdPhysicsChainKind::Skirt &&
        chains.front().dynamicBodyCount == 5U,
        "MMD skirt bodies were not grouped into a gravity balance chain"
    );

    physics.SetGravityMode(MmdPhysicsGravityMode::Balanced050);
    const PhysicsBodyRuntimeSettings halfGravity = scene.Physics().RuntimeSettings(
        physics.BodyHandleAt(2U)
    );
    Require(
        halfGravity.gravityOverride &&
        glm::distance(halfGravity.gravity, glm::vec3(0.0f, -2.695f, 0.0f)) < 0.01f,
        "Global gravity A/B scale did not combine with the skirt profile"
    );

    physics.SetGravityMode(MmdPhysicsGravityMode::Zero);
    Require(
        NearlyEqual(
            scene.Physics().RuntimeSettings(physics.BodyHandleAt(2U)).gravity,
            glm::vec3(0.0f)
        ),
        "Zero-gravity A/B mode did not remove decorative body gravity"
    );

    physics.SetGravityMode(MmdPhysicsGravityMode::Original);
    const PhysicsBodyRuntimeSettings original = scene.Physics().RuntimeSettings(
        physics.BodyHandleAt(2U)
    );
    Require(
        !original.gravityOverride &&
        NearlyEqual(original.gravity, glm::vec3(0.0f, -9.8f, 0.0f)) &&
        NearlyEqual(original.linearDamping, 0.01f) &&
        NearlyEqual(original.angularDamping, 0.02f),
        "Original mode did not restore PMX gravity and damping"
    );

    physics.SetGravityMode(MmdPhysicsGravityMode::Balanced075);
    scene.Update(1.0f / 60.0f);
    Require(
        physics.GravityStatistics().dynamicBodyCount == 5U &&
        physics.GravityStatistics().averageEffectiveGravityScale > 0.40f &&
        physics.GravityStatistics().averageEffectiveGravityScale < 0.42f &&
        physics.GravityStatistics().maximumSpeed >= 0.0f,
        "Gravity, displacement and speed statistics were not updated"
    );
    physics.LogGravityReport();
}

void TestMmdCollisionTopologyAndAdaptiveCcd()
{
    {
        std::unique_ptr<ModelAsset> model = CreateMmdCollisionTopologyModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        const MmdPhysicsCollisionStatistics initial =
            physics.CollisionStatistics();
        Require(
            initial.linkedJointPairCount == 2U &&
            initial.ignoredNearNeighborPairCount == 1U &&
            initial.denseMarginBodyCount == 1U &&
            initial.ccdCandidateCount == 3U,
            "MMD collision topology policy did not classify the dense chain"
        );
        Require(
            scene.Physics().RuntimeSettings(physics.BodyHandleAt(1U)).collisionMargin <
                scene.Physics().RuntimeSettings(physics.BodyHandleAt(0U)).collisionMargin,
            "Dense MMD chain body did not receive its reduced box margin"
        );

        // Consume the first-frame constraint-preserving reset before injecting
        // the speed used to exercise adaptive CCD.
        scene.Update(0.0f);
        scene.Physics().SetLinearVelocity(
            physics.BodyHandleAt(2U),
            glm::vec3(20.0f, 0.0f, 0.0f)
        );
        scene.Update(1.0f / 60.0f);
        Require(
            scene.Physics().RuntimeSettings(physics.BodyHandleAt(2U)).ccdEnabled &&
            physics.CollisionStatistics().activeCcdBodyCount >= 1U &&
            physics.CollisionStatistics().ccdActivationCount >= 1U,
            "MMD adaptive CCD did not activate for actual high per-tick travel"
        );
        Require(
            physics.CollisionStatistics().contactPairCount == 0U,
            "MMD near-neighbor filtering left an internal A-C contact active"
        );
    }

    {
        std::unique_ptr<ModelAsset> model = CreateMmdCrossChainContactModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        scene.Update(1.0f / 60.0f);
        const MmdPhysicsCollisionStatistics& collision =
            entity.GetMmdPhysics().CollisionStatistics();
        Require(
            collision.contactPairCount >= 1U &&
            collision.crossChainContactPairCount >= 1U &&
            collision.maximumPenetrationDepth > 0.0f &&
            !entity.GetMmdPhysics().ContactDiagnostics().empty(),
            "MMD cross-chain contact matrix did not capture an allowed contact"
        );
    }
}

void TestMmdPhysicsP1LocalRecovery()
{
    std::unique_ptr<ModelAsset> model = CreateMmdLocalRecoveryModel();
    Scene scene;
    scene.Physics().SetGravity(glm::vec3(0.0f));
    Entity& entity = scene.InstantiateModel(*model);
    scene.Update(0.0f);

    MmdPhysicsInstance& physics = entity.GetMmdPhysics();
    Require(
        physics.RecoveryStatistics().chainCount == 2U,
        "MMD local recovery did not separate independent physics chains"
    );
    Require(
        scene.Physics().Statistics().ccdBodyCount == 0U &&
        physics.CollisionStatistics().ccdCandidateCount == 2U &&
        !scene.Physics().RuntimeSettings(physics.BodyHandleAt(1U)).ccdEnabled &&
        !scene.Physics().RuntimeSettings(physics.BodyHandleAt(3U)).ccdEnabled,
        "MMD adaptive CCD candidates were not kept inactive at rest"
    );

    const PhysicsBodyState unaffectedBefore = physics.BodyStateAt(3U);
    scene.Physics().SetTransform(
        physics.BodyHandleAt(1U),
        glm::vec3(100.0f, 0.0f, 0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        true
    );
    for (int frame = 0; frame < 120; ++frame)
        scene.Update(0.0f);
    Require(
        physics.RecoveryStatistics().totalRecoveries == 0U &&
        physics.RecoveryStatistics().physicsTickCount == 0U,
        "MMD recovery advanced during render frames with no physics tick"
    );
    scene.Update(1.0f / 60.0f);

    const MmdPhysicsRecoveryStatistics& recovery =
        physics.RecoveryStatistics();
    Require(
        recovery.totalRecoveries == 1U &&
        recovery.physicsTickCount == 1U &&
        recovery.lastRecoveredBodyCount == 2U &&
        (recovery.lastReason == MmdPhysicsRecoveryReason::Runaway ||
            recovery.lastReason ==
                MmdPhysicsRecoveryReason::ExtremeVelocity) &&
        !physics.StabilizationFailed(),
        "Abnormal MMD chain did not recover on the real physics tick"
    );
    Require(
        glm::distance(
            physics.BodyStateAt(1U).position,
            glm::vec3(1.0f, 0.0f, 0.0f)
        ) < 0.05f,
        "Recovered MMD chain did not return to its animation-aligned target"
    );
    Require(
        glm::distance(
            physics.BodyStateAt(3U).position,
            unaffectedBefore.position
        ) < 0.01f,
        "Local recovery disturbed an independent MMD physics chain"
    );
}


std::unique_ptr<ModelAsset> CreateMmdLongRecoveryChainModel()
{
    constexpr std::size_t BodyCount = 17U;
    auto model = std::make_unique<ModelAsset>("longRecoveryChain");
    std::vector<Bone> bones;
    std::vector<MmdRigidBodyDefinition> bodies(BodyCount);
    bones.reserve(BodyCount);
    for (std::size_t index = 0U; index < BodyCount; ++index)
    {
        const glm::mat4 bind = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(static_cast<float>(index), 0.0f, 0.0f)
        );
        bones.push_back(Bone{
            "longRecoveryBone" + std::to_string(index),
            InvalidBoneIndex,
            bind,
            glm::inverse(bind)
        });
        MmdRigidBodyDefinition& body = bodies[index];
        body.name = "longRecoveryBody" + std::to_string(index);
        body.bone = static_cast<BoneIndex>(index);
        body.shape = MmdRigidBodyShape::Sphere;
        body.size = glm::vec3(0.2f, 0.0f, 0.0f);
        body.modelBindTransform = bind;
        body.boneToBody = glm::mat4(1.0f);
        body.bodyToBone = glm::mat4(1.0f);
        body.mode = index == 0U
            ? MmdRigidBodyMode::FollowBone
            : MmdRigidBodyMode::PhysicsWithBone;
        body.mass = index == 0U ? 0.0f : 1.0f;
    }
    model->SetSkeleton(Skeleton(std::move(bones)));

    std::vector<MmdJointDefinition> joints(BodyCount - 1U);
    for (std::size_t index = 0U; index < joints.size(); ++index)
    {
        MmdJointDefinition& joint = joints[index];
        joint.name = "longRecoveryJoint" + std::to_string(index);
        joint.bodyA = static_cast<RigidBodyIndex>(index);
        joint.bodyB = static_cast<RigidBodyIndex>(index + 1U);
        joint.position = glm::vec3(
            static_cast<float>(index) + 0.5f,
            0.0f,
            0.0f
        );
        joint.modelBindTransform = glm::translate(
            glm::mat4(1.0f),
            joint.position
        );
        joint.linearLower = glm::vec3(0.0f);
        joint.linearUpper = glm::vec3(0.0f);
        joint.angularLower = glm::vec3(0.0f);
        joint.angularUpper = glm::vec3(0.0f);
    }
    model->SetMmdPhysics(MmdPhysicsAsset(
        std::move(bodies),
        std::move(joints)
    ));
    return model;
}

void TestMmdPhysicsP11LocalizedRegionAndFuse()
{
    {
        std::unique_ptr<ModelAsset> model = CreateMmdLocalRecoveryModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        scene.Update(0.0f);
        MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        scene.Physics().SetTransform(
            physics.BodyHandleAt(1U),
            glm::vec3(3.0f, 0.0f, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            true
        );
        constexpr float Tick = 1.0f / 60.0f;
        for (int tick = 0; tick < 20; ++tick)
            physics.ObserveSimulationSubstep(Tick);
        Require(
            physics.RecoveryStatistics().totalRecoveries == 0U &&
            physics.RecoveryStatistics().pendingAbnormalChainCount == 1U,
            "Finite joint error recovered before its physical-time threshold"
        );
        for (int tick = 0; tick < 10; ++tick)
            physics.ObserveSimulationSubstep(Tick);
        Require(
            physics.RecoveryStatistics().totalRecoveries == 1U &&
            physics.RecoveryStatistics().lastReason ==
                MmdPhysicsRecoveryReason::JointViolation &&
            physics.RecoveryStatistics().lastAbnormalSeconds >= 0.44f,
            "Sustained joint error did not recover after physical-time gating"
        );
    }

    {
        std::unique_ptr<ModelAsset> model =
            CreateMmdLongRecoveryChainModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        scene.Update(0.0f);
        MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        Require(
            physics.RecoveryStatistics().chainCount == 1U,
            "Long MMD recovery fixture was not one coarse chain"
        );
        const PhysicsBodyState farBodyBefore = physics.BodyStateAt(16U);
        scene.Physics().SetTransform(
            physics.BodyHandleAt(8U),
            glm::vec3(100.0f, 0.0f, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            true
        );
        physics.ObserveSimulationSubstep(1.0f / 60.0f);
        const MmdPhysicsRecoveryStatistics& recovery =
            physics.RecoveryStatistics();
        Require(
            recovery.totalRecoveries == 1U &&
            recovery.lastRecoveredBodyCount <= 9U &&
            recovery.lastRecoveredBodyCount < 17U &&
            recovery.largestRecoveryRegion ==
                recovery.lastRecoveredBodyCount,
            "MMD P1.1 recovery did not stay inside the local graph radius"
        );
        Require(
            glm::distance(
                physics.BodyStateAt(16U).position,
                farBodyBefore.position
            ) < 0.001f,
            "Localized MMD recovery modified a distant body in the same chain"
        );
    }

    {
        std::unique_ptr<ModelAsset> model = CreateMmdLocalRecoveryModel();
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        scene.Update(0.0f);
        MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        constexpr float Tick = 1.0f / 60.0f;
        const auto runHighVelocityEpisode = [&]()
        {
            scene.Physics().SetLinearVelocity(
                physics.BodyHandleAt(1U),
                glm::vec3(60.0f, 0.0f, 0.0f)
            );
            for (int tick = 0; tick < 20; ++tick)
                physics.ObserveSimulationSubstep(Tick);
        };
        const auto clearCooldown = [&]()
        {
            for (int tick = 0; tick < 190; ++tick)
                physics.ObserveSimulationSubstep(Tick);
        };

        runHighVelocityEpisode();
        clearCooldown();
        runHighVelocityEpisode();
        clearCooldown();
        runHighVelocityEpisode();
        Require(
            physics.RecoveryStatistics().totalRecoveries == 3U &&
            physics.RecoveryStatistics().totalFuseTrips == 1U &&
            physics.RecoveryStatistics().fusedChainCount == 1U,
            "MMD recovery fuse did not trip after repeated local resets"
        );

        runHighVelocityEpisode();
        Require(
            physics.RecoveryStatistics().totalRecoveries == 3U &&
            physics.RecoveryStatistics().suppressedRecoveryCount == 1U,
            "MMD recovery fuse did not suppress the next repeated reset"
        );
    }
}

void TestMmdPhysicsInitializationStabilization()
{
    {
        std::unique_ptr<ModelAsset> model =
            CreateMmdInitializationStabilizationModel(false);
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        entity.GetPose().SetLocalMatrix(
            0U,
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(2.0f, 0.0f, 0.0f)
            )
        );
        scene.Update(0.0f);

        const PhysicsBodyState anchor =
            entity.GetMmdPhysics().BodyStateAt(0U);
        const PhysicsBodyState tip =
            entity.GetMmdPhysics().BodyStateAt(1U);
        const glm::vec3 anchorJoint = anchor.position +
            anchor.rotation * glm::vec3(0.5f, 0.0f, 0.0f);
        const glm::vec3 tipJoint = tip.position +
            tip.rotation * glm::vec3(-0.5f, 0.0f, 0.0f);
        Require(
            glm::distance(anchorJoint, tipJoint) < 0.2f,
            "Hidden MMD warmup did not settle a reset joint chain"
        );
        Require(
            entity.GetMmdPhysics().PendingStabilizationSteps() == 0U &&
            !entity.GetMmdPhysics().StabilizationFailed(),
            "Converged MMD initialization remained pending or failed"
        );
    }

    {
        std::unique_ptr<ModelAsset> model =
            CreateMmdInitializationStabilizationModel(true);
        Scene scene;
        scene.Physics().SetGravity(glm::vec3(0.0f));
        Entity& entity = scene.InstantiateModel(*model);
        entity.GetPose().SetLocalMatrix(
            0U,
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(10.0f, 0.0f, 0.0f)
            )
        );
        scene.Update(0.0f);
        Require(
            entity.GetMmdPhysics().StabilizationFailed(),
            "Non-convergent MMD initialization did not enter safe freeze"
        );
    }
}


void TestMmdPhysics2BSpringAndLifecycle()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics2BSpringModel();
    Scene scene;
    Entity& first = scene.InstantiateModel(*model);
    Entity& second = scene.InstantiateModel(
        *model,
        Transform(glm::vec3(4.0f, 0.0f, 0.0f))
    );
    Require(
        first.GetMmdPhysics().ConstraintCount() == 1U &&
        second.GetMmdPhysics().ConstraintCount() == 1U &&
        scene.Physics().BodyCount() == 4U &&
        scene.Physics().ConstraintCount() == 2U,
        "MMD physics instances did not create isolated bodies and joints"
    );

    scene.Physics().SetGravity(glm::vec3(0.0f));
    scene.Physics().ApplyCentralImpulse(
        first.GetMmdPhysics().BodyHandleAt(0U),
        glm::vec3(-4.0f, 0.0f, 0.0f)
    );
    scene.Physics().ApplyCentralImpulse(
        first.GetMmdPhysics().BodyHandleAt(1U),
        glm::vec3(4.0f, 0.0f, 0.0f)
    );
    for (int frame = 0; frame < 120; ++frame)
        scene.Update(1.0f / 60.0f);
    const float firstDistance = glm::distance(
        first.GetMmdPhysics().BodyStateAt(0U).position,
        first.GetMmdPhysics().BodyStateAt(1U).position
    );
    Require(
        std::abs(firstDistance - 1.0f) < 0.2f,
        "MMD Spring 6DOF failed to constrain its rigid bodies"
    );
    Require(
        std::abs(second.GetMmdPhysics().BodyStateAt(0U).position.x - 3.5f) < 0.1f,
        "One MMD physics instance contaminated another instance"
    );

    Require(scene.RemoveEntity(first), "Scene failed to remove MMD physics Entity");
    Require(
        scene.Physics().BodyCount() == 2U &&
        scene.Physics().ConstraintCount() == 1U,
        "Removing an Entity did not unregister its Bullet runtime"
    );
}

void TestMmdPhysics2BScaleValidation()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics2BModeModel();
    Scene scene;
    bool rejected = false;
    try
    {
        scene.InstantiateModel(
            *model,
            Transform(
                glm::vec3(0.0f),
                glm::vec3(0.0f),
                glm::vec3(1.0f, 2.0f, 1.0f)
            )
        );
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(
        rejected && scene.Physics().BodyCount() == 0U,
        "MMD physics accepted non-uniform Entity scale or leaked bodies"
    );
}


void TestDemoPmxPhysics2BRuntimeWhenAvailable()
{
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    if (!std::filesystem::is_regular_file(modelPath))
        return;

    ImportedModelData imported;
    try
    {
        imported = ModelImporter().Import(modelPath);
    }
    catch (const std::runtime_error& error)
    {
        if (std::string(error.what()).find("texture was not found") !=
            std::string::npos)
        {
            return;
        }
        throw;
    }
    Require(
        imported.skeleton.has_value() && imported.mmdPhysics.has_value(),
        "Demo PMX lost the data required by Physics 2B"
    );
    const std::size_t expectedConstraints = static_cast<std::size_t>(
        std::count_if(
            imported.mmdPhysics->Joints().begin(),
            imported.mmdPhysics->Joints().end(),
            [](const MmdJointDefinition& joint)
            {
                return joint.bodyA == InvalidRigidBodyIndex ||
                    joint.bodyA != joint.bodyB;
            }
        )
    );
    std::array<std::size_t, 3U> modeCounts{};
    std::vector<RigidBodyIndex> physicsWithBoneIndices;
    for (RigidBodyIndex index = 0U;
         index < imported.mmdPhysics->RigidBodyCount();
         ++index)
    {
        const MmdRigidBodyMode mode =
            imported.mmdPhysics->RigidBodyAt(index).mode;
        ++modeCounts[static_cast<std::size_t>(mode)];
        if (mode == MmdRigidBodyMode::PhysicsWithBone)
            physicsWithBoneIndices.push_back(index);
    }
    Require(
        modeCounts == std::array<std::size_t, 3U>{38U, 74U, 383U},
        "Demo PMX rigid-body mode distribution changed"
    );
    ModelAsset model("demoPhysics2B");
    model.SetSkeleton(std::move(*imported.skeleton));
    model.SetMmdPhysics(std::move(*imported.mmdPhysics));

    Scene scene;
    Entity& entity = scene.InstantiateModel(model);
    Require(
        entity.GetMmdPhysics().RigidBodyCount() == 495U &&
        entity.GetMmdPhysics().ConstraintCount() == expectedConstraints &&
        scene.Physics().BodyCount() == 495U &&
        scene.Physics().ConstraintCount() == expectedConstraints,
        "Demo PMX did not create its complete supported Bullet runtime"
    );
    std::vector<glm::vec3> physicsWithBoneStartPositions;
    physicsWithBoneStartPositions.reserve(physicsWithBoneIndices.size());
    for (RigidBodyIndex index : physicsWithBoneIndices)
    {
        physicsWithBoneStartPositions.push_back(
            entity.GetMmdPhysics().BodyStateAt(index).position
        );
    }

    for (int frame = 0; frame < 60; ++frame)
        scene.Update(1.0f / 60.0f);

    std::size_t movedPhysicsWithBoneCount = 0U;
    for (std::size_t sample = 0U;
         sample < physicsWithBoneIndices.size();
         ++sample)
    {
        const PhysicsBodyState state = entity.GetMmdPhysics().BodyStateAt(
            physicsWithBoneIndices[sample]
        );
        if (glm::distance(
                state.position,
                physicsWithBoneStartPositions[sample]
            ) > 0.002f)
        {
            ++movedPhysicsWithBoneCount;
        }
    }
    Require(
        movedPhysicsWithBoneCount > 0U,
        "Demo PMX PhysicsWithBone bodies were prevented from translating"
    );

    for (RigidBodyIndex index = 0U; index < 495U; index += 47U)
    {
        const PhysicsBodyState state = entity.GetMmdPhysics().BodyStateAt(index);
        Require(
            std::isfinite(state.position.x) &&
            std::isfinite(state.position.y) &&
            std::isfinite(state.position.z) &&
            std::isfinite(state.rotation.w),
            "Demo PMX Bullet runtime produced non-finite state"
        );
    }
    Require(scene.RemoveEntity(entity), "Failed to remove demo Physics 2B Entity");
    Require(
        scene.Physics().BodyCount() == 0U &&
        scene.Physics().ConstraintCount() == 0U,
        "Demo Physics 2B runtime leaked Bullet objects"
    );
}



void TestDemoPmxVmdInitializationStabilizationWhenAvailable()
{
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"皮卡皮卡皮卡丘+" /
        u8"身体动作.vmd";
    if (!std::filesystem::is_regular_file(motionPath))
    {
        motionPath = ProjectAssetDirectory / "motions" /
            "#U76ae#U5361#U76ae#U5361#U76ae#U5361#U4e18+" /
            "#U8eab#U4f53#U52a8#U4f5c.vmd";
    }
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

    ImportedModelData imported;
    try
    {
        imported = ModelImporter().Import(modelPath);
    }
    catch (const std::runtime_error& error)
    {
        if (std::string(error.what()).find("texture was not found") !=
            std::string::npos)
        {
            return;
        }
        throw;
    }
    Require(
        imported.skeleton.has_value() && imported.mmdPhysics.has_value(),
        "Demo PMX/VMD stabilization source lost skeleton or physics"
    );

    ModelAsset model("demoVmdInitialization");
    model.SetSkeleton(std::move(*imported.skeleton));
    if (!imported.morphs.empty())
        model.SetMorphs(std::move(imported.morphs));
    model.SetMmdPhysics(std::move(*imported.mmdPhysics));
    const MorphSet* morphs = model.HasMorphs() ? &model.GetMorphSet() : nullptr;
    ImportedVmdAnimationData motion = VmdImporter().Import(
        motionPath,
        model.GetSkeleton(),
        VmdImportOptions{.clipName = "initializationMotion"},
        morphs
    );
    model.AddAnimationClip(std::move(motion.clip));

    Scene scene;
    Entity& entity = scene.InstantiateModel(model);
    scene.Update(0.0f);
    entity.GetMmdPhysics().LogAlignmentReport(4U);
    Require(
        entity.GetMmdPhysics().PendingStabilizationSteps() == 0U &&
        !entity.GetMmdPhysics().StabilizationFailed(),
        "Demo PMX/VMD initialization remained pending or entered safe freeze"
    );

    const int longRunFrames = std::getenv("WISTERIA_SANITIZER_SMOKE") != nullptr
        ? 60
        : 720;
    for (int frame = 0; frame < longRunFrames; ++frame)
    {
        scene.Update(1.0f / 60.0f);
        for (RigidBodyIndex index = 0U;
             index < entity.GetMmdPhysics().RigidBodyCount();
             ++index)
        {
            const PhysicsBodyState state =
                entity.GetMmdPhysics().BodyStateAt(index);
            Require(
                std::isfinite(state.position.x) &&
                std::isfinite(state.position.y) &&
                std::isfinite(state.position.z) &&
                std::isfinite(state.rotation.w) &&
                std::isfinite(state.rotation.x) &&
                std::isfinite(state.rotation.y) &&
                std::isfinite(state.rotation.z),
                "Demo PMX/VMD long-run produced non-finite Bullet state"
            );
        }
        Require(
            !entity.GetMmdPhysics().StabilizationFailed(),
            "Demo PMX/VMD long-run entered safe freeze"
        );
    }
    entity.GetMmdPhysics().LogAlignmentReport(8U);
}

void TestP0Bullet275CompatibilityLongRunWhenAvailable()
{
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"皮卡皮卡皮卡丘+" /
        u8"身体动作.vmd";
    if (!std::filesystem::is_regular_file(motionPath))
    {
        motionPath = ProjectAssetDirectory / "motions" /
            "#U76ae#U5361#U76ae#U5361#U76ae#U5361#U4e18+" /
            "#U8eab#U4f53#U52a8#U4f5c.vmd";
    }
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

    ImportedModelData imported;
    try
    {
        imported = ModelImporter().Import(modelPath);
    }
    catch (const std::runtime_error& error)
    {
        if (std::string(error.what()).find("texture was not found") !=
            std::string::npos)
        {
            return;
        }
        throw;
    }
    Require(
        imported.skeleton.has_value() && imported.mmdPhysics.has_value(),
        "P0 A/B source lost skeleton or physics"
    );

    ModelAsset model("p0Bullet275Compatibility");
    model.SetSkeleton(std::move(*imported.skeleton));
    if (!imported.morphs.empty())
        model.SetMorphs(std::move(imported.morphs));
    model.SetMmdPhysics(std::move(*imported.mmdPhysics));
    const MorphSet* morphs = model.HasMorphs() ? &model.GetMorphSet() : nullptr;
    ImportedVmdAnimationData motion = VmdImporter().Import(
        motionPath,
        model.GetSkeleton(),
        VmdImportOptions{.clipName = "p0Motion"},
        morphs
    );
    model.AddAnimationClip(std::move(motion.clip));

    struct ArmResult
    {
        std::string name;
        MmdPhysicsInstance::MmdRuntimeJointDiagnostics diagnostics{};
        std::size_t recoveries = 0U;
        float maximumMode2TranslationDelta = 0.0f;
        bool finite = true;
        bool stabilized = true;
    };
    std::vector<ArmResult> results;

    const auto runArm = [&model, &results](
        std::string name,
        MmdPhysicsRuntimePolicy policy,
        bool raw
    )
    {
        if (raw)
        {
            policy.recovery.enabled = false;
            policy.enableChainProfiles = false;
            policy.collision.enableNearNeighborFiltering = false;
            policy.collision.enableSkirtSemanticFiltering = false;
            policy.ccd.adaptive = false;
        }

        Scene scene;
        Entity& entity = scene.InstantiateModel(
            model,
            {},
            ModelInstantiationOptions{.enablePhysics = false}
        );
        entity.SetMmdPhysics(scene.Physics(), model.GetMmdPhysics(), policy);
        scene.Update(0.0f);

        const MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        ArmResult result;
        result.name = std::move(name);
        result.stabilized =
            physics.PendingStabilizationSteps() == 0U &&
            !physics.StabilizationFailed();

        const int longRunFrames =
            std::getenv("WISTERIA_SANITIZER_SMOKE") != nullptr ? 60 : 720;
        for (int frame = 0; frame < longRunFrames; ++frame)
        {
            scene.Update(1.0f / 60.0f);
            for (RigidBodyIndex index = 0U;
                 index < physics.RigidBodyCount();
                 ++index)
            {
                const PhysicsBodyState state = physics.BodyStateAt(index);
                if (!std::isfinite(state.position.x) ||
                    !std::isfinite(state.position.y) ||
                    !std::isfinite(state.position.z) ||
                    !std::isfinite(state.rotation.w) ||
                    !std::isfinite(state.rotation.x) ||
                    !std::isfinite(state.rotation.y) ||
                    !std::isfinite(state.rotation.z))
                {
                    result.finite = false;
                    break;
                }
            }
            if (physics.StabilizationFailed())
                result.stabilized = false;
        }

        result.diagnostics = physics.RuntimeJointDiagnostics();
        result.recoveries = physics.RecoveryStatistics().totalRecoveries;
        result.maximumMode2TranslationDelta =
            physics.FidelityStatistics().maximumMode2TranslationDelta;
        results.push_back(result);

        std::cout << "[P0 A/B] " << result.name
                  << " linearViol="
                  << result.diagnostics.maximumLinearLimitViolation
                  << " angularViolDeg="
                  << result.diagnostics.maximumAngularLimitViolationDegrees
                  << " severe="
                  << result.diagnostics.jointsOverFailureThreshold
                  << " maxJointPos="
                  << result.diagnostics.maximumPositionSeparation
                  << " recoveries=" << result.recoveries
                  << " mode2Delta=" << result.maximumMode2TranslationDelta
                  << " finite=" << (result.finite ? "true" : "false")
                  << " stabilized="
                  << (result.stabilized ? "true" : "false")
                  << std::endl;
    };

    runArm(
        "adaptive-default",
        MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults(),
        false
    );
    runArm(
        "compat-bullet275",
        MmdPhysicsRuntimePolicy::MmdCompatDefaults(),
        false
    );
    runArm(
        "raw-adaptive-default",
        MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults(),
        true
    );
    runArm(
        "raw-compat-bullet275",
        MmdPhysicsRuntimePolicy::MmdCompatDefaults(),
        true
    );

    MmdPhysicsRuntimePolicy legacyOnly = MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults();
    legacyOnly.bullet275.legacySpringConstraint = true;
    legacyOnly.bullet275.disableOffsetForConstraintFrame = true;
    runArm("iso-legacy-constraint", legacyOnly, false);

    MmdPhysicsRuntimePolicy deactivationOnly =
        MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults();
    deactivationOnly.bullet275.disableDynamicDeactivation = true;
    runArm("iso-deactivation", deactivationOnly, false);

    MmdPhysicsRuntimePolicy linkedCollisionOnly =
        MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults();
    linkedCollisionOnly.bullet275.disableLinkedBodyCollisions = false;
    runArm("iso-linked-collision", linkedCollisionOnly, false);

    Require(results.size() == 7U, "P0 Bullet 2.75 A/B did not run all arms");
    for (const ArmResult& result : results)
    {
        Require(
            result.finite && result.stabilized,
            "P0 Bullet 2.75 A/B arm became non-finite or entered safe freeze: " +
                result.name
        );
    }
}

void TestMmdCompatRuntimeWhenAvailable()
{
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"皮卡皮卡皮卡丘+" /
        u8"身体动作.vmd";
    if (!std::filesystem::is_regular_file(motionPath))
    {
        motionPath = ProjectAssetDirectory / "motions" /
            "#U76ae#U5361#U76ae#U5361#U76ae#U5361#U4e18+" /
            "#U8eab#U4f53#U52a8#U4f5c.vmd";
    }
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

    ImportedModelData imported;
    try
    {
        imported = ModelImporter().Import(modelPath);
    }
    catch (const std::runtime_error& error)
    {
        if (std::string(error.what()).find("texture was not found") !=
            std::string::npos)
        {
            return;
        }
        throw;
    }
    Require(
        imported.skeleton.has_value() && imported.mmdPhysics.has_value(),
        "MMD compat A/B source lost skeleton or physics"
    );

    ModelAsset model("mmdCompatRuntime");
    model.SetSkeleton(std::move(*imported.skeleton));
    if (!imported.morphs.empty())
        model.SetMorphs(std::move(imported.morphs));
    model.SetMmdPhysics(std::move(*imported.mmdPhysics));
    const MorphSet* morphs = model.HasMorphs() ? &model.GetMorphSet() : nullptr;
    ImportedVmdAnimationData motion = VmdImporter().Import(
        motionPath,
        model.GetSkeleton(),
        VmdImportOptions{.clipName = "compatMotion"},
        morphs
    );
    model.AddAnimationClip(std::move(motion.clip));

    const int longRunFrames =
        std::getenv("WISTERIA_SANITIZER_SMOKE") != nullptr ? 60 : 720;

    struct CompatResult
    {
        bool created = false;
        bool finite = true;
        std::size_t bodies = 0U;
        std::size_t joints = 0U;
        float maxDisplacement = 0.0f;
        MmdCompatJointDiagnostics diagnostics{};
    };

    const auto runCompat = [&]() -> CompatResult
    {
        CompatResult result;
        Scene scene;
        Entity& entity = scene.InstantiateModel(
            model,
            {},
            ModelInstantiationOptions{.enablePhysics = false}
        );
        try
        {
            entity.SetMmdPhysics(
                scene.Physics(),
                model.GetMmdPhysics(),
                MmdCompatSettings{}
            );
        }
        catch (const std::exception&)
        {
            return result;
        }
        result.created = true;

        const MmdCompatPhysicsInstance& physics =
            entity.GetMmdCompatPhysics();
        result.bodies = physics.RigidBodyCount();
        result.joints = physics.JointCount();

        std::vector<glm::vec3> startPositions;
        startPositions.reserve(result.bodies);
        for (RigidBodyIndex index = 0U; index < result.bodies; ++index)
            startPositions.push_back(physics.BodyStateAt(index).position);

        scene.Update(0.0f);
        for (int frame = 0; frame < longRunFrames; ++frame)
        {
            scene.Update(1.0f / 60.0f);
            for (RigidBodyIndex index = 0U; index < result.bodies; ++index)
            {
                const PhysicsBodyState state = physics.BodyStateAt(index);
                if (!std::isfinite(state.position.x) ||
                    !std::isfinite(state.position.y) ||
                    !std::isfinite(state.position.z) ||
                    !std::isfinite(state.rotation.w) ||
                    !std::isfinite(state.rotation.x) ||
                    !std::isfinite(state.rotation.y) ||
                    !std::isfinite(state.rotation.z))
                {
                    result.finite = false;
                    break;
                }
            }
        }
        for (RigidBodyIndex index = 0U; index < result.bodies; ++index)
        {
            const float distance = glm::distance(
                physics.BodyStateAt(index).position,
                startPositions[index]
            );
            result.maxDisplacement = std::max(
                result.maxDisplacement,
                distance
            );
        }
        result.diagnostics = physics.JointDiagnostics();
        return result;
    };

    const auto runLegacy = [&]() -> MmdPhysicsInstance::MmdRuntimeJointDiagnostics
    {
        Scene scene;
        Entity& entity = scene.InstantiateModel(
            model,
            {},
            ModelInstantiationOptions{.enablePhysics = false}
        );
        entity.SetMmdPhysics(
            scene.Physics(),
            model.GetMmdPhysics(),
            MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults()
        );
        scene.Update(0.0f);
        const MmdPhysicsInstance& physics = entity.GetMmdPhysics();
        for (int frame = 0; frame < longRunFrames; ++frame)
            scene.Update(1.0f / 60.0f);
        return physics.RuntimeJointDiagnostics();
    };

    const CompatResult compat = runCompat();
    const MmdPhysicsInstance::MmdRuntimeJointDiagnostics legacy =
        runLegacy();

    std::cout << "[MMD COMPAT A/B] legacy linearViol="
              << legacy.maximumLinearLimitViolation
              << " angularViolDeg="
              << legacy.maximumAngularLimitViolationDegrees
              << " severe=" << legacy.jointsOverFailureThreshold
              << std::endl;
    std::cout << "[MMD COMPAT A/B] compat created="
              << (compat.created ? "true" : "false")
              << " bodies=" << compat.bodies
              << " joints=" << compat.joints
              << " finite=" << (compat.finite ? "true" : "false")
              << " maxDisplacement=" << compat.maxDisplacement
              << " linearViol="
              << compat.diagnostics.maximumLinearLimitViolation
              << " angularViolDeg="
              << compat.diagnostics.maximumAngularLimitViolationDegrees
              << " severe="
              << compat.diagnostics.jointsOverFailureThreshold
              << std::endl;

    Require(
        compat.created && compat.finite &&
            compat.diagnostics.finite &&
            compat.bodies == 495U && compat.joints > 0U,
        "MMD compat runtime failed to create or run the demo model"
    );
    Require(
        legacy.finite,
        "MMD legacy runtime became non-finite during compat A/B"
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

void TestMmdPhysics3AllJointInstantiation()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics3AllJointModel();
    Scene scene;
    Entity& entity = scene.InstantiateModel(*model);
    Require(
        entity.GetMmdPhysics().RigidBodyCount() == 2U &&
        entity.GetMmdPhysics().ConstraintCount() == 6U &&
        scene.Physics().ConstraintCount() == 6U,
        "MMD runtime did not instantiate all PMX 2.1 joint kinds"
    );
    scene.Physics().SetGravity(glm::vec3(0.0f));
    scene.Update(1.0f / 60.0f);
    Require(
        std::isfinite(entity.GetMmdPhysics().BodyStateAt(0U).position.x) &&
        std::isfinite(entity.GetMmdPhysics().BodyStateAt(1U).position.x),
        "PMX 2.1 joint runtime produced non-finite state"
    );
}

void TestMmdPhysics3ImpulseMorphs()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics3ImpulseModel();
    Scene scene;
    Entity& entity = scene.InstantiateModel(*model);
    scene.Physics().SetGravity(glm::vec3(0.0f));
    MorphState& morphState = entity.GetMorphState();
    MmdPhysicsInstance& physics = entity.GetMmdPhysics();
    const PhysicsBodyHandle body = physics.BodyHandleAt(1U);
    const PhysicsBodyState initial = scene.Physics().State(body);

    scene.Physics().SetTransform(
        body,
        initial.position,
        glm::angleAxis(
            glm::radians(90.0f),
            glm::vec3(0.0f, 0.0f, 1.0f)
        ),
        true
    );
    morphState.SetWeight("localImpulse", 1.0f);
    physics.ApplyImpulseMorphs(morphState);
    PhysicsBodyState state = scene.Physics().State(body);
    Require(
        std::abs(state.linearVelocity.y) > 0.5f &&
        std::abs(state.linearVelocity.x) < 0.15f &&
        std::abs(state.angularVelocity.x) > 0.2f,
        "Local Impulse Morph was not rotated by the rigid body transform"
    );

    morphState.Reset();
    scene.Physics().SetLinearVelocity(body, glm::vec3(9.0f, 8.0f, 7.0f));
    scene.Physics().SetAngularVelocity(body, glm::vec3(6.0f, 5.0f, 4.0f));
    morphState.SetWeight("globalImpulse", 1.0f);
    morphState.SetWeight("resetImpulse", 1.0f);
    physics.ApplyImpulseMorphs(morphState);
    state = scene.Physics().State(body);
    Require(
        state.linearVelocity.x > 1.0f &&
        std::abs(state.linearVelocity.y) < 0.1f &&
        std::abs(state.linearVelocity.z) < 0.1f &&
        state.angularVelocity.z > 0.2f &&
        std::abs(state.angularVelocity.x) < 0.1f &&
        std::abs(state.angularVelocity.y) < 0.1f,
        "Impulse Morph reset was not processed before the same-frame impulse"
    );
}

void TestMmdPhysics3AfterPhysicsPhase()
{
    Bone source;
    source.name = "source";

    Bone before;
    before.name = "before";
    before.appendTransform = MmdAppendTransform{
        0U,
        1.0f,
        false,
        true
    };

    Bone after;
    after.name = "after";
    after.deformAfterPhysics = true;
    after.appendTransform = MmdAppendTransform{
        0U,
        1.0f,
        false,
        true
    };

    Skeleton skeleton({source, before, after});
    Require(
        skeleton.MmdBeforePhysicsConstraintOrder().size() == 1U &&
        skeleton.MmdAfterPhysicsConstraintOrder().size() == 1U,
        "Skeleton did not partition before/after-physics constraints"
    );

    PoseBuffer buffer(skeleton);
    BoneTransform animatedSource = buffer.TransformAt(0U);
    animatedSource.translation = glm::vec3(2.0f, 0.0f, 0.0f);
    buffer.SetTransform(0U, animatedSource);

    MmdPoseSolver solver;
    solver.Solve(buffer, MmdPosePhase::BeforePhysics);
    Require(
        NearlyEqual(
            buffer.TransformAt(1U).translation,
            glm::vec3(2.0f, 0.0f, 0.0f)
        ) &&
        NearlyEqual(buffer.TransformAt(2U).translation, glm::vec3(0.0f)),
        "Before-physics solve executed an after-physics append constraint"
    );

    solver.Solve(buffer, MmdPosePhase::AfterPhysics);
    Require(
        NearlyEqual(
            buffer.TransformAt(2U).translation,
            glm::vec3(2.0f, 0.0f, 0.0f)
        ),
        "After-physics solve did not execute its append constraint"
    );

    Pose pose(skeleton);
    Animator animator(pose);
    const AnimationClip clip(
        "afterPhase",
        1.0f,
        {
            AnimationTrack(
                0U,
                {VectorKeyframe{0.0f, glm::vec3(2.0f, 0.0f, 0.0f)}}
            )
        }
    );
    animator.Play(clip);
    animator.Update(0.0f);
    animator.SolveAfterPhysics();
    const glm::vec3 firstAfter = BoneTransform::FromMatrix(
        pose.LocalMatrix(2U)
    ).translation;
    animator.Update(0.0f);
    animator.SolveAfterPhysics();
    const glm::vec3 secondAfter = BoneTransform::FromMatrix(
        pose.LocalMatrix(2U)
    ).translation;
    Require(
        NearlyEqual(firstAfter, glm::vec3(2.0f, 0.0f, 0.0f)) &&
        NearlyEqual(secondAfter, firstAfter),
        "After-physics constraints accumulated across frames"
    );

    Bone ikLink;
    ikLink.name = "afterIkLink";
    Bone ikEffector;
    ikEffector.name = "afterIkEffector";
    ikEffector.parentIndex = 0U;
    ikEffector.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    ikEffector.inverseBindMatrix = glm::inverse(
        ikEffector.bindLocalMatrix
    );
    Bone ikController;
    ikController.name = "afterIkController";
    ikController.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    ikController.inverseBindMatrix = glm::inverse(
        ikController.bindLocalMatrix
    );
    ikController.deformAfterPhysics = true;
    ikController.ikConstraint = MmdIkConstraint{
        1U,
        8U,
        glm::radians(45.0f),
        {MmdIkLink{0U}}
    };
    Skeleton ikSkeleton({ikLink, ikEffector, ikController});
    Pose ikPose(ikSkeleton);
    Animator ikAnimator(ikPose);
    const AnimationClip ikClip(
        "afterIk",
        1.0f,
        {
            AnimationTrack(
                2U,
                {VectorKeyframe{0.0f, glm::vec3(0.0f, 1.0f, 0.0f)}}
            )
        }
    );
    ikAnimator.Play(ikClip);
    Require(
        glm::distance(
            glm::vec3(ikPose.GlobalMatrix(1U)[3]),
            glm::vec3(1.0f, 0.0f, 0.0f)
        ) < 0.001f,
        "After-physics IK executed during the before-physics phase"
    );
    ikAnimator.SolveAfterPhysics();
    Require(
        glm::distance(
            glm::vec3(ikPose.GlobalMatrix(1U)[3]),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) < 0.001f,
        "After-physics IK did not solve after the physics phase"
    );
}

void TestMmdPhysics3AutomaticReset()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics2BModeModel();
    AnimationClip& clip = model->AddAnimationClip(AnimationClip(
        "resetClip",
        1.0f,
        {
            AnimationTrack(
                0U,
                {
                    VectorKeyframe{0.0f, glm::vec3(0.0f)},
                    VectorKeyframe{1.0f, glm::vec3(0.2f, 0.0f, 0.0f)}
                }
            )
        }
    ));

    Scene scene;
    Entity& entity = scene.InstantiateModel(*model);
    scene.Physics().SetGravity(glm::vec3(0.0f));
    Animator& animator = entity.GetAnimator();
    animator.Play(clip);
    scene.Update(0.0f);

    const PhysicsBodyHandle body = entity.GetMmdPhysics().BodyHandleAt(1U);
    scene.Physics().ApplyCentralImpulse(body, glm::vec3(4.0f, 0.0f, 0.0f));
    Require(
        glm::length(scene.Physics().State(body).linearVelocity) > 0.5f,
        "Automatic-reset test failed to establish body velocity"
    );
    animator.SetTime(0.5f);
    scene.Update(0.0f);
    Require(
        glm::length(scene.Physics().State(body).linearVelocity) < Epsilon,
        "Animator SetTime did not automatically reset MMD physics"
    );

    animator.SetLooping(true);
    animator.SetTime(0.95f);
    scene.Update(0.0f);
    scene.Physics().ApplyCentralImpulse(body, glm::vec3(4.0f, 0.0f, 0.0f));
    scene.Update(0.10f);
    Require(
        animator.Time() < 0.2f &&
        glm::length(scene.Physics().State(body).linearVelocity) < Epsilon,
        "Animation loop wrap did not automatically reset MMD physics"
    );
}

void TestMmdPhysics2BSceneMoveAssignment()
{
    std::unique_ptr<ModelAsset> model = CreatePhysics2BSpringModel();
    Scene source;
    Entity& sourceEntity = source.InstantiateModel(*model);
    const PhysicsBodyHandle sourceBody =
        sourceEntity.GetMmdPhysics().BodyHandleAt(0U);

    Scene destination;
    destination.InstantiateModel(*model);
    destination = std::move(source);
    Require(
        destination.Physics().BodyCount() == 2U &&
        destination.Physics().ConstraintCount() == 1U &&
        destination.Physics().Contains(sourceBody),
        "Scene move assignment invalidated incoming MMD physics handles"
    );
    destination.Update(1.0f / 60.0f);
    destination.ClearEntities();
    Require(
        destination.Physics().BodyCount() == 0U &&
        destination.Physics().ConstraintCount() == 0U,
        "Moved Scene did not release its MMD physics runtime"
    );
}


template<typename Function>
bool RunTest(const char* name, Function&& function)
{
    try
    {
        function();
        std::cout << "[PASS] " << name << '\n';
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        return false;
    }
}
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
    failures += !RunTest(
        "MMD Physics 3 all joint types",
        TestMmdPhysics3AllJointInstantiation
    );
    failures += !RunTest(
        "MMD Physics 3 Impulse Morphs",
        TestMmdPhysics3ImpulseMorphs
    );
    failures += !RunTest(
        "MMD Physics 3 after-physics phase",
        TestMmdPhysics3AfterPhysicsPhase
    );
    failures += !RunTest(
        "MMD Physics 3 automatic reset",
        TestMmdPhysics3AutomaticReset
    );
    failures += !RunTest(
        "MMD Physics 2B modes and Pose sync",
        TestMmdPhysics2BModesAndPoseSync
    );
    failures += !RunTest(
        "MMD runtime policy injection",
        TestMmdPhysicsRuntimePolicyInjection
    );
    failures += !RunTest(
        "MMD P1.2 Mode 2 fidelity strategies",
        TestMmdPhysicsFidelityModesAndDebugLayers
    );
    failures += !RunTest(
        "MMD P1.2 vertex skinning debug samples",
        TestMeshSkinningFidelityDebugSamples
    );
    failures += !RunTest(
        "MMD bind alignment overlay",
        TestMmdPhysicsBindAlignmentOverlay
    );
    failures += !RunTest(
        "MMD initialization stabilization",
        TestMmdPhysicsInitializationStabilization
    );
    failures += !RunTest(
        "MMD gravity and constraint balance",
        TestMmdGravityConstraintBalance
    );
    failures += !RunTest(
        "MMD P1.3 chain semantics and anchor diagnostics",
        TestMmdP13ChainSemanticsAndAnchorDiagnostics
    );
    failures += !RunTest(
        "MMD collision topology and adaptive CCD",
        TestMmdCollisionTopologyAndAdaptiveCcd
    );
    failures += !RunTest(
        "MMD P1 local chain recovery",
        TestMmdPhysicsP1LocalRecovery
    );
    failures += !RunTest(
        "MMD P1.1 tick timing, local region and fuse",
        TestMmdPhysicsP11LocalizedRegionAndFuse
    );
    failures += !RunTest(
        "MMD Physics 2B Spring and lifecycle",
        TestMmdPhysics2BSpringAndLifecycle
    );
    failures += !RunTest(
        "MMD Physics 2B scale validation",
        TestMmdPhysics2BScaleValidation
    );
    failures += !RunTest(
        "MMD Physics 2B Scene move assignment",
        TestMmdPhysics2BSceneMoveAssignment
    );
    failures += !RunTest(
        "Demo PMX Physics 2B runtime",
        TestDemoPmxPhysics2BRuntimeWhenAvailable
    );
    failures += !RunTest(
        "Demo PMX/VMD initialization and long-run diagnostics",
        TestDemoPmxVmdInitializationStabilizationWhenAvailable
    );
    failures += !RunTest(
        "P0 Bullet 2.75 compatibility A/B long-run",
        TestP0Bullet275CompatibilityLongRunWhenAvailable
    );
    failures += !RunTest(
        "MMD compat runtime demo A/B",
        TestMmdCompatRuntimeWhenAvailable
    );
    failures += !RunTest("Skeleton and Pose", TestSkeletonAndPose);
    failures += !RunTest("Skeleton validation", TestSkeletonValidation);
    failures += !RunTest(
        "Animation sampling and Animator",
        TestAnimationSamplingAndAnimator
    );
    failures += !RunTest("Root motion", TestRootMotion);
    failures += !RunTest("MMD append and IK constraints", TestMmdBoneConstraints);
    failures += !RunTest("Animated model importer", TestAnimatedModelImporter);
    failures += !RunTest(
        "Extended PMX morph importer",
        TestExtendedPmxMorphImporter
    );
    failures += !RunTest(
        "PMX 2.1 Flip/Impulse importer",
        TestPmx21FlipImpulseImporter
    );
    failures += !RunTest("PMX Physics 1 importer", TestPmxPhysicsImporter);
    failures += !RunTest(
        "PMX Physics 1 importer validation",
        TestPmxPhysicsImporterValidation
    );
    failures += !RunTest(
        "MMD physics asset validation",
        TestMmdPhysicsAssetValidation
    );
    failures += !RunTest("VMD importer", TestVmdImporter);
    failures += !RunTest("VMD asset integration", TestVmdAssetWhenAvailable);
    failures += !RunTest("ModelAsset skeleton", TestModelAssetSkeleton);
    failures += !RunTest("Morph runtime", TestMorphRuntime);
    failures += !RunTest(
        "Extended MMD morph runtime",
        TestExtendedMmdMorphRuntime
    );
    failures += !RunTest(
        "PMX 2.1 Flip/Impulse runtime",
        TestPmx21FlipImpulseMorphRuntime
    );
    failures += !RunTest(
        "Generic PhysicsInstance lifecycle",
        TestGenericPhysicsInstanceLifecycle
    );
    failures += !RunTest(
        "MMD full-body demo animation",
        TestMmdFullBodyDemoAnimation
    );
    failures += !RunTest(
        "MMD full-body demo integration",
        TestMmdFullBodyDemoIntegrationWhenAvailable
    );
    failures += !RunTest("Morph Lab demo asset", TestMorphLabDemoAsset);
    failures += !RunTest("RenderPart and ModelAsset", TestRenderPartAndModelAsset);
    failures += !RunTest("Built-in cube tangents", TestBuiltInCubeTangents);
    failures += !RunTest("Mesh bounds center", TestMeshBoundsCenter);
    failures += !RunTest("Model instantiation", TestModelInstantiation);
    failures += !RunTest("Frame-rate independent behaviours", TestFrameRateIndependentBehaviours);
    failures += !RunTest("Input frame transitions", TestInputFrameTransitions);
    failures += !RunTest("Free camera controller", TestFreeCameraController);
    failures += !RunTest("FXAA settings", TestFxaaSettings);
    failures += !RunTest("ResourceManager model registry", TestResourceManagerModelRegistry);
    failures += !RunTest(
        "Environment resource and Scene binding",
        TestEnvironmentResourceAndSceneBinding
    );
    failures += !RunTest("Static model importer", TestStaticModelImporter);
    failures += !RunTest("Imported resource creation", TestImportedResourceCreation);
    failures += !RunTest("Importer missing-file rejection", TestImporterRejectsMissingFile);
    failures += !RunTest(
        "Transactional imported resource creation",
        TestImportResourceCollisionIsTransactional
    );
    failures += !RunTest("Converted MMD GLB integration", TestConvertedMmdGlbWhenAvailable);
    failures += !RunTest("Converted MMD OBJ integration", TestConvertedMmdObjWhenAvailable);
    failures += !RunTest("Rigged GLB skin integration", TestRiggedGlbImportWhenAvailable);
    failures += !RunTest(
        "Demo PMX Physics 1 integration",
        TestDemoPmxPhysicsImportWhenAvailable
    );
    failures += !RunTest(
        "Direct PMX material integration",
        TestDirectPmxMaterialImportWhenAvailable
    );
    failures += !RunTest(
        "Direct PMX Group Morph integration",
        TestDirectPmxGroupMorphImportWhenAvailable
    );
    return failures == 0 ? 0 : 1;
}
