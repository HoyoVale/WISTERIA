#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/camera.hpp"
#include <cmath>
#include <stdexcept>

namespace wisteria
{
namespace
{
bool IsFinite(const glm::vec3& value)
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

CameraParam ValidateCameraParam(const CameraParam& cameraParam)
{
    if (!IsFinite(cameraParam.Position) ||
        !IsFinite(cameraParam.Target) ||
        !IsFinite(cameraParam.Up) ||
        !std::isfinite(cameraParam.VerticalFovDegrees) ||
        !std::isfinite(cameraParam.NearClip) ||
        !std::isfinite(cameraParam.FarClip))
    {
        throw std::invalid_argument("Camera parameters must contain finite values");
    }

    const glm::vec3 forward = cameraParam.Target - cameraParam.Position;
    if (glm::dot(forward, forward) <= 0.000001f)
        throw std::invalid_argument("Camera position and target must be different");

    const float upLengthSquared = glm::dot(cameraParam.Up, cameraParam.Up);
    if (upLengthSquared <= 0.000001f)
        throw std::invalid_argument("Camera up vector must be non-zero");

    const glm::vec3 side = glm::cross(forward, cameraParam.Up);
    if (glm::dot(side, side) <= 0.000001f)
        throw std::invalid_argument("Camera up vector must not be parallel to its view direction");
    if (cameraParam.VerticalFovDegrees <= 1.0f ||
        cameraParam.VerticalFovDegrees >= 179.0f)
    {
        throw std::invalid_argument("Camera vertical FOV must be between 1 and 179 degrees");
    }
    if (cameraParam.NearClip <= 0.0f ||
        cameraParam.FarClip <= cameraParam.NearClip)
    {
        throw std::invalid_argument("Camera clip planes must satisfy 0 < near < far");
    }

    CameraParam result = cameraParam;
    result.Up = glm::normalize(result.Up);
    return result;
}
}

Camera::Camera(const CameraParam& cameraParam)
{
    this->SetParam(cameraParam);
}

glm::mat4 Camera::GetView() const
{
    return glm::lookAt(
        this->param.Position,
        this->param.Target,
        this->param.Up
    );
}

glm::mat4 Camera::GetProjection(float aspect) const
{
    if (!std::isfinite(aspect) || aspect <= 0.0f)
        throw std::invalid_argument("Camera aspect ratio must be finite and positive");

    return glm::perspective(
        glm::radians(this->param.VerticalFovDegrees),
        aspect,
        this->param.NearClip,
        this->param.FarClip
    );
}

void Camera::SetParam(const CameraParam& cameraParam)
{
    this->param = ValidateCameraParam(cameraParam);
}

void Camera::SetPosition(const glm::vec3& position)
{
    CameraParam next = this->param;
    next.Position = position;
    this->SetParam(next);
}

void Camera::SetTarget(const glm::vec3& target)
{
    CameraParam next = this->param;
    next.Target = target;
    this->SetParam(next);
}

void Camera::SetUp(const glm::vec3& up)
{
    CameraParam next = this->param;
    next.Up = up;
    this->SetParam(next);
}

void Camera::SetVerticalFovDegrees(float verticalFovDegrees)
{
    CameraParam next = this->param;
    next.VerticalFovDegrees = verticalFovDegrees;
    this->SetParam(next);
}

void Camera::SetClipPlanes(float nearClip, float farClip)
{
    CameraParam next = this->param;
    next.NearClip = nearClip;
    next.FarClip = farClip;
    this->SetParam(next);
}
}  // namespace wisteria
