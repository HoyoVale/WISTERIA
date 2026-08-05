#include "wisteria/native/wisteria_native.h"
#include "internal/native_context.hpp"

#include "wisteria/platform/application.hpp"
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
}
