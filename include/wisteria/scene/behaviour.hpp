#pragma once

#include "wisteria/rendering/camera.hpp"
#include <glm/glm.hpp>

namespace wisteria
{
class Entity;
class Event;
class Input;

// A unit of per-entity update logic. Entity will own Behaviour instances and
// invoke these lifecycle methods when the update/event systems are connected.
class Behaviour
{
public:
    Behaviour() = default;
    virtual ~Behaviour();

    Behaviour(const Behaviour&) = delete;
    Behaviour& operator=(const Behaviour&) = delete;
    Behaviour(Behaviour&&) = delete;
    Behaviour& operator=(Behaviour&&) = delete;

    bool IsEnabled() const noexcept;
    void SetEnabled(bool enabled) noexcept;

    virtual void OnAttach(Entity& entity) noexcept;
    virtual void OnDetach(Entity& entity) noexcept;
    virtual void Update(Entity& entity, float deltaTime) = 0;
    virtual void OnEvent(Entity& entity, const Event& event);

private:
    bool enabled = true;
};

class MoveBehaviour final : public Behaviour
{
public:
    explicit MoveBehaviour(const glm::vec3& velocity = glm::vec3(0.0f));

    const glm::vec3& Velocity() const noexcept;
    void SetVelocity(const glm::vec3& velocity);
    void Update(Entity& entity, float deltaTime) override;

private:
    glm::vec3 velocity{0.0f};
};

class RotateBehaviour final : public Behaviour
{
public:
    explicit RotateBehaviour(
        const glm::vec3& angularVelocityDegrees = glm::vec3(0.0f)
    );

    const glm::vec3& AngularVelocity() const noexcept;
    void SetAngularVelocity(const glm::vec3& angularVelocityDegrees);
    void Update(Entity& entity, float deltaTime) override;

private:
    glm::vec3 angularVelocityDegrees{0.0f};
};

class ScaleBehaviour final : public Behaviour
{
public:
    // A value of 2 doubles that axis in one second; 0.5 halves it.
    explicit ScaleBehaviour(
        const glm::vec3& multiplierPerSecond = glm::vec3(1.0f)
    );

    const glm::vec3& MultiplierPerSecond() const noexcept;
    void SetMultiplierPerSecond(const glm::vec3& multiplierPerSecond);
    void Update(Entity& entity, float deltaTime) override;

private:
    glm::vec3 multiplierPerSecond{1.0f};
};

struct FreeCameraControllerSettings
{
    float moveSpeed = 2.5f;
    float sprintMultiplier = 3.0f;
    float mouseSensitivity = 0.1f;
    float scrollSensitivity = 2.0f;
    float minimumFovDegrees = 15.0f;
    float maximumFovDegrees = 90.0f;
    float minimumPitchDegrees = -89.0f;
    float maximumPitchDegrees = 89.0f;
};

// Camera-level update behaviour. It intentionally does not derive from the
// Entity Behaviour base because a Camera is not an Entity.
class FreeCameraControllerBehaviour final
{
public:
    FreeCameraControllerBehaviour(
        Camera& camera,
        Input& input,
        const FreeCameraControllerSettings& settings = {}
    );

    void Update(float deltaTime);
    void Reset();

    const FreeCameraControllerSettings& Settings() const noexcept;
    void SetSettings(const FreeCameraControllerSettings& settings);

private:
    void SynchronizeDirection();
    glm::vec3 Forward() const;

private:
    Camera& camera;
    Input& input;
    CameraParam initialCameraParam;
    FreeCameraControllerSettings settings;
    float yawDegrees = -90.0f;
    float pitchDegrees = 0.0f;
    float targetDistance = 1.0f;
};
}  // namespace wisteria
