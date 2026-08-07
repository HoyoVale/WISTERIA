#pragma once

#include "wisteria/animation/bone.hpp"
#include "wisteria/mmd/physics/mmd_physics_asset.hpp"
#include "wisteria/mmd/physics/mmd_physics_configuration.hpp"

#include <cstddef>
#include <span>

#include <glm/vec3.hpp>

namespace wisteria
{
// R1.3 §7 unit audit: measure only, never modify behaviour. All real values
// must be finite; empty collections report available=false/count=0; negative
// joint lower limits and zero-length helpers are legal data. "Reasonable
// magnitude" is a diagnostic warning, not an assertion.

struct MmdPhysicsAuditRange
{
    bool available = false;
    // False when any input sample was non-finite (or negative, which is
    // invalid for these non-negative metrics). Bad samples are counted, not
    // silently dropped.
    bool finite = true;
    std::size_t count = 0U;
    std::size_t nonFiniteCount = 0U;
    std::size_t zeroCount = 0U;
    float minPositive = 0.0f;
    float median = 0.0f;
    float p95 = 0.0f;
    float max = 0.0f;
};

struct MmdPhysicsAuditBounds
{
    bool available = false;
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct MmdPhysicsAuditResult
{
    bool finite = true;
    MmdPhysicsAuditBounds modelBounds;
    bool modelHeightAvailable = false;
    float modelHeight = 0.0f;
    MmdPhysicsAuditRange boneLength;
    MmdPhysicsAuditRange rigidBodySize;
    MmdPhysicsAuditRange jointLinearRange;
    MmdPhysicsAuditRange jointAngularRangeDeg;
    bool gravityAvailable = false;
    float gravityMagnitude = 0.0f;
    bool gravityPerModelHeightAvailable = false;
    float gravityPerModelHeight = 0.0f;
    float fixedTimeStep = 0.0f;
    bool shapeMarginRatioAvailable = false;
    float shapeMarginPerMedianBodySize = 0.0f;
};

struct MmdPhysicsAuditOptions
{
    // Bullet collision margin used for the shape-margin ratio; 0 (default)
    // keeps the ratio unavailable.
    float collisionMargin = 0.0f;
};

MmdPhysicsAuditResult RunMmdPhysicsAudit(
    const MmdPhysicsAsset& asset,
    std::span<const Bone> bones,
    const MmdPhysicsConfiguration& configuration,
    const MmdPhysicsAuditBounds& modelBounds = {},
    const MmdPhysicsAuditOptions& options = {}
);
}  // namespace wisteria
