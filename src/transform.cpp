#include "pch.hpp"
#include "transform.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
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

glm::mat4 RotationMatrix(const glm::vec3& rotationDegrees)
{
    glm::mat4 matrix(1.0f);
    matrix = glm::rotate(
        matrix,
        glm::radians(rotationDegrees.x),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    matrix = glm::rotate(
        matrix,
        glm::radians(rotationDegrees.y),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    return glm::rotate(
        matrix,
        glm::radians(rotationDegrees.z),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );
}

glm::vec3 ExtractEulerAngleXYZ(const glm::mat4& matrix)
{
    const float first = std::atan2(matrix[2][1], matrix[2][2]);
    const float cosineSecond = std::sqrt(
        matrix[0][0] * matrix[0][0] +
        matrix[1][0] * matrix[1][0]
    );
    const float second = std::atan2(-matrix[2][0], cosineSecond);
    const float sineFirst = std::sin(first);
    const float cosineFirst = std::cos(first);
    const float third = std::atan2(
        sineFirst * matrix[0][2] - cosineFirst * matrix[0][1],
        cosineFirst * matrix[1][1] - sineFirst * matrix[1][2]
    );
    return glm::vec3(-first, -second, -third);
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
    matrix *= RotationMatrix(this->rotationDegrees);
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

void Transform::ApplyLocalMotion(const RootMotionDelta& motion)
{
    ValidateFinite(motion.translation, "Root motion translation");
    if (!std::isfinite(motion.rotation.w) ||
        !std::isfinite(motion.rotation.x) ||
        !std::isfinite(motion.rotation.y) ||
        !std::isfinite(motion.rotation.z))
    {
        throw std::invalid_argument("Root motion rotation must be finite");
    }
    const float rotationLength = glm::length(motion.rotation);
    if (!std::isfinite(rotationLength) || rotationLength <= 0.000001f)
        throw std::invalid_argument("Root motion rotation must be non-zero");

    // Root motion is authored in model-local units. Entity scale and current
    // orientation therefore affect its resulting world-space displacement.
    const glm::mat3 localToWorld(this->Matrix());
    this->SetPosition(
        this->position + localToWorld * motion.translation
    );

    const glm::mat4 combinedRotation =
        RotationMatrix(this->rotationDegrees) *
        glm::mat4_cast(motion.rotation / rotationLength);
    this->SetRotation(glm::degrees(
        ExtractEulerAngleXYZ(combinedRotation)
    ));
}
