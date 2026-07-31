#pragma once
#include <glm/glm.hpp>

struct PointLightData
{
    glm::vec3 Position = glm::vec3(3.5f);
    glm::vec3 Color = glm::vec3(1.0f, 0.85f, 0.65f);
    float Intensity = 2.0f; // Linear brightness multiplier.
    float Range = 10.0f; // Effective light range.
    float Constant = 1.0f;
    float Linear = 0.09f;
    float Quadratic = 0.032f;
};


class PointLight{
public:
    explicit PointLight(const PointLightData &_data = {});
    ~PointLight() = default;

    const glm::vec3& Position() const noexcept;
    const glm::vec3& Color() const noexcept;
    float Intensity() const noexcept;
    float Range() const noexcept;
    float Constant() const noexcept;
    float Linear() const noexcept;
    float Quadratic() const noexcept;

    void SetPosition(const glm::vec3& position);
    void SetColor(const glm::vec3& color);
    void SetIntensity(float intensity);
    void SetRange(float range);
    void SetAttenuation(float constant, float linear, float quadratic);

    // Returns light color multiplied by its intensity.
    glm::vec3 Radiance() const noexcept;

private:
    PointLightData data;
};

struct DirectionalLightData
{
    // Direction in which light rays travel, from the source toward the scene.
    glm::vec3 Direction = {-0.2f, -1.0f, -0.3f};
    glm::vec3 Color = glm::vec3(1.0f);
    float Intensity = 0.35f;
};

class DirectionalLight
{
public:
    explicit DirectionalLight(const DirectionalLightData& data = {});
    ~DirectionalLight() = default;

    const glm::vec3& Direction() const noexcept;
    const glm::vec3& Color() const noexcept;
    float Intensity() const noexcept;

    void SetDirection(const glm::vec3& direction);
    void SetColor(const glm::vec3& color);
    void SetIntensity(float intensity);

    glm::vec3 Radiance() const noexcept;

private:
    DirectionalLightData data;
};

struct SpotLightData
{
    glm::vec3 Position = {2.5f, 2.5f, 3.0f};
    // Direction in which light rays travel, from the source toward the scene.
    glm::vec3 Direction = {-0.55f, -0.55f, -0.65f};
    glm::vec3 Color = glm::vec3(1.0f);
    float Intensity = 2.0f;
    float Range = 8.0f;
    float Constant = 1.0f;
    float Linear = 0.09f;
    float Quadratic = 0.032f;
    float InnerCutoffDegrees = 12.5f;
    float OuterCutoffDegrees = 20.0f;
};

class SpotLight
{
public:
    explicit SpotLight(const SpotLightData& data = {});
    ~SpotLight() = default;

    const glm::vec3& Position() const noexcept;
    const glm::vec3& Direction() const noexcept;
    const glm::vec3& Color() const noexcept;
    float Intensity() const noexcept;
    float Range() const noexcept;
    float Constant() const noexcept;
    float Linear() const noexcept;
    float Quadratic() const noexcept;
    float InnerCutoffDegrees() const noexcept;
    float OuterCutoffDegrees() const noexcept;
    float InnerCutoffCos() const noexcept;
    float OuterCutoffCos() const noexcept;

    void SetPosition(const glm::vec3& position);
    void SetDirection(const glm::vec3& direction);
    void SetColor(const glm::vec3& color);
    void SetIntensity(float intensity);
    void SetRange(float range);
    void SetAttenuation(float constant, float linear, float quadratic);
    void SetCutoff(float innerDegrees, float outerDegrees);

    glm::vec3 Radiance() const noexcept;

private:
    SpotLightData data;
};
