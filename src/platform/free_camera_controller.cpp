#include "wisteria/common/pch.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/platform/input.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wisteria
{
namespace
{
void ValidateDeltaTime(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        throw std::invalid_argument(
            "Behaviour delta time must be finite and non-negative"
        );
}
}

FreeCameraControllerBehaviour::FreeCameraControllerBehaviour(
    Camera& camera,
    Input& input,
    const FreeCameraControllerSettings& settings
)
    : camera(camera),
      input(input),
      initialCameraParam(camera.GetParam())
{
    this->SetSettings(settings);
    this->SynchronizeDirection();
}

void FreeCameraControllerBehaviour::Update(float deltaTime)
{
    ValidateDeltaTime(deltaTime);

    if (this->input.WasMouseButtonPressed(InputMouseButton::Right))
    {
        this->input.SetCursorCaptured(!this->input.IsCursorCaptured());
    }
    if (this->input.WasKeyPressed(InputKey::Escape))
        this->input.SetCursorCaptured(false);
    if (this->input.WasKeyPressed(InputKey::R))
    {
        this->Reset();
        return;
    }

    const double scrollDelta = this->input.ScrollDeltaY();
    if (scrollDelta != 0.0)
    {
        const float nextFov = std::clamp(
            this->camera.VerticalFovDegrees() -
                static_cast<float>(scrollDelta) *
                this->settings.scrollSensitivity,
            this->settings.minimumFovDegrees,
            this->settings.maximumFovDegrees
        );
        this->camera.SetVerticalFovDegrees(nextFov);
    }

    bool directionChanged = false;
    if (this->input.IsCursorCaptured())
    {
        const MouseDelta mouseDelta = this->input.CursorDelta();
        if (mouseDelta.x != 0.0 || mouseDelta.y != 0.0)
        {
            this->yawDegrees +=
                static_cast<float>(mouseDelta.x) *
                this->settings.mouseSensitivity;
            this->pitchDegrees = std::clamp(
                this->pitchDegrees -
                    static_cast<float>(mouseDelta.y) *
                    this->settings.mouseSensitivity,
                this->settings.minimumPitchDegrees,
                this->settings.maximumPitchDegrees
            );
            directionChanged = true;
        }
    }

    const glm::vec3 forward = this->Forward();
    const glm::vec3 up = this->camera.Up();
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 movement(0.0f);

    if (this->input.IsKeyDown(InputKey::W)) movement += forward;
    if (this->input.IsKeyDown(InputKey::S)) movement -= forward;
    if (this->input.IsKeyDown(InputKey::D)) movement += right;
    if (this->input.IsKeyDown(InputKey::A)) movement -= right;
    if (this->input.IsKeyDown(InputKey::E)) movement += up;
    if (this->input.IsKeyDown(InputKey::Q)) movement -= up;

    const float movementLengthSquared = glm::dot(movement, movement);
    const bool positionChanged = movementLengthSquared > 0.000001f;
    CameraParam next = this->camera.GetParam();

    if (positionChanged)
    {
        const float speed = this->settings.moveSpeed *
            (this->input.IsKeyDown(InputKey::LeftShift)
                ? this->settings.sprintMultiplier
                : 1.0f);
        next.Position += glm::normalize(movement) * speed * deltaTime;
    }

    if (directionChanged || positionChanged)
    {
        next.Target = next.Position + forward * this->targetDistance;
        this->camera.SetParam(next);
    }
}

void FreeCameraControllerBehaviour::Reset()
{
    this->camera.SetParam(this->initialCameraParam);
    this->SynchronizeDirection();
}

const FreeCameraControllerSettings&
FreeCameraControllerBehaviour::Settings() const noexcept
{
    return this->settings;
}

void FreeCameraControllerBehaviour::SetSettings(
    const FreeCameraControllerSettings& settings
)
{
    const bool valid =
        std::isfinite(settings.moveSpeed) && settings.moveSpeed > 0.0f &&
        std::isfinite(settings.sprintMultiplier) &&
            settings.sprintMultiplier >= 1.0f &&
        std::isfinite(settings.mouseSensitivity) &&
            settings.mouseSensitivity > 0.0f &&
        std::isfinite(settings.scrollSensitivity) &&
            settings.scrollSensitivity > 0.0f &&
        std::isfinite(settings.minimumFovDegrees) &&
        std::isfinite(settings.maximumFovDegrees) &&
        settings.minimumFovDegrees > 1.0f &&
        settings.maximumFovDegrees < 179.0f &&
        settings.minimumFovDegrees < settings.maximumFovDegrees &&
        std::isfinite(settings.minimumPitchDegrees) &&
        std::isfinite(settings.maximumPitchDegrees) &&
        settings.minimumPitchDegrees > -90.0f &&
        settings.maximumPitchDegrees < 90.0f &&
        settings.minimumPitchDegrees < settings.maximumPitchDegrees;

    if (!valid)
        throw std::invalid_argument("Free-camera controller settings are invalid");

    this->settings = settings;
}

void FreeCameraControllerBehaviour::SynchronizeDirection()
{
    const glm::vec3 direction =
        this->camera.Target() - this->camera.Position();
    this->targetDistance = glm::length(direction);
    const glm::vec3 normalizedDirection = direction / this->targetDistance;
    this->pitchDegrees = glm::degrees(std::asin(std::clamp(
        normalizedDirection.y,
        -1.0f,
        1.0f
    )));
    this->yawDegrees = glm::degrees(std::atan2(
        normalizedDirection.z,
        normalizedDirection.x
    ));
}

glm::vec3 FreeCameraControllerBehaviour::Forward() const
{
    const float yaw = glm::radians(this->yawDegrees);
    const float pitch = glm::radians(this->pitchDegrees);
    return glm::normalize(glm::vec3(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)
    ));
}
}  // namespace wisteria
