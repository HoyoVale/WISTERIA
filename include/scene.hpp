#pragma once

#include "camera.hpp"
#include "entity.hpp"
#include "light.hpp"
#include "model_asset.hpp"
#include <cstddef>
#include <memory>
#include <vector>

class EnvironmentMap;

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

    void Update(float deltaTime);

    Entity& CreateEntity(const Transform& transform = {});
    Entity& CreateEntity(
        Mesh& mesh,
        Material& material,
        const Transform& transform = {}
    );
    Entity& InstantiateModel(
        const ModelAsset& model,
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

    void SetEnvironment(EnvironmentMap* environment) noexcept;
    void ClearEnvironment() noexcept;
    EnvironmentMap* Environment() noexcept;
    const EnvironmentMap* Environment() const noexcept;

    std::size_t EntityCount() const noexcept;
    Entity* EntityAt(std::size_t index) noexcept;
    const Entity* EntityAt(std::size_t index) const noexcept;
    const std::vector<std::unique_ptr<Entity>>& Entities() const noexcept;

    const std::vector<std::unique_ptr<PointLight>>& PointLights() const noexcept;

    const std::vector<std::unique_ptr<DirectionalLight>>& DirectionalLights() const noexcept;

    const std::vector<std::unique_ptr<SpotLight>>& SpotLights() const noexcept;

private:
    Camera activeCamera;
    std::vector<std::unique_ptr<Entity>> entities;
    std::vector<std::unique_ptr<PointLight>> pointLights;
    std::vector<std::unique_ptr<DirectionalLight>> directionalLights;
    std::vector<std::unique_ptr<SpotLight>> spotLights;
    EnvironmentMap* environment = nullptr;
};
