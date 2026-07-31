#pragma once

#include "camera.hpp"
#include "entity.hpp"
#include "light.hpp"
#include <memory>
#include <vector>

// Scene owns scene objects. Mesh and Material stay externally owned resources.
class Scene
{
public:
    Scene() = default;
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    Camera& ActiveCamera() noexcept;
    const Camera& ActiveCamera() const noexcept;

    Entity& CreateEntity(
        Mesh& mesh,
        Material& material,
        const Transform& transform = {}
    );
    bool RemoveEntity(const Entity& entity);
    void ClearEntities() noexcept;

    PointLight& CreatePointLight(const PointLightData& data = {});
    bool RemovePointLight(const PointLight& light);
    void ClearPointLights() noexcept;

    DirectionalLight& CreateDirectionalLight(
        const DirectionalLightData& data = {}
    );
    bool RemoveDirectionalLight(const DirectionalLight& light);
    void ClearDirectionalLights() noexcept;

    SpotLight& CreateSpotLight(const SpotLightData& data = {});
    bool RemoveSpotLight(const SpotLight& light);
    void ClearSpotLights() noexcept;

    std::vector<std::unique_ptr<Entity>>& Entities() noexcept;
    const std::vector<std::unique_ptr<Entity>>& Entities() const noexcept;

    std::vector<std::unique_ptr<PointLight>>& PointLights() noexcept;
    const std::vector<std::unique_ptr<PointLight>>& PointLights() const noexcept;

    std::vector<std::unique_ptr<DirectionalLight>>& DirectionalLights() noexcept;
    const std::vector<std::unique_ptr<DirectionalLight>>& DirectionalLights() const noexcept;

    std::vector<std::unique_ptr<SpotLight>>& SpotLights() noexcept;
    const std::vector<std::unique_ptr<SpotLight>>& SpotLights() const noexcept;

private:
    Camera activeCamera;
    std::vector<std::unique_ptr<Entity>> entities;
    std::vector<std::unique_ptr<PointLight>> pointLights;
    std::vector<std::unique_ptr<DirectionalLight>> directionalLights;
    std::vector<std::unique_ptr<SpotLight>> spotLights;
};
