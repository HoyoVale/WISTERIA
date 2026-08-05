#include "wisteria/common/pch.hpp"
#include "wisteria/mmd/mmd_camera_conversion.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace wisteria
{
namespace
{
// Mirrors saba::MMDLookAtCamera: translate by |distance| along +Z, then apply
// the MMD rotation order (Y, then -Z, then X) around the interest point.
struct LookAtResult
{
    glm::vec3 center{0.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

LookAtResult ComputeMmdLookAt(
    const glm::vec3& interest,
    const glm::vec3& rotationRadians,
    float distance
)
{
    glm::mat4 view(1.0f);
    view = glm::translate(view, glm::vec3(0.0f, 0.0f, std::abs(distance)));
    glm::mat4 rotation(1.0f);
    rotation = glm::rotate(
        rotation,
        rotationRadians.y,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    rotation = glm::rotate(
        rotation,
        rotationRadians.z,
        glm::vec3(0.0f, 0.0f, -1.0f)
    );
    rotation = glm::rotate(
        rotation,
        rotationRadians.x,
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    view = rotation * view;

    LookAtResult result;
    result.eye = glm::vec3(view[3]) + interest;
    result.center = glm::mat3(view) * glm::vec3(0.0f, 0.0f, -1.0f) +
        result.eye;
    result.up = glm::mat3(view) * glm::vec3(0.0f, 1.0f, 0.0f);
    return result;
}
}  // namespace

CameraParam ToCameraParam(
    const CameraTrackSample& sample,
    const CameraParam& fallback
)
{
    const LookAtResult look = ComputeMmdLookAt(
        sample.interest,
        glm::radians(sample.rotation),
        sample.distance
    );

    CameraParam param = fallback;
    param.Position = look.eye;
    param.Target = look.center;
    param.Up = look.up;
    // The host camera only supports perspective. When the backend reports a
    // projection mode it is applied; otherwise the fallback FOV is kept.
    if (sample.perspective.value_or(true))
    {
        param.VerticalFovDegrees = sample.viewAngle;
    }
    return param;
}
}  // namespace wisteria
