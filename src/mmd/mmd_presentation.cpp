#include "wisteria/common/pch.hpp"
#include "wisteria/mmd/mmd_presentation.hpp"
#include "wisteria/mmd/mmd_camera_conversion.hpp"
#include "wisteria/mmd/mmd_light_conversion.hpp"

#include <optional>

namespace wisteria
{
bool ApplyMmdCameraFrame(
    const MmdRuntimeModel& runtime,
    float frame,
    Camera& camera,
    const CameraParam& fallback
)
{
    const std::optional<CameraTrackSample> sample =
        runtime.SampleCameraMotion(frame);
    if (!sample.has_value())
        return false;
    camera.SetParam(ToCameraParam(*sample, fallback));
    return true;
}

bool ApplyMmdLightFrame(
    const MmdRuntimeModel& runtime,
    float frame,
    DirectionalLight& light,
    const DirectionalLightData& fallback
)
{
    const std::optional<LightTrackSample> sample =
        runtime.SampleLightMotion(frame);
    if (!sample.has_value())
        return false;
    const DirectionalLightData data = ToLightData(*sample, fallback);
    light.SetDirection(data.Direction);
    light.SetColor(data.Color);
    light.SetIntensity(data.Intensity);
    return true;
}
}  // namespace wisteria
