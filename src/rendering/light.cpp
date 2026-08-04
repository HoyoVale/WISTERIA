#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/light.hpp"
#include <cmath>
#include <stdexcept>

namespace wisteria
{
namespace
{
glm::vec3 NormalizeLightDirection(const glm::vec3& direction)
{
    const float lengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
        throw std::invalid_argument("Light direction must be a finite non-zero vector");

    return glm::normalize(direction);
}
}

PointLight::PointLight(const PointLightData &_data)
    :data(_data)
{
    this->SetColor(_data.Color);
    this->SetIntensity(_data.Intensity);
    this->SetRange(_data.Range);
    this->SetAttenuation(_data.Constant, _data.Linear, _data.Quadratic);
}

const glm::vec3& PointLight::Position() const noexcept
{
    return this->data.Position;
}

const glm::vec3& PointLight::Color() const noexcept
{
    return this->data.Color;
}

float PointLight::Intensity() const noexcept
{
    return this->data.Intensity;
}

float PointLight::Range() const noexcept
{
    return this->data.Range;
}

float PointLight::Constant() const noexcept
{
    return this->data.Constant;
}

float PointLight::Linear() const noexcept
{
    return this->data.Linear;
}

float PointLight::Quadratic() const noexcept
{
    return this->data.Quadratic;
}

void PointLight::SetPosition(const glm::vec3& position)
{
    this->data.Position = position;
}

void PointLight::SetColor(const glm::vec3& color)
{
    this->data.Color = glm::max(color, glm::vec3(0.0f));
}

void PointLight::SetIntensity(float intensity)
{
    this->data.Intensity = glm::max(intensity, 0.0f);
}

void PointLight::SetRange(float range)
{
    this->data.Range = glm::max(range, 0.001f);
}

void PointLight::SetAttenuation(
    float constant,
    float linear,
    float quadratic
)
{
    this->data.Constant = glm::max(constant, 0.001f);
    this->data.Linear = glm::max(linear, 0.0f);
    this->data.Quadratic = glm::max(quadratic, 0.0f);
}

glm::vec3 PointLight::Radiance() const noexcept
{
    return this->data.Color * this->data.Intensity;
}

DirectionalLight::DirectionalLight(const DirectionalLightData& data)
    : data(data)
{
    this->SetDirection(data.Direction);
    this->SetColor(data.Color);
    this->SetIntensity(data.Intensity);
}

const glm::vec3& DirectionalLight::Direction() const noexcept
{
    return this->data.Direction;
}

const glm::vec3& DirectionalLight::Color() const noexcept
{
    return this->data.Color;
}

float DirectionalLight::Intensity() const noexcept
{
    return this->data.Intensity;
}

void DirectionalLight::SetDirection(const glm::vec3& direction)
{
    this->data.Direction = NormalizeLightDirection(direction);
}

void DirectionalLight::SetColor(const glm::vec3& color)
{
    this->data.Color = glm::max(color, glm::vec3(0.0f));
}

void DirectionalLight::SetIntensity(float intensity)
{
    this->data.Intensity = glm::max(intensity, 0.0f);
}

glm::vec3 DirectionalLight::Radiance() const noexcept
{
    return this->data.Color * this->data.Intensity;
}

SpotLight::SpotLight(const SpotLightData& data)
    : data(data)
{
    this->SetDirection(data.Direction);
    this->SetColor(data.Color);
    this->SetIntensity(data.Intensity);
    this->SetRange(data.Range);
    this->SetAttenuation(data.Constant, data.Linear, data.Quadratic);
    this->SetCutoff(data.InnerCutoffDegrees, data.OuterCutoffDegrees);
}

const glm::vec3& SpotLight::Position() const noexcept
{
    return this->data.Position;
}

const glm::vec3& SpotLight::Direction() const noexcept
{
    return this->data.Direction;
}

const glm::vec3& SpotLight::Color() const noexcept
{
    return this->data.Color;
}

float SpotLight::Intensity() const noexcept
{
    return this->data.Intensity;
}

float SpotLight::Range() const noexcept
{
    return this->data.Range;
}

float SpotLight::Constant() const noexcept
{
    return this->data.Constant;
}

float SpotLight::Linear() const noexcept
{
    return this->data.Linear;
}

float SpotLight::Quadratic() const noexcept
{
    return this->data.Quadratic;
}

float SpotLight::InnerCutoffDegrees() const noexcept
{
    return this->data.InnerCutoffDegrees;
}

float SpotLight::OuterCutoffDegrees() const noexcept
{
    return this->data.OuterCutoffDegrees;
}

float SpotLight::InnerCutoffCos() const noexcept
{
    return glm::cos(glm::radians(this->data.InnerCutoffDegrees));
}

float SpotLight::OuterCutoffCos() const noexcept
{
    return glm::cos(glm::radians(this->data.OuterCutoffDegrees));
}

void SpotLight::SetPosition(const glm::vec3& position)
{
    this->data.Position = position;
}

void SpotLight::SetDirection(const glm::vec3& direction)
{
    this->data.Direction = NormalizeLightDirection(direction);
}

void SpotLight::SetColor(const glm::vec3& color)
{
    this->data.Color = glm::max(color, glm::vec3(0.0f));
}

void SpotLight::SetIntensity(float intensity)
{
    this->data.Intensity = glm::max(intensity, 0.0f);
}

void SpotLight::SetRange(float range)
{
    this->data.Range = glm::max(range, 0.001f);
}

void SpotLight::SetAttenuation(
    float constant,
    float linear,
    float quadratic
)
{
    this->data.Constant = glm::max(constant, 0.001f);
    this->data.Linear = glm::max(linear, 0.0f);
    this->data.Quadratic = glm::max(quadratic, 0.0f);
}

void SpotLight::SetCutoff(float innerDegrees, float outerDegrees)
{
    this->data.InnerCutoffDegrees = glm::clamp(innerDegrees, 0.0f, 89.0f);
    this->data.OuterCutoffDegrees = glm::clamp(
        outerDegrees,
        this->data.InnerCutoffDegrees + 0.1f,
        89.9f
    );
}

glm::vec3 SpotLight::Radiance() const noexcept
{
    return this->data.Color * this->data.Intensity;
}
}  // namespace wisteria
