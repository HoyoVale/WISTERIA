#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Local-space translation and rotation produced by one Animator update.
// Scale intentionally remains an Entity/Transform property.
namespace wisteria
{
struct RootMotionDelta
{
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    bool IsIdentity(float epsilon = 0.000001f) const noexcept;
};
}  // namespace wisteria
