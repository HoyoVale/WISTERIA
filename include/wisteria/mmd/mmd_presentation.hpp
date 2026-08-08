#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/light.hpp"

namespace wisteria
{
// R1.6 Phase 0D: orchestration-layer MMD presentation adapter.
//
// MMD Runtime only supplies neutral samples (SampleCameraMotion /
// SampleLightMotion); the host decides whether and how to apply them. These
// helpers are the reusable path so the demo is no longer the only place that
// applies VMD camera/light. Renderer never samples VMD.

// Samples the runtime's camera track at `frame` and applies the converted
// host camera parameters. Returns false (and leaves the camera untouched)
// when no camera track is loaded or the sample is unavailable. `fallback`
// preserves host settings the sample does not carry (clip planes, etc.).
bool ApplyMmdCameraFrame(
    const MmdRuntimeModel& runtime,
    float frame,
    Camera& camera,
    const CameraParam& fallback
);

// Samples the runtime's light track at `frame` and applies the converted
// host directional-light state. Returns false (and leaves the light
// untouched) when no light track is loaded or the sample is unavailable.
// `fallback` preserves host settings the sample does not carry (Intensity).
bool ApplyMmdLightFrame(
    const MmdRuntimeModel& runtime,
    float frame,
    DirectionalLight& light,
    const DirectionalLightData& fallback
);
}  // namespace wisteria
