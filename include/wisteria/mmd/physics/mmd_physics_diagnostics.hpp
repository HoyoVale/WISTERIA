#pragma once

#include "wisteria/mmd/physics/mmd_physics_modes.hpp"

#include <glm/glm.hpp>
#include <cstddef>
#include <limits>

struct MmdPhysicsChainBalanceStatistics
{
    std::size_t chainIndex = std::numeric_limits<std::size_t>::max();
    MmdPhysicsChainKind kind = MmdPhysicsChainKind::General;
    std::size_t bodyCount = 0U;
    std::size_t dynamicBodyCount = 0U;
    float totalMass = 0.0f;
    float gravityScale = 1.0f;
    float effectiveGravityScale = 1.0f;
    float minimumLinearDamping = 0.0f;
    float minimumAngularDamping = 0.0f;
    float averageDownwardDisplacement = 0.0f;
    float maximumDownwardDisplacement = 0.0f;
    float averageSpeed = 0.0f;
    float maximumSpeed = 0.0f;
    std::size_t contactPairCount = 0U;
    float contactImpulse = 0.0f;
    float maximumMode2TranslationDelta = 0.0f;
    std::size_t anchorBodyIndex = std::numeric_limits<std::size_t>::max();
    float anchorLinearSpeed = 0.0f;
    float anchorAngularSpeed = 0.0f;
    float averageAnchorDistance = 0.0f;
    float maximumAnchorDistance = 0.0f;
    float averageNormalizedExtension = 0.0f;
    float maximumNormalizedExtension = 0.0f;
    float totalConstraintImpulse = 0.0f;
    float maximumConstraintImpulse = 0.0f;
};

struct MmdPhysicsGravityStatistics
{
    MmdPhysicsGravityMode mode = MmdPhysicsGravityMode::Balanced100;
    float globalGravityScale = 1.0f;
    bool chainProfilesEnabled = true;
    std::size_t chainCount = 0U;
    std::size_t dynamicBodyCount = 0U;
    std::size_t skirtLayerIgnoredPairCount = 0U;
    std::size_t skirtSemanticIgnoredPairCount = 0U;
    float minimumEffectiveGravityScale = 1.0f;
    float maximumEffectiveGravityScale = 1.0f;
    float averageEffectiveGravityScale = 1.0f;
    float averageDownwardDisplacement = 0.0f;
    float maximumDownwardDisplacement = 0.0f;
    float averageSpeed = 0.0f;
    float maximumSpeed = 0.0f;
    float totalContactImpulse = 0.0f;
    float totalConstraintImpulse = 0.0f;
    float maximumConstraintImpulse = 0.0f;
    float maximumNormalizedExtension = 0.0f;
    float maximumAnchorLinearSpeed = 0.0f;
    float maximumMode2TranslationDelta = 0.0f;
};

struct MmdPhysicsFidelityStatistics
{
    std::size_t drivenBoneCount = 0U;
    std::size_t physicsWithBoneCount = 0U;
    std::size_t sampledVertexCount = 0U;
    float maximumBulletToBonePositionError = 0.0f;
    float averageBulletToBonePositionError = 0.0f;
    float maximumBulletToBoneRotationErrorDegrees = 0.0f;
    float averageBulletToBoneRotationErrorDegrees = 0.0f;
    float maximumMode2TranslationDelta = 0.0f;
    float averageMode2TranslationDelta = 0.0f;
};

struct MmdPhysicsContactDiagnostic
{
    std::size_t bodyAIndex = std::numeric_limits<std::size_t>::max();
    std::size_t bodyBIndex = std::numeric_limits<std::size_t>::max();
    std::size_t chainAIndex = std::numeric_limits<std::size_t>::max();
    std::size_t chainBIndex = std::numeric_limits<std::size_t>::max();
    std::size_t contactPointCount = 0U;
    float maximumPenetrationDepth = 0.0f;
    float totalAppliedImpulse = 0.0f;
    float maximumAppliedImpulse = 0.0f;
    glm::vec3 deepestPointOnB{0.0f};
    glm::vec3 deepestNormalOnB{0.0f, 1.0f, 0.0f};
};

struct MmdPhysicsCollisionStatistics
{
    std::size_t linkedJointPairCount = 0U;
    std::size_t ignoredNearNeighborPairCount = 0U;
    std::size_t denseMarginBodyCount = 0U;
    std::size_t ccdCandidateCount = 0U;
    std::size_t activeCcdBodyCount = 0U;
    std::size_t ccdActivationCount = 0U;
    std::size_t ccdDeactivationCount = 0U;
    std::size_t contactPairCount = 0U;
    std::size_t sameChainContactPairCount = 0U;
    std::size_t crossChainContactPairCount = 0U;
    std::size_t contactPointCount = 0U;
    float maximumPenetrationDepth = 0.0f;
    float totalAppliedImpulse = 0.0f;
    float maximumPairImpulse = 0.0f;
};

struct MmdPhysicsAlignmentSummary
{
    std::size_t bodyCount = 0U;
    std::size_t jointCount = 0U;
    float maximumBindPositionError = 0.0f;
    float maximumBindRotationErrorDegrees = 0.0f;
    float maximumBulletPositionError = 0.0f;
    float maximumBulletRotationErrorDegrees = 0.0f;
    float maximumPostResetPositionError = 0.0f;
    float maximumPostResetRotationErrorDegrees = 0.0f;
    float maximumSkinningBindError = 0.0f;
    bool nonIdentityBindSpace = false;
};

struct MmdPhysicsRecoveryStatistics
{
    std::size_t chainCount = 0U;
    std::size_t physicsTickCount = 0U;
    std::size_t totalRecoveries = 0U;
    std::size_t recoveredBodyCount = 0U;
    std::size_t largestRecoveryRegion = 0U;
    std::size_t pendingAbnormalChainCount = 0U;
    std::size_t cooldownChainCount = 0U;
    std::size_t fusedChainCount = 0U;
    std::size_t totalFuseTrips = 0U;
    std::size_t suppressedRecoveryCount = 0U;
    std::size_t lastRecoveredChain = std::numeric_limits<std::size_t>::max();
    std::size_t lastRecoveredBodyCount = 0U;
    std::size_t lastSeedBodyIndex = std::numeric_limits<std::size_t>::max();
    std::size_t lastJointIndex = std::numeric_limits<std::size_t>::max();
    MmdPhysicsRecoveryReason lastReason = MmdPhysicsRecoveryReason::None;
    float lastAbnormalSeconds = 0.0f;
    float lastPositionError = 0.0f;
    float lastLinearViolation = 0.0f;
    float lastAngularViolationDegrees = 0.0f;
    float lastLinearSpeed = 0.0f;
    float lastAngularSpeed = 0.0f;
    float lastAnchorDistance = 0.0f;
    float lastBindChainLength = 0.0f;
    float lastNormalizedExtension = 0.0f;
};
