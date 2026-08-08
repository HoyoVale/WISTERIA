#pragma once

#include "wisteria/runtime/determinism.hpp"
#include "wisteria/runtime/runtime_creation_options.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <glm/vec3.hpp>

namespace wisteria
{
// R1.3 Phase 0A (frozen 2026-08-07): MMD physics compatibility & backend
// governance. These neutral types are the single authoritative configuration
// source; the Saba adapter translates them into runtime settings.
// Contract: docs/architecture/R1_3_MMD_COMPAT_CONTRACT.md

// The stable physics settings subset is shared with the generic runtime
// layer (R1.4); MMD keeps it as an alias so both layers stay in sync.
using MmdPhysicsRuntimeSettings = RuntimePhysicsSettings;

enum class MmdPhysicsPreset : std::uint8_t
{
    MmdRaw = 0,           // SabaBaseline + no community/adaptive overrides
    MmdCommunity = 1,     // Phase 0A identical to MmdRaw; evidence-gated later
    WisteriaAdaptive = 2  // Phase 0A identical to MmdRaw; reserved slots only
};

// fingerprint v2 schema version (R1.3 contract §5).
inline constexpr std::uint32_t MmdPhysicsConfigurationFingerprintVersion = 2U;

struct MmdPhysicsProfileIdentity
{
    std::string backend = "saba-mmd";
    // R1.2C canonicalization integrity revision (aa662dd): SimpleBroadphase
    // + canonical collision-world rebuild. v1 (2808dab, DBVT) is historical.
    std::string baseline = "saba-baseline-v2";
    MmdPhysicsPreset preset = MmdPhysicsPreset::MmdRaw;
    std::uint32_t profileRevision = 1U;
    // Non-empty for DeriveDiagnosticConfiguration outputs, legacy constructor
    // overrides and low-level SetPhysicsSettings/SetMmdPhysicsSettings
    // overrides; records the source preset label (e.g. "mmd-raw").
    // Human/Trace identity only and never hashed into the effective
    // fingerprint (R1.3 §5).
    std::string originPreset;
};

enum class MmdLinkedBodyCollisionMode : std::uint8_t
{
    PmxMaskOnly = 0,                     // current Saba baseline
    DisableConstraintLinkedPairs = 1,    // PMX mask AND !constraint-linked
    ForceEnableLinkedPairsDiagnostic = 2 // Reserved (Phase 0A)
};

enum class MmdMode2WritebackMode : std::uint8_t
{
    PreserveAnimatedTranslation = 0,  // current Saba baseline
    StrictBoneLength = 1,             // Reserved (Phase 0A)
    FullTransformDiagnostic = 2       // diagnostic only
};

struct MmdPhysicsCompatibilityProfile
{
    // "PMX/MMD/community standard interpretation" fields only.
    float gravityScale = 1.0f;  // audited later; Phase 0A keeps behaviour
    MmdLinkedBodyCollisionMode linkedBodyCollision =
        MmdLinkedBodyCollisionMode::PmxMaskOnly;
    MmdMode2WritebackMode mode2 =
        MmdMode2WritebackMode::PreserveAnimatedTranslation;
};

struct MmdPhysicsAdaptivePolicy
{
    // WISTERIA-owned enhancements. Phase 0A: all unsupported/disabled; the
    // legacy MmdPhysicsInstance adaptive path is not ported to Saba.
    bool recoveryEnabled = false;
    bool adaptiveCcdEnabled = false;
    bool adaptiveMarginEnabled = false;
    bool localChainEnhancementsEnabled = false;
};

struct MmdPhysicsTraceOptions
{
    // Records Mode 2 translation delta; never changes physics behaviour.
    // Reserved for Phase 0B trace tooling: Phase 0A trace already records
    // the full Mode 2 transform, so no configuration carries this option.
    bool recordMode2TranslationDelta = false;
};

struct MmdPhysicsConfiguration
{
    MmdPhysicsProfileIdentity identity;
    MmdPhysicsRuntimeSettings runtime;
    MmdPhysicsCompatibilityProfile compatibility;
    MmdPhysicsAdaptivePolicy adaptive;
};

struct MmdPhysicsDiagnosticOverrides
{
    std::optional<MmdLinkedBodyCollisionMode> linkedBodyCollision;
    std::optional<MmdMode2WritebackMode> mode2;
};

// Normal-run construction entry. Phase 0A default preset is MmdRaw; every
// preset expands to SabaBaseline v1 (zero behaviour change).
MmdPhysicsConfiguration BuildPresetConfiguration(MmdPhysicsPreset preset);

// Diagnostic experiments must be derived from a preset and always carry the
// origin identity. Reserved modes (ForceEnableLinkedPairsDiagnostic,
// StrictBoneLength) are rejected. On failure output is left unchanged.
TimelineStatus DeriveDiagnosticConfiguration(
    const MmdPhysicsConfiguration& base,
    const MmdPhysicsDiagnosticOverrides& overrides,
    MmdPhysicsConfiguration& output
);

// Rejects anonymous configurations (empty backend/baseline, unknown preset,
// zero revision) and invalid values (non-positive timestep/substeps,
// non-finite gravity/scale, reserved or diagnostic-only modes in direct
// presets).
bool ValidateConfiguration(
    const MmdPhysicsConfiguration& config
) noexcept;

std::string_view ToPresetName(MmdPhysicsPreset preset) noexcept;

// Lowercase hyphenated preset label ("mmd-raw"), used for origin identities.
std::string ToPresetNameLower(MmdPhysicsPreset preset);

// Stable trace/log identity: "mmd-raw-v1" for direct presets and
// "custom-from-mmd-raw-v1" for derived diagnostic configurations.
std::string FormatConfigurationIdentity(
    const MmdPhysicsConfiguration& config
);

// fingerprint v2: hashes only fields that can change simulation results.
// Preset display labels, originPreset and profileRevision are excluded, so
// behaviour-identical presets share one effective hash (R1.3 §5).
std::uint64_t ComputeEffectiveConfigurationFingerprint(
    const MmdPhysicsConfiguration& config
) noexcept;
}  // namespace wisteria
