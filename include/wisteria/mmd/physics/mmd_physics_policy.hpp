#pragma once

#include "wisteria/mmd/physics/mmd_physics_modes.hpp"

#include <cstddef>
#include <string>

// Tunable MMD adapter behavior. The generic PhysicsWorld layer intentionally
// does not know any of these values. Community-compatible presets and
// model-specific profiles can be added without changing Bullet ownership or
// the scene physics lifecycle.
struct MmdPhysicsStabilizationPolicy
{
    std::size_t steps = 30U;
    float fixedTimeStep = 1.0f / 60.0f;
    float warmupLinearViolation = 0.1f;
    float warmupAngularViolationDegrees = 5.0f;
    float failureLinearViolation = 0.5f;
    float failureAngularViolationDegrees = 45.0f;
};

struct MmdPhysicsRecoveryPolicy
{
    bool enabled = true;
    float persistenceSeconds = 0.45f;
    float highVelocityPersistenceSeconds = 0.30f;
    float cooldownSeconds = 3.0f;
    float fuseWindowSeconds = 12.0f;
    float fuseDurationSeconds = 10.0f;
    std::size_t fuseLimit = 3U;
    std::size_t localGraphRadius = 4U;
    std::size_t maximumDynamicBodies = 24U;
    std::size_t maximumTotalBodies = 32U;
    float linearSpeed = 55.0f;
    float hardLinearSpeed = 180.0f;
    float angularSpeed = 140.0f;
    float hardAngularSpeed = 360.0f;
    float jointSeparation = 1.75f;
    float hardJointSeparation = 6.0f;
    float linearViolation = 1.0f;
    float hardLinearViolation = 3.0f;
    float angularViolationDegrees = 115.0f;
    float hardAngularViolationDegrees = 175.0f;
    float runawayDistance = 5.0f;
    float normalizedExtension = 1.35f;
    float hardNormalizedExtension = 3.0f;
    float extensionGrowthTolerance = 0.025f;
    float runawaySupportSpeed = 8.0f;
    float severityGrowthTolerance = 0.03f;
};

struct MmdPhysicsCollisionPolicy
{
    bool enableNearNeighborFiltering = true;
    bool enableSkirtSemanticFiltering = true;
    float nearNeighborProximityFactor = 0.85f;
    std::size_t maximumContactDiagnostics = 64U;
    std::size_t skirtSelfCollisionGraphDistance = 4U;
    float skirtSelfCollisionProximityFactor = 1.15f;
};

struct MmdPhysicsCcdPolicy
{
    bool adaptive = true;
    float enableTravelFactor = 0.35f;
    float disableTravelFactor = 0.15f;
    float disableDelaySeconds = 0.25f;
};

struct MmdPhysicsChainTuning
{
    float gravityScale = 1.0f;
    float minimumLinearDamping = 0.0f;
    float minimumAngularDamping = 0.0f;
};

struct MmdPhysicsRuntimePolicy
{
    std::string name = "wisteria-adaptive-v1";
    MmdPhysicsStabilizationPolicy stabilization{};
    MmdPhysicsRecoveryPolicy recovery{};
    MmdPhysicsCollisionPolicy collision{};
    MmdPhysicsCcdPolicy ccd{};
    bool enableChainProfiles = true;

    MmdPhysicsChainTuning general{};
    MmdPhysicsChainTuning skirt{0.55f, 0.25f, 0.35f};
    MmdPhysicsChainTuning hair{0.65f, 0.18f, 0.28f};
    MmdPhysicsChainTuning tail{0.70f, 0.15f, 0.25f};
    MmdPhysicsChainTuning accessory{0.50f, 0.22f, 0.32f};
    MmdPhysicsChainTuning decorativeFallback{0.65f, 0.18f, 0.28f};

    MmdPhysicsGravityMode initialGravityMode = MmdPhysicsGravityMode::Balanced100;
    MmdPhysicsWithBoneSyncMode initialSyncMode =
        MmdPhysicsWithBoneSyncMode::RotationOnly;

    const MmdPhysicsChainTuning& ChainTuning(
        MmdPhysicsChainKind kind
    ) const noexcept;

    static MmdPhysicsRuntimePolicy WisteriaAdaptiveDefaults();
};
