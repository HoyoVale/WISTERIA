#pragma once

#include "wisteria/rendering/camera.hpp"
#include "wisteria/scene/entity.hpp"
#include "wisteria/rendering/light.hpp"
#include "wisteria/assets/model_asset.hpp"
#include "wisteria/physics/physics_world.hpp"
#include <cstddef>
#include <memory>
#include <vector>

class EnvironmentMap;

struct ModelInstantiationOptions
{
    bool enablePhysics = true;
};

// Scene owns scene objects. Mesh and Material stay externally owned resources.
class Scene
{
public:
    Scene() = default;
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&& other) noexcept;

    Camera& ActiveCamera() noexcept;
    const Camera& ActiveCamera() const noexcept;
    PhysicsWorld& Physics() noexcept;
    const PhysicsWorld& Physics() const noexcept;
    const PhysicsFrameStatistics& LastPhysicsFrameStatistics() const noexcept;

    void Update(float deltaTime);
    void Clear() noexcept;

    Entity& CreateEntity(const Transform& transform = {});
    Entity& CreateEntity(
        Mesh& mesh,
        Material& material,
        const Transform& transform = {}
    );
    Entity& InstantiateModel(
        const ModelAsset& model,
        const Transform& transform = {},
        const ModelInstantiationOptions& options = {}
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
    std::unique_ptr<PhysicsWorld> physicsWorld =
        std::make_unique<PhysicsWorld>();
    std::vector<std::unique_ptr<Entity>> entities;
    std::vector<std::unique_ptr<PointLight>> pointLights;
    std::vector<std::unique_ptr<DirectionalLight>> directionalLights;
    std::vector<std::unique_ptr<SpotLight>> spotLights;
    EnvironmentMap* environment = nullptr;
    double physicsAccumulator = 0.0;
    PhysicsFrameStatistics physicsFrameStatistics{};
};
