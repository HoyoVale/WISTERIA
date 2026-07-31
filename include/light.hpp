#pragma once
#include <glm/glm.hpp>

struct LightData
{
    glm::vec3 Position = glm::vec3(5.0f);
    glm::vec3 Color = glm::vec3(1.0f, 0.8f, 0.6f);
    float Intensity = 2.0f; // Linear brightness multiplier.
    float Range = 10.0f; // Effective light range.
};


class PointLight{
public:
    explicit PointLight(const LightData &_data = {});
    ~PointLight() = default;

    const glm::vec3& Position() const noexcept;
    const glm::vec3& Color() const noexcept;
    float Intensity() const noexcept;
    float Range() const noexcept;

    void SetPosition(const glm::vec3& position);
    void SetColor(const glm::vec3& color);
    void SetIntensity(float intensity);
    void SetRange(float range);

    // 方便 Renderer 获取最终光照颜色。
    glm::vec3 Radiance() const noexcept;

private:
    LightData data;
};

