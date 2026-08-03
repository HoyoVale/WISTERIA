#pragma once

#include "wisteria/mmd/physics/mmd_physics_modes.hpp"

#include <glm/glm.hpp>
#include <string>

// Tunable settings for the Saba-style MMD compat runtime. This is intentionally
// much smaller than the legacy MmdPhysicsRuntimePolicy: no recovery, no chain
// profiles, no semantic collision filtering.
struct MmdCompatSettings
{
    std::string name = "mmd-compat-v1";

    glm::vec3 gravity{0.0f, -9.8f, 0.0f};
    float fixedTimeStep = 1.0f / 60.0f;
    int maxSubSteps = 4;

    // Bullet 2.75 compatibility knobs (P0 experiment).
    bool legacySpringConstraint = false;
    bool disableOffsetForConstraintFrame = false;
    float constraintStopErp = 0.475f;
    bool disableDynamicDeactivation = false;
    bool disableLinkedBodyCollisions = true;

    // Joint-violation severity thresholds, matching the legacy runtime so
    // A/B diagnostics are directly comparable.
    float failureLinearViolation = 0.5f;
    float failureAngularViolationDegrees = 45.0f;

    MmdPhysicsWithBoneSyncMode physicsWithBoneSync =
        MmdPhysicsWithBoneSyncMode::RotationOnly;
};
