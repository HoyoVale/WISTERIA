#include "pch.hpp"
#include "transform.hpp"

#include <glm/gtc/matrix_transform.hpp>

Transform::Transform(
    const glm::vec3& position,
    const glm::vec3& rotationDegrees,
    const glm::vec3& scale
)
    : position(position),
      rotationDegrees(rotationDegrees),
      scale(scale)
{
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
    this->position = position;
}

void Transform::SetRotation(const glm::vec3& rotationDegrees)
{
    this->rotationDegrees = rotationDegrees;
}

void Transform::SetScale(const glm::vec3& scale)
{
    this->scale = scale;
}

void Transform::Translate(const glm::vec3& offset)
{
    this->position += offset;
}

void Transform::Rotate(const glm::vec3& offsetDegrees)
{
    this->rotationDegrees += offsetDegrees;
}

void Transform::ScaleBy(const glm::vec3& factor)
{
    this->scale *= factor;
}
