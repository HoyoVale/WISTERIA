#include "wisteria/common/pch.hpp"
#include "wisteria/mmd/mmd_light_conversion.hpp"

#include <glm/glm.hpp>

namespace wisteria
{
DirectionalLightData ToLightData(
    const LightTrackSample& sample,
    const DirectionalLightData& fallback
)
{
    DirectionalLightData data = fallback;
    data.Color = glm::clamp(
        sample.color,
        glm::vec3(0.0f),
        glm::vec3(1.0f)
    );
    const float positionLength = glm::length(sample.position);
    data.Direction = positionLength > 0.000001f
        ? -sample.position / positionLength
        : glm::vec3(0.0f, -1.0f, 0.0f);
    return data;
}
}  // namespace wisteria
