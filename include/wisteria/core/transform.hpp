#pragma once

#include "wisteria/core/root_motion.hpp"
#include <glm/glm.hpp>

namespace wisteria
{
class Transform {
public:
    Transform(
        const glm::vec3& position = {},
        const glm::vec3& rotationDegrees = {},
        const glm::vec3& scale = glm::vec3(1.0f)
    );

    glm::mat4 Matrix() const;

    const glm::vec3& Position() const noexcept;
    const glm::vec3& Rotation() const noexcept;
    const glm::vec3& Scale() const noexcept;

    void SetPosition(const glm::vec3& position);
    void SetRotation(const glm::vec3& rotationDegrees);
    void SetScale(const glm::vec3& scale);

    void Translate(const glm::vec3& offset);
    void Rotate(const glm::vec3& offsetDegrees);
    void ScaleBy(const glm::vec3& factor);
    void ApplyLocalMotion(const RootMotionDelta& motion);

private:
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
};
}  // namespace wisteria
