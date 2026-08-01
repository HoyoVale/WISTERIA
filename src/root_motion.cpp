#include "pch.hpp"
#include "root_motion.hpp"

#include <cmath>

bool RootMotionDelta::IsIdentity(float epsilon) const noexcept
{
    if (!std::isfinite(epsilon) || epsilon < 0.0f)
        return false;
    if (!std::isfinite(this->translation.x) ||
        !std::isfinite(this->translation.y) ||
        !std::isfinite(this->translation.z) ||
        !std::isfinite(this->rotation.w) ||
        !std::isfinite(this->rotation.x) ||
        !std::isfinite(this->rotation.y) ||
        !std::isfinite(this->rotation.z))
    {
        return false;
    }
    const float rotationLength = glm::length(this->rotation);
    if (!std::isfinite(rotationLength) || rotationLength <= 0.0f)
        return false;
    const glm::quat normalized = this->rotation / rotationLength;
    return glm::length(this->translation) <= epsilon &&
        std::abs(std::abs(normalized.w) - 1.0f) <= epsilon;
}
