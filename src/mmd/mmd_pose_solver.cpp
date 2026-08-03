#include "wisteria/common/pch.hpp"
#include "wisteria/mmd/mmd_pose_solver.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <vector>

namespace
{
constexpr float DirectionEpsilon = 0.000001f;
constexpr float SolvedDistance = 0.0001f;
constexpr std::uint32_t MaximumIkIterations = 256U;

glm::mat4 RotationMatrixXYZ(const glm::vec3& angles)
{
    glm::mat4 matrix(1.0f);
    matrix = glm::rotate(matrix, angles.x, glm::vec3(1.0f, 0.0f, 0.0f));
    matrix = glm::rotate(matrix, angles.y, glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::rotate(matrix, angles.z, glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::vec3 ExtractEulerAngleXYZ(const glm::mat4& matrix)
{
    const float first = std::atan2(matrix[2][1], matrix[2][2]);
    const float cosineSecond = std::sqrt(
        matrix[0][0] * matrix[0][0] +
        matrix[1][0] * matrix[1][0]
    );
    const float second = std::atan2(-matrix[2][0], cosineSecond);
    const float sineFirst = std::sin(first);
    const float cosineFirst = std::cos(first);
    const float third = std::atan2(
        sineFirst * matrix[0][2] - cosineFirst * matrix[0][1],
        cosineFirst * matrix[1][1] - sineFirst * matrix[1][2]
    );
    return glm::vec3(-first, -second, -third);
}

glm::quat WeightedRotation(glm::quat rotation, float weight)
{
    rotation = glm::normalize(rotation);
    if (rotation.w < 0.0f)
        rotation = -rotation;
    const float halfAngle = std::acos(std::clamp(rotation.w, -1.0f, 1.0f));
    const float sineHalfAngle = std::sin(halfAngle);
    if (std::abs(sineHalfAngle) <= DirectionEpsilon)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 axis(rotation.x, rotation.y, rotation.z);
    return glm::angleAxis(
        2.0f * halfAngle * weight,
        glm::normalize(axis)
    );
}

glm::quat GlobalRotation(const glm::mat4& matrix)
{
    glm::vec3 axisX(matrix[0]);
    glm::vec3 axisY(matrix[1]);
    glm::vec3 axisZ(matrix[2]);
    if (glm::length(axisX) <= DirectionEpsilon ||
        glm::length(axisY) <= DirectionEpsilon ||
        glm::length(axisZ) <= DirectionEpsilon)
    {
        throw std::runtime_error("MMD IK encountered a singular bone matrix");
    }
    axisX = glm::normalize(axisX);
    axisY = glm::normalize(axisY);
    axisZ = glm::normalize(axisZ);
    return glm::normalize(glm::quat_cast(glm::mat3(axisX, axisY, axisZ)));
}

void ClampIkRotation(
    BoneTransform& transform,
    const BoneTransform& bindTransform,
    const MmdIkLink& link
)
{
    if (!link.hasLimits)
        return;
    const glm::quat relative = glm::normalize(
        glm::inverse(bindTransform.rotation) * transform.rotation
    );
    glm::vec3 angles = ExtractEulerAngleXYZ(glm::mat4_cast(relative));
    angles = glm::clamp(angles, link.minimumAngle, link.maximumAngle);
    transform.rotation = glm::normalize(
        bindTransform.rotation *
        glm::quat_cast(glm::mat3(RotationMatrixXYZ(angles)))
    );
}
}

void MmdPoseSolver::Solve(
    PoseBuffer& pose,
    MmdPosePhase phase,
    const IkEnabledPredicate& ikEnabled
)
{
    const Skeleton& skeleton = pose.GetSkeleton();
    const std::span<const BoneIndex> constraintOrder =
        phase == MmdPosePhase::BeforePhysics
            ? skeleton.MmdBeforePhysicsConstraintOrder()
            : skeleton.MmdAfterPhysicsConstraintOrder();
    if (constraintOrder.empty())
        return;

    this->transforms.assign(
        pose.Transforms().begin(),
        pose.Transforms().end()
    );
    this->localMatrices.assign(
        pose.LocalMatrices().begin(),
        pose.LocalMatrices().end()
    );
    this->globalMatrices.resize(skeleton.BoneCount(), glm::mat4(1.0f));
    this->changed.assign(skeleton.BoneCount(), 0U);
    this->traversalStack.clear();
    this->traversalStack.reserve(skeleton.BoneCount());
    const std::span<const BoneTransform> bindTransforms =
        pose.BindTransforms();
    bool globalsInitialized = false;
    const auto ensureGlobals = [this, &skeleton, &globalsInitialized]
    {
        if (globalsInitialized)
            return;
        this->RecalculateAll(skeleton);
        globalsInitialized = true;
    };

    for (BoneIndex constrainedBone : constraintOrder)
    {
        const Bone& bone = skeleton.BoneAt(constrainedBone);
        if (bone.appendTransform.has_value())
        {
            const MmdAppendTransform& append = *bone.appendTransform;
            const BoneTransform& source =
                this->transforms[append.sourceBone];
            const BoneTransform& sourceBind =
                bindTransforms[append.sourceBone];
            BoneTransform& destination =
                this->transforms[constrainedBone];
            bool destinationChanged = false;
            if (append.affectTranslation)
            {
                const glm::vec3 translationDelta =
                    (source.translation - sourceBind.translation) *
                    append.weight;
                if (glm::dot(translationDelta, translationDelta) >
                    DirectionEpsilon * DirectionEpsilon)
                {
                    destination.translation += translationDelta;
                    destinationChanged = true;
                }
            }
            if (append.affectRotation)
            {
                const glm::quat sourceDelta = glm::normalize(
                    glm::inverse(sourceBind.rotation) * source.rotation
                );
                const glm::quat weightedDelta =
                    WeightedRotation(sourceDelta, append.weight);
                if (glm::dot(
                        glm::vec3(
                            weightedDelta.x,
                            weightedDelta.y,
                            weightedDelta.z
                        ),
                        glm::vec3(
                            weightedDelta.x,
                            weightedDelta.y,
                            weightedDelta.z
                        )
                    ) > DirectionEpsilon * DirectionEpsilon)
                {
                    destination.rotation = glm::normalize(
                        destination.rotation * weightedDelta
                    );
                    destinationChanged = true;
                }
            }
            if (destinationChanged)
            {
                this->changed[constrainedBone] = 1U;
                this->localMatrices[constrainedBone] = destination.Matrix();
                if (globalsInitialized)
                    this->RecalculateSubtree(skeleton, constrainedBone);
            }
        }

        if (!bone.ikConstraint.has_value() ||
            (ikEnabled && !ikEnabled(constrainedBone)))
        {
            continue;
        }
        const MmdIkConstraint& ik = *bone.ikConstraint;
        const std::uint32_t iterations = std::min(
            ik.iterations,
            MaximumIkIterations
        );
        ensureGlobals();
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            const glm::vec3 initialGoal(
                this->globalMatrices[constrainedBone][3]
            );
            const glm::vec3 initialEffector(
                this->globalMatrices[ik.targetBone][3]
            );
            if (glm::length(initialGoal - initialEffector) <= SolvedDistance)
                break;

            bool rotatedAnyLink = false;
            for (const MmdIkLink& link : ik.links)
            {
                const glm::vec3 goal(
                    this->globalMatrices[constrainedBone][3]
                );
                const glm::vec3 effector(
                    this->globalMatrices[ik.targetBone][3]
                );
                const glm::vec3 linkPosition(
                    this->globalMatrices[link.bone][3]
                );
                glm::vec3 toEffector = effector - linkPosition;
                glm::vec3 toGoal = goal - linkPosition;
                const float effectorLength = glm::length(toEffector);
                const float goalLength = glm::length(toGoal);
                if (effectorLength <= DirectionEpsilon ||
                    goalLength <= DirectionEpsilon)
                {
                    continue;
                }
                toEffector /= effectorLength;
                toGoal /= goalLength;
                float angle = std::acos(std::clamp(
                    glm::dot(toEffector, toGoal),
                    -1.0f,
                    1.0f
                ));
                angle = std::min(angle, ik.angleLimit);
                if (angle <= DirectionEpsilon)
                    continue;

                glm::vec3 axisGlobal = glm::cross(toEffector, toGoal);
                if (glm::length(axisGlobal) <= DirectionEpsilon)
                    continue;
                axisGlobal = glm::normalize(axisGlobal);
                const glm::quat linkGlobalRotation =
                    GlobalRotation(this->globalMatrices[link.bone]);
                const glm::vec3 axisLocal = glm::normalize(
                    glm::inverse(linkGlobalRotation) * axisGlobal
                );
                BoneTransform& linkTransform =
                    this->transforms[link.bone];
                linkTransform.rotation = glm::normalize(
                    linkTransform.rotation * glm::angleAxis(angle, axisLocal)
                );
                ClampIkRotation(
                    linkTransform,
                    bindTransforms[link.bone],
                    link
                );
                this->changed[link.bone] = 1U;
                rotatedAnyLink = true;
                this->localMatrices[link.bone] = linkTransform.Matrix();
                this->RecalculateSubtree(skeleton, link.bone);
            }

            const glm::vec3 finalGoal(
                this->globalMatrices[constrainedBone][3]
            );
            const glm::vec3 finalEffector(
                this->globalMatrices[ik.targetBone][3]
            );
            if (glm::length(finalGoal - finalEffector) <= SolvedDistance ||
                !rotatedAnyLink)
            {
                break;
            }
        }
    }

    for (std::size_t index = 0; index < this->transforms.size(); ++index)
    {
        if (this->changed[index] != 0U)
        {
            pose.SetTransform(
                static_cast<BoneIndex>(index),
                this->transforms[index]
            );
        }
    }
}

void MmdPoseSolver::RecalculateAll(const Skeleton& skeleton)
{
    for (BoneIndex boneIndex : skeleton.EvaluationOrder())
    {
        const Bone& bone = skeleton.BoneAt(boneIndex);
        const glm::mat4& local = this->localMatrices[boneIndex];
        this->globalMatrices[boneIndex] =
            bone.parentIndex == InvalidBoneIndex
                ? local
                : this->globalMatrices[bone.parentIndex] * local;
    }
}

void MmdPoseSolver::RecalculateSubtree(
    const Skeleton& skeleton,
    BoneIndex rootBone
)
{
    this->traversalStack.clear();
    this->traversalStack.push_back(rootBone);
    while (!this->traversalStack.empty())
    {
        const BoneIndex boneIndex = this->traversalStack.back();
        this->traversalStack.pop_back();
        const Bone& bone = skeleton.BoneAt(boneIndex);
        const glm::mat4& local = this->localMatrices[boneIndex];
        this->globalMatrices[boneIndex] =
            bone.parentIndex == InvalidBoneIndex
                ? local
                : this->globalMatrices[bone.parentIndex] * local;

        const std::span<const BoneIndex> children =
            skeleton.Children(boneIndex);
        for (auto iterator = children.rbegin();
             iterator != children.rend();
             ++iterator)
        {
            this->traversalStack.push_back(*iterator);
        }
    }
}
