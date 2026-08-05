#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/rendering/light.hpp"

namespace wisteria
{
// WISTERIA application layer: converts a neutral MMD light sample into host
// light data. This is not a backend responsibility; backends only produce
// LightTrackSample and never write DirectionalLight objects directly.
//
// The fallback preserves host settings the sample does not carry (Intensity
// and any future fields), matching the old ApplyLightMotion behavior which
// only changed Color and Direction.
DirectionalLightData ToLightData(
    const LightTrackSample& sample,
    const DirectionalLightData& fallback
);
}  // namespace wisteria
