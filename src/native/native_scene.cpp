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
        entry->windowHandle = window;
        const WisteriaScene sceneHandle = handle->nextSceneHandle++;
        windowEntry->boundScene = sceneHandle;
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
        const WisteriaWindow windowHandle = iterator->second->windowHandle;
        WindowEntry* windowEntry = FindWindow(*handle, windowHandle);
        // Only detach when this scene is still the one bound to the window.
        // Destroying an older scene must not replace a newer bound scene.
        if (windowEntry != nullptr && windowEntry->window != nullptr &&
            windowEntry->boundScene == scene)
        {
            auto replacement = handle->application->CreateScene();
            handle->application->BindScene(*windowEntry->window, replacement);
            windowEntry->boundScene = 0U;
        }
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
    const float euler_degrees[3],
    const float scale[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (position == nullptr || euler_degrees == nullptr ||
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
                euler_degrees[0],
                euler_degrees[1],
                euler_degrees[2]
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
    const float euler_degrees[3],
    const float scale[3]
)
{
    if (position == nullptr || euler_degrees == nullptr ||
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
    return GuardAbi(*handle, [&]
    {
        iterator->second->GetTransform() = Transform(
            glm::vec3(position[0], position[1], position[2]),
            glm::vec3(
                euler_degrees[0],
                euler_degrees[1],
                euler_degrees[2]
            ),
            glm::vec3(scale[0], scale[1], scale[2])
        );
    });
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
    entry->entityMotions.erase(entity);
    entry->entities.erase(iterator);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_runtime_backend(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    char* buffer,
    size_t buffer_size
)
{
    if (buffer == nullptr || buffer_size == 0U)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    Entity* stored = FindEntity(*entry, entity);
    if (stored == nullptr)
        return InvalidHandle(*handle, "Entity handle is invalid");
    ModelInstance* instance = stored->TryGetModelInstance();
    if (instance == nullptr || instance->TryGetRuntime() == nullptr)
    {
        SetError(*handle, "Entity has no model runtime backend");
        return WISTERIA_ERROR_NOT_FOUND;
    }
    CopyErrorMessage(
        std::string(instance->TryGetRuntime()->BackendName()),
        buffer,
        buffer_size
    );
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_load_motion(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const char* motion_path,
    WisteriaMotion* out_motion
)
{
    if (out_motion != nullptr)
        *out_motion = 0U;
    if (motion_path == nullptr || motion_path[0] == '\0' ||
        out_motion == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
    {
        SetError(*handle, "Entity is not driven by an MMD runtime");
        return WISTERIA_ERROR_NOT_FOUND;
    }
    const std::filesystem::path path = PathFromUtf8(motion_path);
    if (path.empty() || !std::filesystem::is_regular_file(path))
    {
        SetError(*handle, "Motion file does not exist");
        return WISTERIA_ERROR_IO;
    }
    return GuardAbi(*handle, [&]
    {
        if (!runtime->LoadMotion(path))
            throw std::runtime_error("MMD backend failed to load motion");
        const WisteriaMotion motion = handle->nextMotionHandle++;
        entry->entityMotions[entity] = motion;
        *out_motion = motion;
    });
}

enum WisteriaStatus wisteria_entity_unload_motion(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    WisteriaMotion motion
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    const auto iterator = entry->entityMotions.find(entity);
    if (iterator == entry->entityMotions.end() || iterator->second != motion)
        return InvalidHandle(*handle, "Entity motion handle is invalid");
    return GuardAbi(*handle, [&]
    {
        runtime->ClearMotion();
        entry->entityMotions.erase(iterator);
    });
}

enum WisteriaStatus wisteria_entity_restart_motion(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t reset_physics
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    return GuardAbi(*handle, [&]
    {
        runtime->RestartMotion(reset_physics != 0);
        runtime->ResumeMotion();
        runtime->Update(0.0f);
    });
}

enum WisteriaStatus wisteria_entity_pause_motion(
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
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    return GuardAbi(*handle, [&] { runtime->PauseMotion(); });
}

enum WisteriaStatus wisteria_entity_resume_motion(
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
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    return GuardAbi(*handle, [&] { runtime->ResumeMotion(); });
}

enum WisteriaStatus wisteria_entity_set_motion_looping(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t looping
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    return GuardAbi(*handle, [&]
    {
        runtime->SetMotionLooping(looping != 0);
    });
}

enum WisteriaStatus wisteria_entity_set_motion_frame(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    double frame
)
{
    if (!std::isfinite(frame) || frame < 0.0)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    return GuardAbi(*handle, [&]
    {
        runtime->SetMotionFrame(frame);
        // Make the requested frame observable immediately through pose and
        // vertex export without requiring an extra frontend-only update call.
        runtime->Update(0.0f);
    });
}

enum WisteriaStatus wisteria_entity_motion_frame(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    double* out_frame
)
{
    if (out_frame == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    *out_frame = runtime->MotionFrame();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_motion_max_frame(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    double* out_max_frame
)
{
    if (out_max_frame == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    *out_max_frame = runtime->MotionMaxFrame();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_set_mmd_ik_enabled(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const char* bone_name,
    int32_t enabled
)
{
    if (bone_name == nullptr || bone_name[0] == '\0')
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    const BoneIndex bone = runtime->FindBoneIndex(bone_name);
    if (bone == InvalidBoneIndex)
    {
        SetError(*handle, "MMD bone was not found");
        return WISTERIA_ERROR_NOT_FOUND;
    }
    return GuardAbi(*handle, [&]
    {
        runtime->SetMmdIkEnabled(bone, enabled != 0);
    });
}

enum WisteriaStatus wisteria_entity_set_physics_settings(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    float fixed_time_step,
    int32_t max_sub_steps,
    float gravity_x,
    float gravity_y,
    float gravity_z,
    int32_t enabled
)
{
    if (!std::isfinite(fixed_time_step) || fixed_time_step <= 0.0f ||
        max_sub_steps <= 0 || !std::isfinite(gravity_x) ||
        !std::isfinite(gravity_y) || !std::isfinite(gravity_z))
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    return GuardAbi(*handle, [&]
    {
        MmdPhysicsRuntimeSettings settings;
        settings.fixedTimeStep = fixed_time_step;
        settings.maxSubSteps = max_sub_steps;
        settings.gravity = glm::vec3(gravity_x, gravity_y, gravity_z);
        settings.enabled = enabled != 0;
        runtime->SetMmdPhysicsSettings(settings);
    });
}

enum WisteriaStatus wisteria_entity_physics_reset(
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
    MmdRuntimeModel* runtime = FindEntityMmdRuntime(*entry, entity);
    if (runtime == nullptr)
        return InvalidHandle(*handle, "Entity MMD runtime is invalid");
    return GuardAbi(*handle, [&] { runtime->ResetMmdPhysics(); });
}

enum WisteriaStatus wisteria_entity_vertex_bounds(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    struct WisteriaVertexBounds* out_bounds
)
{
    if (out_bounds == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    Entity* stored = FindEntity(*entry, entity);
    if (stored == nullptr)
        return InvalidHandle(*handle, "Entity handle is invalid");
    ModelInstance* instance = stored->TryGetModelInstance();
    if (instance == nullptr || instance->TryGetRuntime() == nullptr)
        return InvalidHandle(*handle, "Entity runtime is invalid");
    const ModelVertexFrame frame =
        instance->TryGetRuntime()->VertexFrame();
    if (frame.positions.empty())
    {
        SetError(*handle, "Entity runtime has no vertex frame");
        return WISTERIA_ERROR_NOT_FOUND;
    }
    glm::vec3 minimum = frame.positions.front();
    glm::vec3 maximum = frame.positions.front();
    bool finite = true;
    for (const glm::vec3& position : frame.positions)
    {
        finite = finite && std::isfinite(position.x) &&
            std::isfinite(position.y) && std::isfinite(position.z);
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    out_bounds->finite = finite ? 1 : 0;
    out_bounds->minimum[0] = minimum.x;
    out_bounds->minimum[1] = minimum.y;
    out_bounds->minimum[2] = minimum.z;
    out_bounds->maximum[0] = maximum.x;
    out_bounds->maximum[1] = maximum.y;
    out_bounds->maximum[2] = maximum.z;
    out_bounds->maximumDisplacementFromBind = 0.0f;
    out_bounds->vertexCount =
        static_cast<uint64_t>(frame.positions.size());
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_bone_count(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    uint64_t* out_count
)
{
    if (out_count == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    Entity* stored = FindEntity(*entry, entity);
    if (stored == nullptr || !stored->HasPose())
        return InvalidHandle(*handle, "Entity pose is invalid");
    *out_count = static_cast<uint64_t>(stored->GetPose().BoneCount());
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_bone_name(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    uint64_t bone_index,
    char* buffer,
    size_t buffer_size
)
{
    if (buffer == nullptr || buffer_size == 0U)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    Entity* stored = FindEntity(*entry, entity);
    if (stored == nullptr || !stored->HasPose())
        return InvalidHandle(*handle, "Entity pose is invalid");
    const Pose& pose = stored->GetPose();
    if (bone_index >= pose.BoneCount())
        return InvalidHandle(*handle, "Bone index is invalid");
    CopyErrorMessage(
        pose.GetSkeleton().BoneAt(static_cast<BoneIndex>(bone_index)).name,
        buffer,
        buffer_size
    );
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_entity_bone_local_matrix(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    uint64_t bone_index,
    float out_matrix_16[16]
)
{
    if (out_matrix_16 == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    SceneEntry* entry = FindScene(*handle, scene);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Scene handle is invalid");
    Entity* stored = FindEntity(*entry, entity);
    if (stored == nullptr || !stored->HasPose())
        return InvalidHandle(*handle, "Entity pose is invalid");
    const Pose& pose = stored->GetPose();
    if (bone_index >= pose.BoneCount())
        return InvalidHandle(*handle, "Bone index is invalid");
    const glm::mat4& matrix = pose.LocalMatrix(
        static_cast<BoneIndex>(bone_index)
    );
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
            out_matrix_16[column * 4 + row] = matrix[column][row];
    }
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
    return GuardAbi(*handle, [&]
    {
        DirectionalLight& light = entry->scene->CreateDirectionalLight(
            DirectionalLightData{
                .Direction = glm::vec3(
                    direction[0],
                    direction[1],
                    direction[2]
                ),
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
    });
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
    return GuardAbi(*handle, [&]
    {
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
    });
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
    return GuardAbi(*handle, [&]
    {
        const DirectionalLightData replacement{
            .Direction = glm::vec3(
                direction[0],
                direction[1],
                direction[2]
            ),
            .Color = glm::vec3(color[0], color[1], color[2]),
            .Intensity = intensity
        };
        *stored = DirectionalLight(replacement);
    });
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
    return GuardAbi(*handle, [&]
    {
        PointLightData replacement;
        replacement.Position = glm::vec3(
            position[0],
            position[1],
            position[2]
        );
        replacement.Color = glm::vec3(color[0], color[1], color[2]);
        replacement.Intensity = intensity;
        replacement.Range = range;
        *stored = PointLight(replacement);
    });
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
    return GuardAbi(*handle, [&]
    {
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
    });
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
    return GuardAbi(*handle, [&]
    {
        SpotLightData replacement;
        replacement.Position = glm::vec3(
            position[0],
            position[1],
            position[2]
        );
        replacement.Direction = glm::vec3(
            direction[0],
            direction[1],
            direction[2]
        );
        replacement.Color = glm::vec3(color[0], color[1], color[2]);
        replacement.Intensity = intensity;
        replacement.Range = range;
        replacement.InnerCutoffDegrees = inner_cutoff_degrees;
        replacement.OuterCutoffDegrees = outer_cutoff_degrees;
        *stored = SpotLight(replacement);
    });
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
    if (ModelInstance* instance =
        iterator->second->TryGetModelInstance())
    {
        if (IModelRuntimeDriver* runtime = instance->TryGetRuntime())
        {
            if (runtime->SetMorphWeight(morph_name, weight))
                return WISTERIA_OK;
        }
    }
    MorphState* morphState = iterator->second->TryGetMorphState();
    if (morphState == nullptr)
    {
        SetError(*handle, "Entity runtime has no named morph");
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
    if (const ModelInstance* instance =
        iterator->second->TryGetModelInstance())
    {
        if (const IModelRuntimeDriver* runtime = instance->TryGetRuntime())
        {
            const std::optional<float> value =
                runtime->MorphWeight(morph_name);
            if (value.has_value())
            {
                *out_weight = *value;
                return WISTERIA_OK;
            }
        }
    }
    MorphState* morphState = iterator->second->TryGetMorphState();
    if (morphState == nullptr)
    {
        SetError(*handle, "Entity runtime has no named morph");
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

enum WisteriaStatus wisteria_scene_add_cone(
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
        BuildConeMeshData(radius, height, segments),
        "cone",
        color,
        position,
        out_entity
    );
}

enum WisteriaStatus wisteria_scene_add_torus(
    WisteriaContext context,
    WisteriaScene scene,
    float major_radius,
    float minor_radius,
    int32_t major_segments,
    int32_t minor_segments,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
)
{
    if (out_entity != nullptr)
        *out_entity = 0U;
    if (color == nullptr || position == nullptr || out_entity == nullptr ||
        !std::isfinite(major_radius) || major_radius <= 0.0f ||
        !std::isfinite(minor_radius) || minor_radius <= 0.0f ||
        major_segments < 3 || minor_segments < 3)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    return AddPrimitiveEntity(
        context,
        scene,
        BuildTorusMeshData(
            major_radius,
            minor_radius,
            major_segments,
            minor_segments
        ),
        "torus",
        color,
        position,
        out_entity
    );
}

enum WisteriaStatus wisteria_entity_set_part_color(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t part_index,
    const float color[3]
)
{
    if (color == nullptr || part_index < 0)
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

    const std::span<RenderPart> parts = iterator->second->RenderParts();
    if (static_cast<std::size_t>(part_index) >= parts.size())
    {
        SetError(*handle, "Entity part index is out of range");
        return WISTERIA_ERROR_NOT_FOUND;
    }

    const std::uint8_t red = static_cast<std::uint8_t>(
        glm::clamp(color[0], 0.0f, 1.0f) * 255.0f
    );
    const std::uint8_t green = static_cast<std::uint8_t>(
        glm::clamp(color[1], 0.0f, 1.0f) * 255.0f
    );
    const std::uint8_t blue = static_cast<std::uint8_t>(
        glm::clamp(color[2], 0.0f, 1.0f) * 255.0f
    );
    const std::uint32_t colorKey =
        (static_cast<std::uint32_t>(red) << 16U) |
        (static_cast<std::uint32_t>(green) << 8U) |
        static_cast<std::uint32_t>(blue);
    const auto cachedMaterial = entry->solidMaterials.find(colorKey);
    if (cachedMaterial != entry->solidMaterials.end())
    {
        parts[static_cast<std::size_t>(part_index)].SetMaterial(
            *cachedMaterial->second
        );
        return WISTERIA_OK;
    }

    try
    {
        MaterialData materialData;
        materialData.textureSources.clear();
        materialData.baseColorFactor = glm::vec4(
            glm::clamp(color[0], 0.0f, 1.0f),
            glm::clamp(color[1], 0.0f, 1.0f),
            glm::clamp(color[2], 0.0f, 1.0f),
            1.0f
        );
        Material& material = handle->application->GetResources().CreateMaterial(
            "scene:" + std::to_string(scene) + ":color:" +
                std::to_string(colorKey),
            materialData
        );
        parts[static_cast<std::size_t>(part_index)].SetMaterial(material);
        entry->solidMaterials.emplace(colorKey, &material);
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
}
}
