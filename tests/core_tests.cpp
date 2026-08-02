#include "animation.hpp"
#include "animator.hpp"
#include "behaviour.hpp"
#include "demo_scene.hpp"
#include "Models/cube.hpp"
#include "entity.hpp"
#include "importer.hpp"
#include "input.hpp"
#include "manager.hpp"
#include "model_asset.hpp"
#include "pose.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "vmd_importer.hpp"
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
        imported.rigidBodyCount == 1U &&
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
        model.MmdRigidBodyCount() == 1U,
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
    Entity& reference = scene.InstantiateModel(model);
    Entity& active = scene.InstantiateModel(model);
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
        impulses[0U].rigidBodyIndex == 0U &&
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
    model.SetMmdRigidBodyCount(3U);
    model.AddPart(mesh, material, localTransform);

    Require(model.Name() == "testModel", "ModelAsset name was not preserved");
    Require(
        model.MmdRigidBodyCount() == 3U,
        "ModelAsset did not preserve PMX rigid-body metadata"
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
        "Direct PMX material integration",
        TestDirectPmxMaterialImportWhenAvailable
    );
    failures += !RunTest(
        "Direct PMX Group Morph integration",
        TestDirectPmxGroupMorphImportWhenAvailable
    );
    return failures == 0 ? 0 : 1;
}
