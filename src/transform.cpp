#include "pch.hpp"
#include "transform.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <string>

namespace
{
bool IsFinite(const glm::vec3& value)
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

void ValidateFinite(const glm::vec3& value, const char* name)
{
    if (!IsFinite(value))
        throw std::invalid_argument(std::string(name) + " must contain finite values");
}

void ValidateScale(const glm::vec3& scale)
{
    ValidateFinite(scale, "Transform scale");
    constexpr float MinimumMagnitude = 0.000001f;
    if (std::abs(scale.x) <= MinimumMagnitude ||
        std::abs(scale.y) <= MinimumMagnitude ||
        std::abs(scale.z) <= MinimumMagnitude)
    {
        throw std::invalid_argument("Transform scale components must be non-zero");
    }
}
}

Transform::Transform(
    const glm::vec3& position,
    const glm::vec3& rotationDegrees,
    const glm::vec3& scale
)
{
    this->SetPosition(position);
    this->SetRotation(rotationDegrees);
    this->SetScale(scale);
}

glm::mat4 Transform::Matrix() const
{
    glm::mat4 matrix(1.0f);

    matrix = glm::translate(matrix, this->position);
    matrix = glm::rotate(
        matrix,
        glm::radians(this->rotationDegrees.x),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    matrix = glm::rotate(
        matrix,
        glm::radians(this->rotationDegrees.y),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    matrix = glm::rotate(
        matrix,
        glm::radians(this->rotationDegrees.z),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );
    matrix = glm::scale(matrix, this->scale);

    return matrix;
}

const glm::vec3& Transform::Position() const noexcept
{
    return this->position;
}

const glm::vec3& Transform::Rotation() const noexcept
{
    return this->rotationDegrees;
}

const glm::vec3& Transform::Scale() const noexcept
{
    return this->scale;
}

void Transform::SetPosition(const glm::vec3& position)
{
    ValidateFinite(position, "Transform position");
    this->position = position;
}

void Transform::SetRotation(const glm::vec3& rotationDegrees)
{
    ValidateFinite(rotationDegrees, "Transform rotation");
    this->rotationDegrees = rotationDegrees;
}

void Transform::SetScale(const glm::vec3& scale)
{
    ValidateScale(scale);
    this->scale = scale;
}

void Transform::Translate(const glm::vec3& offset)
{
    this->SetPosition(this->position + offset);
}

void Transform::Rotate(const glm::vec3& offsetDegrees)
{
    this->SetRotation(this->rotationDegrees + offsetDegrees);
}

void Transform::ScaleBy(const glm::vec3& factor)
{
    this->SetScale(this->scale * factor);
}
