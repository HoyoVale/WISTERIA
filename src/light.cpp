#include "pch.hpp"
#include "light.hpp"

PointLight::PointLight(const LightData &_data)
    :data(_data)
{
    this->SetColor(_data.Color);
    this->SetIntensity(_data.Intensity);
    this->SetRange(_data.Range);
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
    this->data.Range = glm::max(range, 0.0f);
}

glm::vec3 PointLight::Radiance() const noexcept
{
    return this->data.Color * this->data.Intensity;
}
