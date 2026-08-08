#include "wisteria/mmd/physics/mmd_physics_configuration.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace wisteria
{
namespace
{
bool IsKnownPreset(MmdPhysicsPreset preset) noexcept
{
    return preset == MmdPhysicsPreset::MmdRaw ||
        preset == MmdPhysicsPreset::MmdCommunity ||
        preset == MmdPhysicsPreset::WisteriaAdaptive;
}

bool IsImplementedLinkedBodyMode(MmdLinkedBodyCollisionMode mode) noexcept
{
    return mode == MmdLinkedBodyCollisionMode::PmxMaskOnly ||
        mode == MmdLinkedBodyCollisionMode::DisableConstraintLinkedPairs;
}

bool IsImplementedMode2(MmdMode2WritebackMode mode) noexcept
{
    return mode == MmdMode2WritebackMode::PreserveAnimatedTranslation ||
        mode == MmdMode2WritebackMode::FullTransformDiagnostic;
}

bool IsFinite(float value) noexcept
{
    return std::isfinite(value) != 0;
}

// FNV-1a64 streaming helper; explicit little-endian byte writes keep the
// fingerprint stable across hosts of the same build (R1.2B convention).
struct ConfigurationHasher
{
    std::uint64_t state = 14695981039346656037ULL;

    void Byte(std::uint8_t byte) noexcept
    {
        state ^= byte;
        state *= 1099511628211ULL;
    }

    void U32(std::uint32_t value) noexcept
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            Byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    void I32(std::int32_t value) noexcept
    {
        U32(static_cast<std::uint32_t>(value));
    }

    void F32(float value) noexcept
    {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }

    void String(std::string_view text) noexcept
    {
        U32(static_cast<std::uint32_t>(text.size()));
        for (std::size_t index = 0U; index < text.size(); ++index)
        {
            Byte(static_cast<std::uint8_t>(text[index]));
        }
    }
};
}  // namespace

std::string_view ToPresetName(MmdPhysicsPreset preset) noexcept
{
    switch (preset)
    {
        case MmdPhysicsPreset::MmdRaw:
            return "MMD_RAW";
        case MmdPhysicsPreset::MmdCommunity:
            return "MMD_COMMUNITY";
        case MmdPhysicsPreset::WisteriaAdaptive:
            return "WISTERIA_ADAPTIVE";
    }
    return "UNKNOWN";
}

std::string ToPresetNameLower(MmdPhysicsPreset preset)
{
    std::string name(ToPresetName(preset));
    std::transform(
        name.begin(),
        name.end(),
        name.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    // Contract trace identities use hyphens ("mmd-raw-v1"), while the
    // display names use underscores ("MMD_RAW").
    std::replace(name.begin(), name.end(), '_', '-');
    return name;
}

MmdPhysicsConfiguration BuildPresetConfiguration(MmdPhysicsPreset preset)
{
    MmdPhysicsConfiguration configuration;
    configuration.identity.preset = preset;
    configuration.identity.profileRevision = 1U;
    // Phase 0A: every preset equals SabaBaseline v2. Struct defaults already
    // encode runtime.fixedTimeStep=1/120, maxSubSteps=10, gravity=-98,
    // PmxMaskOnly, PreserveAnimatedTranslation and all-adaptive-disabled.
    return configuration;
}

TimelineStatus DeriveDiagnosticConfiguration(
    const MmdPhysicsConfiguration& base,
    const MmdPhysicsDiagnosticOverrides& overrides,
    MmdPhysicsConfiguration& output
)
{
    if (!ValidateConfiguration(base))
        return TimelineStatus::InvalidState;
    if (overrides.linkedBodyCollision.has_value() &&
        !IsImplementedLinkedBodyMode(*overrides.linkedBodyCollision))
    {
        return TimelineStatus::InvalidState;
    }
    if (overrides.mode2.has_value() &&
        !IsImplementedMode2(*overrides.mode2))
    {
        return TimelineStatus::InvalidState;
    }

    MmdPhysicsConfiguration derived = base;
    if (overrides.linkedBodyCollision.has_value())
    {
        derived.compatibility.linkedBodyCollision =
            *overrides.linkedBodyCollision;
    }
    if (overrides.mode2.has_value())
    {
        derived.compatibility.mode2 = *overrides.mode2;
    }
    derived.identity.originPreset = ToPresetNameLower(base.identity.preset);
    output = std::move(derived);
    return TimelineStatus::Ok;
}

bool ValidateConfiguration(
    const MmdPhysicsConfiguration& config
) noexcept
{
    const MmdPhysicsProfileIdentity& identity = config.identity;
    if (identity.backend.empty() ||
        identity.baseline.empty() ||
        !IsKnownPreset(identity.preset) ||
        identity.profileRevision != 1U)
    {
        return false;
    }

    const MmdPhysicsRuntimeSettings& runtime = config.runtime;
    if (!(runtime.fixedTimeStep > 0.0f) ||
        !IsFinite(runtime.fixedTimeStep) ||
        runtime.maxSubSteps <= 0 ||
        !IsFinite(runtime.gravity.x) ||
        !IsFinite(runtime.gravity.y) ||
        !IsFinite(runtime.gravity.z))
    {
        return false;
    }

    const MmdPhysicsCompatibilityProfile& compatibility =
        config.compatibility;
    if (!(compatibility.gravityScale > 0.0f) ||
        !IsFinite(compatibility.gravityScale) ||
        compatibility.gravityScale != 1.0f ||
        !IsImplementedLinkedBodyMode(compatibility.linkedBodyCollision) ||
        !IsImplementedMode2(compatibility.mode2))
    {
        return false;
    }
    // Phase 0A: no adaptive enhancement is implemented on the Saba runtime.
    // A configuration claiming otherwise would describe behaviour that does
    // not exist while still changing the effective fingerprint.
    if (config.adaptive.recoveryEnabled ||
        config.adaptive.adaptiveCcdEnabled ||
        config.adaptive.adaptiveMarginEnabled ||
        config.adaptive.localChainEnhancementsEnabled)
    {
        return false;
    }
    // A direct preset label may only represent the exact frozen preset:
    // any behaviour deviation must carry a custom identity. Phase 0A has
    // exactly one known profile revision.
    const MmdPhysicsConfiguration preset =
        BuildPresetConfiguration(identity.preset);
    if (identity.originPreset.empty())
    {
        if (ComputeEffectiveConfigurationFingerprint(config) !=
            ComputeEffectiveConfigurationFingerprint(preset))
        {
            return false;
        }
    }
    else
    {
        if (identity.originPreset != ToPresetNameLower(identity.preset))
            return false;
        // custom-from-* configurations are allowed to carry legal runtime
        // overrides (gravity, fixedTimeStep, maxSubSteps, enabled) and the
        // implemented A/B switches. The numeric sanity checks above enforce
        // finite/positive values, gravityScale stays 1.0, adaptive slots stay
        // disabled, and Reserved modes stay rejected. The effective
        // fingerprint hashes all of these runtime behaviours, so a custom
        // label never hides behaviour from the machine identity.
    }
    // FullTransformDiagnostic must not enter a direct preset profile.
    if (identity.originPreset.empty() &&
        compatibility.mode2 == MmdMode2WritebackMode::FullTransformDiagnostic)
    {
        return false;
    }
    return true;
}

std::string FormatConfigurationIdentity(
    const MmdPhysicsConfiguration& config
)
{
    const MmdPhysicsProfileIdentity& identity = config.identity;
    std::string prefix;
    if (identity.originPreset.empty())
    {
        prefix = ToPresetNameLower(identity.preset);
    }
    else
    {
        prefix = "custom-from-" + identity.originPreset;
    }
    return prefix + "-v" + std::to_string(identity.profileRevision);
}

std::uint64_t ComputeEffectiveConfigurationFingerprint(
    const MmdPhysicsConfiguration& config
) noexcept
{
    ConfigurationHasher hasher;
    hasher.U32(MmdPhysicsConfigurationFingerprintVersion);
    hasher.String(config.identity.backend);
    hasher.String(config.identity.baseline);
    hasher.U32(config.runtime.enabled ? 1U : 0U);
    hasher.F32(config.compatibility.gravityScale);
    hasher.F32(config.runtime.gravity.x);
    hasher.F32(config.runtime.gravity.y);
    hasher.F32(config.runtime.gravity.z);
    hasher.F32(config.runtime.fixedTimeStep);
    hasher.I32(config.runtime.maxSubSteps);
    hasher.U32(
        static_cast<std::uint32_t>(config.compatibility.linkedBodyCollision)
    );
    hasher.U32(
        static_cast<std::uint32_t>(config.compatibility.mode2)
    );
    hasher.U32(config.adaptive.recoveryEnabled ? 1U : 0U);
    hasher.U32(config.adaptive.adaptiveCcdEnabled ? 1U : 0U);
    hasher.U32(config.adaptive.adaptiveMarginEnabled ? 1U : 0U);
    hasher.U32(config.adaptive.localChainEnhancementsEnabled ? 1U : 0U);
    return hasher.state;
}
}  // namespace wisteria
