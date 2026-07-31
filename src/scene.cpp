#include "pch.hpp"
#include "scene.hpp"
#include <algorithm>

namespace
{
template<typename T>
bool RemoveOwnedObject(
    std::vector<std::unique_ptr<T>>& objects,
    const T& object
)
{
    const auto iterator = std::find_if(
        objects.begin(),
        objects.end(),
        [&object](const std::unique_ptr<T>& candidate)
        {
            return candidate.get() == &object;
        }
    );

    if (iterator == objects.end())
        return false;

    objects.erase(iterator);
    return true;
}
}

Camera& Scene::ActiveCamera() noexcept
{
    return this->activeCamera;
}

const Camera& Scene::ActiveCamera() const noexcept
{
    return this->activeCamera;
}

Entity& Scene::CreateEntity(
    Mesh& mesh,
    Material& material,
    const Transform& transform
)
{
    auto entity = std::make_unique<Entity>(mesh, material, transform);
    Entity& result = *entity;
    this->entities.emplace_back(std::move(entity));
    return result;
}

bool Scene::RemoveEntity(const Entity& entity)
{
    return RemoveOwnedObject(this->entities, entity);
}

void Scene::ClearEntities() noexcept
{
    this->entities.clear();
}

PointLight& Scene::CreatePointLight(const PointLightData& data)
{
    auto light = std::make_unique<PointLight>(data);
    PointLight& result = *light;
    this->pointLights.emplace_back(std::move(light));
    return result;
}

bool Scene::RemovePointLight(const PointLight& light)
{
    return RemoveOwnedObject(this->pointLights, light);
}

void Scene::ClearPointLights() noexcept
{
    this->pointLights.clear();
}

DirectionalLight& Scene::CreateDirectionalLight(
    const DirectionalLightData& data
)
{
    auto light = std::make_unique<DirectionalLight>(data);
    DirectionalLight& result = *light;
    this->directionalLights.emplace_back(std::move(light));
    return result;
}

bool Scene::RemoveDirectionalLight(const DirectionalLight& light)
{
    return RemoveOwnedObject(this->directionalLights, light);
}

void Scene::ClearDirectionalLights() noexcept
{
    this->directionalLights.clear();
}

SpotLight& Scene::CreateSpotLight(const SpotLightData& data)
{
    auto light = std::make_unique<SpotLight>(data);
    SpotLight& result = *light;
    this->spotLights.emplace_back(std::move(light));
    return result;
}

bool Scene::RemoveSpotLight(const SpotLight& light)
{
    return RemoveOwnedObject(this->spotLights, light);
}

void Scene::ClearSpotLights() noexcept
{
    this->spotLights.clear();
}

std::vector<std::unique_ptr<Entity>>& Scene::Entities() noexcept
{
    return this->entities;
}

const std::vector<std::unique_ptr<Entity>>& Scene::Entities() const noexcept
{
    return this->entities;
}

std::vector<std::unique_ptr<PointLight>>& Scene::PointLights() noexcept
{
    return this->pointLights;
}

const std::vector<std::unique_ptr<PointLight>>& Scene::PointLights() const noexcept
{
    return this->pointLights;
}

std::vector<std::unique_ptr<DirectionalLight>>& Scene::DirectionalLights() noexcept
{
    return this->directionalLights;
}

const std::vector<std::unique_ptr<DirectionalLight>>& Scene::DirectionalLights() const noexcept
{
    return this->directionalLights;
}

std::vector<std::unique_ptr<SpotLight>>& Scene::SpotLights() noexcept
{
    return this->spotLights;
}

const std::vector<std::unique_ptr<SpotLight>>& Scene::SpotLights() const noexcept
{
    return this->spotLights;
}
