#include "pch.hpp"
#include "camera.hpp"

Camera::Camera(const CameraParam& cameraParam)
    : param(cameraParam)
{
}

glm::mat4 Camera::GetView() const
{
    return glm::lookAt(
        this->param.Position,
        this->param.Target,
        this->param.Up
    );
}
