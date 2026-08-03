#pragma once

#include <cstdint>

enum class MmdPhysicsDebugOverlay : std::uint8_t
{
    Off,
    BindPose,
    ResetPose,
    Runtime,
    All
};

enum class MmdPhysicsFidelityDebugLayer : std::uint8_t
{
    Off,
    Bone,
    Vertex,
    All
};

enum class MmdPhysicsWithBoneSyncMode : std::uint8_t
{
    RotationOnly,
    FullBody,
    TranslationDelta
};

enum class MmdPhysicsGravityMode : std::uint8_t
{
    Original,
    Balanced100,
    Balanced075,
    Balanced050,
    Balanced025,
    Zero
};

enum class MmdPhysicsChainKind : std::uint8_t
{
    General,
    Skirt,
    Hair,
    Tail,
    Accessory,
    DecorativeFallback
};

enum class MmdPhysicsRecoveryReason : std::uint8_t
{
    None,
    NonFinite,
    NonFiniteJoint,
    ExtremeVelocity,
    HighVelocity,
    Runaway,
    JointViolation
};
