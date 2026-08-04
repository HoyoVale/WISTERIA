#include "wisteria/common/pch.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/scene/entity.hpp"
#include <algorithm>
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

void ValidateVector(const glm::vec3& value, const char* name)
{
    if (!IsFinite(value))
        throw std::invalid_argument(std::string(name) + " must contain finite values");
}

void ValidateDeltaTime(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        throw std::invalid_argument("Behaviour delta time must be finite and non-negative");
}
}

Behaviour::~Behaviour() = default;

bool Behaviour::IsEnabled() const noexcept
{
    return this->enabled;
}

void Behaviour::SetEnabled(bool enabled) noexcept
{
    this->enabled = enabled;
}

void Behaviour::OnAttach(Entity&) noexcept
{
}

void Behaviour::OnDetach(Entity&) noexcept
{
}

void Behaviour::OnEvent(Entity&, const Event&)
{
}

MoveBehaviour::MoveBehaviour(const glm::vec3& velocity)
{
    this->SetVelocity(velocity);
}

const glm::vec3& MoveBehaviour::Velocity() const noexcept
{
    return this->velocity;
}

void MoveBehaviour::SetVelocity(const glm::vec3& velocity)
{
    ValidateVector(velocity, "Move velocity");
    this->velocity = velocity;
}

void MoveBehaviour::Update(Entity& entity, float deltaTime)
{
    ValidateDeltaTime(deltaTime);
    entity.GetTransform().Translate(this->velocity * deltaTime);
}

RotateBehaviour::RotateBehaviour(const glm::vec3& angularVelocityDegrees)
{
    this->SetAngularVelocity(angularVelocityDegrees);
}

const glm::vec3& RotateBehaviour::AngularVelocity() const noexcept
{
    return this->angularVelocityDegrees;
}

void RotateBehaviour::SetAngularVelocity(
    const glm::vec3& angularVelocityDegrees
)
{
    ValidateVector(angularVelocityDegrees, "Angular velocity");
    this->angularVelocityDegrees = angularVelocityDegrees;
}

void RotateBehaviour::Update(Entity& entity, float deltaTime)
{
    ValidateDeltaTime(deltaTime);
    entity.GetTransform().Rotate(
        this->angularVelocityDegrees * deltaTime
    );
}

ScaleBehaviour::ScaleBehaviour(const glm::vec3& multiplierPerSecond)
{
    this->SetMultiplierPerSecond(multiplierPerSecond);
}

const glm::vec3& ScaleBehaviour::MultiplierPerSecond() const noexcept
{
    return this->multiplierPerSecond;
}

void ScaleBehaviour::SetMultiplierPerSecond(
    const glm::vec3& multiplierPerSecond
)
{
    ValidateVector(multiplierPerSecond, "Scale multiplier");
    if (multiplierPerSecond.x <= 0.0f ||
        multiplierPerSecond.y <= 0.0f ||
        multiplierPerSecond.z <= 0.0f)
    {
        throw std::invalid_argument("Scale multipliers must be greater than zero");
    }
    this->multiplierPerSecond = multiplierPerSecond;
}

void ScaleBehaviour::Update(Entity& entity, float deltaTime)
{
    ValidateDeltaTime(deltaTime);
    const glm::vec3 factor{
        std::pow(this->multiplierPerSecond.x, deltaTime),
        std::pow(this->multiplierPerSecond.y, deltaTime),
        std::pow(this->multiplierPerSecond.z, deltaTime)
    };
    entity.GetTransform().ScaleBy(factor);
}

}  // namespace wisteria
