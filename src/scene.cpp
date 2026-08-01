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

void Scene::Update(float deltaTime)
{
    for (const std::unique_ptr<Entity>& entity : this->entities)
        entity->Update(deltaTime);
}

void Scene::Clear() noexcept
{
    this->ClearEntities();
    this->ClearPointLights();
    this->ClearDirectionalLights();
    this->ClearSpotLights();
    this->ClearEnvironment();
}

Entity& Scene::CreateEntity(const Transform& transform)
{
    auto entity = std::make_unique<Entity>(transform);
    Entity& result = *entity;
    this->entities.emplace_back(std::move(entity));
    return result;
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

Entity& Scene::InstantiateModel(
    const ModelAsset& model,
    const Transform& transform
)
{
    Entity& entity = this->CreateEntity(transform);
    if (model.HasMorphs())
        entity.SetMorphSet(model.GetMorphSet());
    if (model.HasSkeleton())
    {
        entity.SetSkeleton(model.GetSkeleton());
        if (model.AnimationClipCount() > 0)
            entity.GetAnimator().Play(model.AnimationClipAt(0));
    }
    for (const RenderPart& part : model.Parts())
    {
        entity.AddRenderPart(
            part.GetMesh(),
            part.GetMaterial(),
            part.LocalTransform()
        );
    }
    return entity;
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

void Scene::SetEnvironment(EnvironmentMap* environment) noexcept
{
    this->environment = environment;
}

void Scene::ClearEnvironment() noexcept
{
    this->environment = nullptr;
}

EnvironmentMap* Scene::Environment() noexcept
{
    return this->environment;
}

const EnvironmentMap* Scene::Environment() const noexcept
{
    return this->environment;
}

std::size_t Scene::EntityCount() const noexcept
{
    return this->entities.size();
}

Entity* Scene::EntityAt(std::size_t index) noexcept
{
    return index < this->entities.size() ? this->entities[index].get() : nullptr;
}

const Entity* Scene::EntityAt(std::size_t index) const noexcept
{
    return index < this->entities.size() ? this->entities[index].get() : nullptr;
}

const std::vector<std::unique_ptr<Entity>>& Scene::Entities() const noexcept
{
    return this->entities;
}

const std::vector<std::unique_ptr<PointLight>>& Scene::PointLights() const noexcept
{
    return this->pointLights;
}

const std::vector<std::unique_ptr<DirectionalLight>>& Scene::DirectionalLights() const noexcept
{
    return this->directionalLights;
}

const std::vector<std::unique_ptr<SpotLight>>& Scene::SpotLights() const noexcept
{
    return this->spotLights;
}
