#pragma once

#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/rendering/camera.hpp"

namespace wisteria
{
// WISTERIA application layer: converts a neutral MMD camera sample into host
// camera parameters. Replicates the MMD look-at convention that Saba's
// MMDLookAtCamera implements; this is MMD semantics, not generic engine
// camera logic, so it lives in the mmd module.
//
// The fallback camera parameter preserves host settings the sample does not
// carry (NearClip/FarClip and any other fields). Only perspective cameras are
// supported by the current host camera; an orthographic MMD camera
// (perspective == false) is reported as unsupported and falls back to the
// fallback's projection settings while still applying the look-at pose.
CameraParam ToCameraParam(
    const CameraTrackSample& sample,
    const CameraParam& fallback
);
}  // namespace wisteria
