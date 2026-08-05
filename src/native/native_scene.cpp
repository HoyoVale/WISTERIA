#include "wisteria/native/wisteria_native.h"
#include "internal/native_context.hpp"

#include "wisteria/platform/application.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/rendering/primitives/cube.hpp"
#include "wisteria/rendering/primitives/procedural.hpp"
#include "wisteria/scene/scene.hpp"

#include <cmath>
#include <filesystem>
#include <string>

using namespace wisteria::native;
using namespace wisteria;

extern "C"
{
enum WisteriaStatus wisteria_scene_create(
    WisteriaContext context,
    WisteriaWindow window,
    WisteriaScene* out_scene
)
{
    if (out_scene == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    *out_scene = 0U;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* windowEntry = FindWindow(*handle, window);
    if (windowEntry == nullptr || windowEntry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    if (handle->application == nullptr)
        return InvalidHandle(*handle, "Context has no application");

    try
    {
        auto scene = handle->application->CreateScene();
        handle->application->BindScene(*windowEntry->window, scene);
        // The frame loop only renders windows marked as "loaded"; a
        // frontend-built scene is a renderable scene like the demo.
        windowEntry->demoLoaded = true;
        auto entry = std::make_unique<SceneEntry>();
        entry->scene = std::move(scene);
        entry->window = windowEntry->window;
        const WisteriaScene sceneHandle = handle->nextSceneHandle++;
        handle->scenes.emplace(sceneHandle, std::move(entry));
        *out_scene = sceneHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        SetError(*handle, "Unknown C++ exception while creating the scene");
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_scene_destroy(
    WisteriaContext context,
    WisteriaScene scene
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    const auto iterator = handle->scenes.find(scene);
    if (iterator == handle->scenes.end())
        return InvalidHandle(*handle, "Scene handle is invalid");

    try
    {
        // Release the window's reference by binding a fresh empty scene so
        // the old scene (and its entities/lights) is destroyed immediately.
        Window* window = iterator->second->window;
        auto replacement = handle->application->CreateScene();
        if (window != nullptr)
            handle->application->BindScene(*window, replacement);
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    handle->scenes.erase(iterator);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_scene_load_model(
    WisteriaContext context,
    WisteriaScene scene,
    const char* model_path,
    WisteriaSceneModel* out_model
)
{
    if (out_model != nullptr)
        *out_model = 0U;
    if (model_path == nullptr || model_path[0] == '\0' ||
        out_model == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");

    const std::filesystem::path path = PathFromUtf8(model_path);
    if (path.empty())
    {
        SetError(*handle, "Model path is not valid UTF-8");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    if (!std::filesystem::is_regular_file(path))
    {
        SetError(*handle, "Model file does not exist: " +
            std::string(model_path));
        return WISTERIA_ERROR_IO;
    }

    try
    {
        const std::string resourceName = "scene:" + std::to_string(scene) +
            ":model:" + std::to_string(entry->nextModelHandle);
        ModelAsset& model = handle->application->GetResources().LoadModel(
            resourceName,
            path
        );
        const WisteriaSceneModel modelHandle = entry->nextModelHandle++;
        entry->models.emplace(modelHandle, &model);
        *out_model = modelHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        SetError(*handle, "Unknown C++ exception while loading the model");
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_scene_unload_model(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaSceneModel model
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->models.find(model);
    if (iterator == entry->models.end())
        return InvalidHandle(*handle, "Scene model handle is invalid");
    // The asset stays in the application resource cache (shared by the
    // context); this invalidates the handle and detaches it from the scene.
    entry->models.erase(iterator);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_scene_instantiate_model(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaSceneModel model,
    const float position[3],
    const float euler_radians[3],
    const float scale[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (position == nullptr || euler_radians == nullptr ||
        scale == nullptr || out_entity == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto modelIterator = entry->models.find(model);
    if (modelIterator == entry->models.end())
        return InvalidHandle(*handle, "Scene model handle is invalid");

    try
    {
        const Transform transform(
            glm::vec3(position[0], position[1], position[2]),
            glm::vec3(
                euler_radians[0],
                euler_radians[1],
                euler_radians[2]
            ),
            glm::vec3(scale[0], scale[1], scale[2])
        );
        Entity& entity = entry->scene->InstantiateModel(
            *modelIterator->second,
            transform
        );
        const WisteriaEntity entityHandle = entry->nextEntityHandle++;
        entry->entities.emplace(entityHandle, &entity);
        *out_entity = entityHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_entity_set_transform(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const float position[3],
    const float euler_radians[3],
    const float scale[3]
)
{
    if (position == nullptr || euler_radians == nullptr ||
        scale == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->entities.find(entity);
    if (iterator == entry->entities.end())
        return InvalidHandle(*handle, "Entity handle is invalid");
    iterator->second->GetTransform() = Transform(
        glm::vec3(position[0], position[1], position[2]),
        glm::vec3(
            euler_radians[0],
            euler_radians[1],
            euler_radians[2]
        ),
        glm::vec3(scale[0], scale[1], scale[2])
    );
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_get_transform(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    float out_position[3],
    float out_euler_degrees[3],
    float out_scale[3]
)
{
    if (out_position == nullptr || out_euler_degrees == nullptr ||
        out_scale == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->entities.find(entity);
    if (iterator == entry->entities.end())
        return InvalidHandle(*handle, "Entity handle is invalid");
    const Transform& transform = iterator->second->GetTransform();
    const glm::vec3& position = transform.Position();
    const glm::vec3& rotation = transform.Rotation();
    const glm::vec3& scale = transform.Scale();
    out_position[0] = position.x;
    out_position[1] = position.y;
    out_position[2] = position.z;
    out_euler_degrees[0] = rotation.x;
    out_euler_degrees[1] = rotation.y;
    out_euler_degrees[2] = rotation.z;
    out_scale[0] = scale.x;
    out_scale[1] = scale.y;
    out_scale[2] = scale.z;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_get_visible(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t* out_visible
)
{
    if (out_visible == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->entities.find(entity);
    if (iterator == entry->entities.end())
        return InvalidHandle(*handle, "Entity handle is invalid");
    *out_visible = iterator->second->IsVisible() ? 1 : 0;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_set_visible(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t visible
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->entities.find(entity);
    if (iterator == entry->entities.end())
        return InvalidHandle(*handle, "Entity handle is invalid");
    iterator->second->SetVisible(visible != 0);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_destroy(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->entities.find(entity);
    if (iterator == entry->entities.end())
        return InvalidHandle(*handle, "Entity handle is invalid");
    entry->scene->RemoveEntity(*iterator->second);
    entry->entities.erase(iterator);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_scene_add_directional_light(
    WisteriaContext context,
    WisteriaScene scene,
    const float direction[3],
    const float color[3],
    float intensity,
    WisteriaLight* out_light
)
{
    if (out_light != nullptr)
        *out_light = 0U;
    if (direction == nullptr || color == nullptr || out_light == nullptr ||
        !std::isfinite(intensity))
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    DirectionalLight& light = entry->scene->CreateDirectionalLight(
        DirectionalLightData{
            .Direction = glm::vec3(direction[0], direction[1], direction[2]),
            .Color = glm::vec3(color[0], color[1], color[2]),
            .Intensity = intensity
        }
    );
    const WisteriaLight lightHandle = entry->nextLightHandle++;
    entry->lights.emplace(
        lightHandle,
        SceneEntry::LightEntry{0, &light}
    );
    *out_light = lightHandle;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_scene_add_point_light(
    WisteriaContext context,
    WisteriaScene scene,
    const float position[3],
    const float color[3],
    float intensity,
    float range,
    WisteriaLight* out_light
)
{
    if (out_light != nullptr)
        *out_light = 0U;
    if (position == nullptr || color == nullptr || out_light == nullptr ||
        !std::isfinite(intensity) || !std::isfinite(range) || range <= 0.0f)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    PointLight& light = entry->scene->CreatePointLight(
        PointLightData{
            .Position = glm::vec3(position[0], position[1], position[2]),
            .Color = glm::vec3(color[0], color[1], color[2]),
            .Intensity = intensity,
            .Range = range
        }
    );
    const WisteriaLight lightHandle = entry->nextLightHandle++;
    entry->lights.emplace(
        lightHandle,
        SceneEntry::LightEntry{1, &light}
    );
    *out_light = lightHandle;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_light_destroy(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->lights.find(light);
    if (iterator == entry->lights.end())
        return InvalidHandle(*handle, "Light handle is invalid");
    const SceneEntry::LightEntry& stored = iterator->second;
    if (stored.light == nullptr)
        return InvalidHandle(*handle, "Light handle is invalid");
    if (stored.kind == 0)
        entry->scene->RemoveDirectionalLight(
            *static_cast<DirectionalLight*>(stored.light)
        );
    else if (stored.kind == 1)
        entry->scene->RemovePointLight(
            *static_cast<PointLight*>(stored.light)
        );
    else
        entry->scene->RemoveSpotLight(
            *static_cast<SpotLight*>(stored.light)
        );
    entry->lights.erase(iterator);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_directional_light_set(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    const float direction[3],
    const float color[3],
    float intensity
)
{
    if (direction == nullptr || color == nullptr ||
        !std::isfinite(intensity))
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->lights.find(light);
    if (iterator == entry->lights.end() ||
        iterator->second.kind != 0 || iterator->second.light == nullptr)
    {
        return InvalidHandle(*handle, "Directional light handle is invalid");
    }
    DirectionalLight* stored =
        static_cast<DirectionalLight*>(iterator->second.light);
    stored->SetDirection(
        glm::vec3(direction[0], direction[1], direction[2])
    );
    stored->SetColor(glm::vec3(color[0], color[1], color[2]));
    stored->SetIntensity(intensity);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_directional_light_get(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    float out_direction[3],
    float out_color[3],
    float* out_intensity
)
{
    if (out_direction == nullptr || out_color == nullptr ||
        out_intensity == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->lights.find(light);
    if (iterator == entry->lights.end() ||
        iterator->second.kind != 0 || iterator->second.light == nullptr)
    {
        return InvalidHandle(*handle, "Directional light handle is invalid");
    }
    const DirectionalLight* stored =
        static_cast<const DirectionalLight*>(iterator->second.light);
    const glm::vec3& direction = stored->Direction();
    const glm::vec3& color = stored->Color();
    out_direction[0] = direction.x;
    out_direction[1] = direction.y;
    out_direction[2] = direction.z;
    out_color[0] = color.x;
    out_color[1] = color.y;
    out_color[2] = color.z;
    *out_intensity = stored->Intensity();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_point_light_set(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    const float position[3],
    const float color[3],
    float intensity,
    float range
)
{
    if (position == nullptr || color == nullptr ||
        !std::isfinite(intensity) || !std::isfinite(range) || range <= 0.0f)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->lights.find(light);
    if (iterator == entry->lights.end() ||
        iterator->second.kind != 1 || iterator->second.light == nullptr)
    {
        return InvalidHandle(*handle, "Point light handle is invalid");
    }
    PointLight* stored = static_cast<PointLight*>(iterator->second.light);
    stored->SetPosition(
        glm::vec3(position[0], position[1], position[2])
    );
    stored->SetColor(glm::vec3(color[0], color[1], color[2]));
    stored->SetIntensity(intensity);
    stored->SetRange(range);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_point_light_get(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    float out_position[3],
    float out_color[3],
    float* out_intensity,
    float* out_range
)
{
    if (out_position == nullptr || out_color == nullptr ||
        out_intensity == nullptr || out_range == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->lights.find(light);
    if (iterator == entry->lights.end() ||
        iterator->second.kind != 1 || iterator->second.light == nullptr)
    {
        return InvalidHandle(*handle, "Point light handle is invalid");
    }
    const PointLight* stored =
        static_cast<const PointLight*>(iterator->second.light);
    const glm::vec3& position = stored->Position();
    const glm::vec3& color = stored->Color();
    out_position[0] = position.x;
    out_position[1] = position.y;
    out_position[2] = position.z;
    out_color[0] = color.x;
    out_color[1] = color.y;
    out_color[2] = color.z;
    *out_intensity = stored->Intensity();
    *out_range = stored->Range();
    return WISTERIA_OK;
}

namespace
{
bool ValidSpotCutoff(float inner, float outer)
{
    return std::isfinite(inner) && std::isfinite(outer) &&
        inner >= 0.0f && outer > inner && outer <= 90.0f;
}
}

enum WisteriaStatus wisteria_scene_add_spot_light(
    WisteriaContext context,
    WisteriaScene scene,
    const float position[3],
    const float direction[3],
    const float color[3],
    float intensity,
    float range,
    float inner_cutoff_degrees,
    float outer_cutoff_degrees,
    WisteriaLight* out_light
)
{
    if (out_light != nullptr)
        *out_light = 0U;
    if (position == nullptr || direction == nullptr ||
        color == nullptr || out_light == nullptr ||
        !std::isfinite(intensity) || !std::isfinite(range) || range <= 0.0f ||
        !ValidSpotCutoff(inner_cutoff_degrees, outer_cutoff_degrees))
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    SpotLight& light = entry->scene->CreateSpotLight(
        SpotLightData{
            .Position = glm::vec3(position[0], position[1], position[2]),
            .Direction = glm::vec3(
                direction[0],
                direction[1],
                direction[2]
            ),
            .Color = glm::vec3(color[0], color[1], color[2]),
            .Intensity = intensity,
            .Range = range,
            .InnerCutoffDegrees = inner_cutoff_degrees,
            .OuterCutoffDegrees = outer_cutoff_degrees
        }
    );
    const WisteriaLight lightHandle = entry->nextLightHandle++;
    entry->lights.emplace(
        lightHandle,
        SceneEntry::LightEntry{2, &light}
    );
    *out_light = lightHandle;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_spot_light_set(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    const float position[3],
    const float direction[3],
    const float color[3],
    float intensity,
    float range,
    float inner_cutoff_degrees,
    float outer_cutoff_degrees
)
{
    if (position == nullptr || direction == nullptr ||
        color == nullptr ||
        !std::isfinite(intensity) || !std::isfinite(range) || range <= 0.0f ||
        !ValidSpotCutoff(inner_cutoff_degrees, outer_cutoff_degrees))
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->lights.find(light);
    if (iterator == entry->lights.end() ||
        iterator->second.kind != 2 || iterator->second.light == nullptr)
    {
        return InvalidHandle(*handle, "Spot light handle is invalid");
    }
    SpotLight* stored = static_cast<SpotLight*>(iterator->second.light);
    stored->SetPosition(
        glm::vec3(position[0], position[1], position[2])
    );
    stored->SetDirection(
        glm::vec3(direction[0], direction[1], direction[2])
    );
    stored->SetColor(glm::vec3(color[0], color[1], color[2]));
    stored->SetIntensity(intensity);
    stored->SetRange(range);
    stored->SetCutoff(inner_cutoff_degrees, outer_cutoff_degrees);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_spot_light_get(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    float out_position[3],
    float out_direction[3],
    float out_color[3],
    float* out_intensity,
    float* out_range,
    float* out_inner_cutoff_degrees,
    float* out_outer_cutoff_degrees
)
{
    if (out_position == nullptr || out_direction == nullptr ||
        out_color == nullptr || out_intensity == nullptr ||
        out_range == nullptr || out_inner_cutoff_degrees == nullptr ||
        out_outer_cutoff_degrees == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->lights.find(light);
    if (iterator == entry->lights.end() ||
        iterator->second.kind != 2 || iterator->second.light == nullptr)
    {
        return InvalidHandle(*handle, "Spot light handle is invalid");
    }
    const SpotLight* stored =
        static_cast<const SpotLight*>(iterator->second.light);
    const glm::vec3& position = stored->Position();
    const glm::vec3& direction = stored->Direction();
    const glm::vec3& color = stored->Color();
    out_position[0] = position.x;
    out_position[1] = position.y;
    out_position[2] = position.z;
    out_direction[0] = direction.x;
    out_direction[1] = direction.y;
    out_direction[2] = direction.z;
    out_color[0] = color.x;
    out_color[1] = color.y;
    out_color[2] = color.z;
    *out_intensity = stored->Intensity();
    *out_range = stored->Range();
    *out_inner_cutoff_degrees = stored->InnerCutoffDegrees();
    *out_outer_cutoff_degrees = stored->OuterCutoffDegrees();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_set_morph_weight(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const char* morph_name,
    float weight
)
{
    if (morph_name == nullptr || morph_name[0] == '\0' ||
        !std::isfinite(weight))
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->entities.find(entity);
    if (iterator == entry->entities.end())
        return InvalidHandle(*handle, "Entity handle is invalid");
    MorphState* morphState = iterator->second->TryGetMorphState();
    if (morphState == nullptr)
    {
        SetError(*handle, "Entity has no morph state");
        return WISTERIA_ERROR_NOT_FOUND;
    }
    const std::optional<MorphIndex> morphIndex =
        morphState->GetMorphSet().FindMorph(morph_name);
    if (!morphIndex.has_value())
    {
        SetError(*handle, "Morph not found: " + std::string(morph_name));
        return WISTERIA_ERROR_NOT_FOUND;
    }
    morphState->SetWeight(*morphIndex, weight);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_get_morph_weight(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const char* morph_name,
    float* out_weight
)
{
    if (morph_name == nullptr || morph_name[0] == '\0' ||
        out_weight == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    const auto iterator = entry->entities.find(entity);
    if (iterator == entry->entities.end())
        return InvalidHandle(*handle, "Entity handle is invalid");
    MorphState* morphState = iterator->second->TryGetMorphState();
    if (morphState == nullptr)
    {
        SetError(*handle, "Entity has no morph state");
        return WISTERIA_ERROR_NOT_FOUND;
    }
    const std::optional<MorphIndex> morphIndex =
        morphState->GetMorphSet().FindMorph(morph_name);
    if (!morphIndex.has_value())
    {
        SetError(*handle, "Morph not found: " + std::string(morph_name));
        return WISTERIA_ERROR_NOT_FOUND;
    }
    *out_weight = morphState->Weight(*morphIndex);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_scene_set_environment(
    WisteriaContext context,
    WisteriaScene scene,
    int32_t skybox_enabled,
    float intensity
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    EnvironmentMap* environment = entry->scene->Environment();
    if (environment == nullptr)
    {
        environment = &handle->application->GetResources().CreateEnvironment(
            "scene:" + std::to_string(scene) + ":environment",
            EnvironmentMapData::ProceduralSky()
        );
        entry->scene->SetEnvironment(environment);
    }
    environment->SetDrawSkybox(skybox_enabled != 0);
    if (intensity >= 0.0f)
        environment->SetIntensity(intensity);
    return WISTERIA_OK;
}

namespace
{
DefaultModelData BuildGroundPlaneData(float size)
{
    constexpr std::size_t GroundStride = 15U;  // pos3 color3 uv2 normal3 tangent4
    const float half = size * 0.5f;
    DefaultModelData data;
    data.layout = {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT},
        {"normal", 3, FLOAT},
        {"tangent", 4, FLOAT, false, false, 4U}
    };
    const float positions[4][2] = {
        {-half, -half},
        {half, -half},
        {half, half},
        {-half, half}
    };
    const float uvs[4][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    for (int index = 0; index < 4; ++index)
    {
        const float vertex[GroundStride] = {
            positions[index][0], 0.0f, positions[index][1],
            0.75f, 0.75f, 0.75f,
            uvs[index][0], uvs[index][1],
            0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        for (std::size_t component = 0U; component < GroundStride; ++component)
            data.vertices.push_back(vertex[component]);
    }
    // Front face points +Y (see the ground winding regression).
    data.indices = {0U, 2U, 1U, 0U, 3U, 2U};
    return data;
}
}

enum WisteriaStatus wisteria_scene_add_cube(
    WisteriaContext context,
    WisteriaScene scene,
    float size,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (color == nullptr || position == nullptr || out_entity == nullptr ||
        !std::isfinite(size) || size <= 0.0f)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    try
    {
        ResourceManager& resources = handle->application->GetResources();
        const std::string stem = "scene:" + std::to_string(scene) +
            ":cube:" + std::to_string(entry->nextEntityHandle);
        Mesh& mesh = resources.CreateMesh(stem + ":mesh", cubeData);
        MaterialData materialData;
        materialData.textureSources.clear();
        materialData.baseColorFactor = glm::vec4(
            color[0],
            color[1],
            color[2],
            1.0f
        );
        materialData.castSelfShadow = true;
        materialData.receiveSelfShadow = true;
        materialData.groundShadow = true;
        Material& material = resources.CreateMaterial(
            stem + ":material",
            materialData
        );
        Entity& entity = entry->scene->CreateEntity(
            mesh,
            material,
            Transform(
                glm::vec3(position[0], position[1], position[2]),
                glm::vec3(0.0f),
                glm::vec3(size)
            )
        );
        const WisteriaEntity entityHandle = entry->nextEntityHandle++;
        entry->entities.emplace(entityHandle, &entity);
        *out_entity = entityHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_scene_add_ground_plane(
    WisteriaContext context,
    WisteriaScene scene,
    float size,
    const float position[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (position == nullptr || out_entity == nullptr ||
        !std::isfinite(size) || size <= 0.0f)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    try
    {
        ResourceManager& resources = handle->application->GetResources();
        const std::string stem = "scene:" + std::to_string(scene) +
            ":ground:" + std::to_string(entry->nextEntityHandle);
        Mesh& mesh = resources.CreateMesh(
            stem + ":mesh",
            BuildGroundPlaneData(size)
        );
        MaterialData materialData;
        materialData.textureSources.clear();
        materialData.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        materialData.groundPlane = true;
        materialData.receivesGroundShadow = true;
        Material& material = resources.CreateMaterial(
            stem + ":material",
            materialData
        );
        Entity& entity = entry->scene->CreateEntity(
            mesh,
            material,
            Transform(
                glm::vec3(position[0], position[1], position[2]),
                glm::vec3(0.0f),
                glm::vec3(1.0f)
            )
        );
        const WisteriaEntity entityHandle = entry->nextEntityHandle++;
        entry->entities.emplace(entityHandle, &entity);
        *out_entity = entityHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
}

namespace
{
enum WisteriaStatus AddPrimitiveEntity(
    WisteriaContext context,
    WisteriaScene scene,
    DefaultModelData meshData,
    const char* kind,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    try
    {
        ResourceManager& resources = handle->application->GetResources();
        const std::string stem = "scene:" + std::to_string(scene) +
            ":" + kind + ":" + std::to_string(entry->nextEntityHandle);
        Mesh& mesh = resources.CreateMesh(stem + ":mesh", meshData);
        MaterialData materialData;
        materialData.textureSources.clear();
        materialData.baseColorFactor = glm::vec4(
            color[0],
            color[1],
            color[2],
            1.0f
        );
        materialData.castSelfShadow = true;
        materialData.receiveSelfShadow = true;
        materialData.groundShadow = true;
        Material& material = resources.CreateMaterial(
            stem + ":material",
            materialData
        );
        Entity& entity = entry->scene->CreateEntity(
            mesh,
            material,
            Transform(
                glm::vec3(position[0], position[1], position[2]),
                glm::vec3(0.0f),
                glm::vec3(1.0f)
            )
        );
        const WisteriaEntity entityHandle = entry->nextEntityHandle++;
        entry->entities.emplace(entityHandle, &entity);
        *out_entity = entityHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
}
}

enum WisteriaStatus wisteria_scene_add_sphere(
    WisteriaContext context,
    WisteriaScene scene,
    float radius,
    int32_t stacks,
    int32_t slices,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (color == nullptr || position == nullptr || out_entity == nullptr ||
        !std::isfinite(radius) || radius <= 0.0f ||
        stacks < 2 || slices < 3)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    return AddPrimitiveEntity(
        context,
        scene,
        BuildSphereMeshData(radius, stacks, slices),
        "sphere",
        color,
        position,
        out_entity
    );
}

enum WisteriaStatus wisteria_scene_add_cylinder(
    WisteriaContext context,
    WisteriaScene scene,
    float radius,
    float height,
    int32_t segments,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (color == nullptr || position == nullptr || out_entity == nullptr ||
        !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height) || height <= 0.0f || segments < 3)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    return AddPrimitiveEntity(
        context,
        scene,
        BuildCylinderMeshData(radius, height, segments),
        "cylinder",
        color,
        position,
        out_entity
    );
}

enum WisteriaStatus wisteria_scene_add_capsule(
    WisteriaContext context,
    WisteriaScene scene,
    float radius,
    float height,
    int32_t segments,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (color == nullptr || position == nullptr || out_entity == nullptr ||
        !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height) || height <= 0.0f || segments < 3)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    return AddPrimitiveEntity(
        context,
        scene,
        BuildCapsuleMeshData(radius, height, segments),
        "capsule",
        color,
        position,
        out_entity
    );
}
}
