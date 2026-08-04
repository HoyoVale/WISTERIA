#include "wisteria/native/wisteria_native.h"
#include "internal/native_context.hpp"

#include <cmath>
#include <filesystem>

using namespace wisteria::native;
using namespace wisteria;

extern "C"
{
enum WisteriaStatus wisteria_load_model(
    WisteriaContext context,
    const char* model_path,
    WisteriaModel* out_model
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
        auto runtime = std::make_unique<SabaMmdRuntimeModel>(path);
        if (!runtime->Initialize())
        {
            SetError(*handle, "Saba runtime failed to initialize: " +
                std::string(model_path));
            return WISTERIA_ERROR_INITIALIZATION;
        }
        auto entry = std::make_unique<ModelEntry>();
        entry->runtime = std::move(runtime);
        const WisteriaModel modelHandle = handle->nextModelHandle++;
        handle->models.emplace(modelHandle, std::move(entry));
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

enum WisteriaStatus wisteria_unload_model(
    WisteriaContext context,
    WisteriaModel model
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    const auto iterator = handle->models.find(model);
    if (iterator == handle->models.end())
        return InvalidHandle(*handle, "Model handle is invalid");
    handle->models.erase(iterator);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_load_motion(
    WisteriaContext context,
    WisteriaModel model,
    const char* vmd_path,
    WisteriaMotion* out_motion
)
{
    if (out_motion != nullptr)
        *out_motion = 0U;
    if (vmd_path == nullptr || vmd_path[0] == '\0' ||
        out_motion == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");

    const std::filesystem::path path = PathFromUtf8(vmd_path);
    if (path.empty())
    {
        SetError(*handle, "Motion path is not valid UTF-8");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    if (!std::filesystem::is_regular_file(path))
    {
        SetError(*handle, "Motion file does not exist: " +
            std::string(vmd_path));
        return WISTERIA_ERROR_IO;
    }

    try
    {
        if (!entry->runtime->LoadMotion(path))
        {
            SetError(*handle, "Failed to load motion: " +
                std::string(vmd_path));
            return WISTERIA_ERROR_PARSE;
        }
        const WisteriaMotion motionHandle = handle->nextMotionHandle++;
        entry->currentMotion = motionHandle;
        entry->hasMotion = true;
        *out_motion = motionHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        SetError(*handle, "Unknown C++ exception while loading the motion");
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_unload_motion(
    WisteriaContext context,
    WisteriaModel model,
    WisteriaMotion motion
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    if (!entry->hasMotion || entry->currentMotion != motion)
        return InvalidHandle(*handle, "Motion handle is invalid");
    entry->runtime->ClearMotion();
    entry->hasMotion = false;
    entry->currentMotion = 0U;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_play_motion(
    WisteriaContext context,
    WisteriaModel model,
    WisteriaMotion motion
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    if (!entry->hasMotion || entry->currentMotion != motion)
        return InvalidHandle(*handle, "Motion handle is invalid");
    entry->runtime->RestartMotion(true);
    entry->runtime->ResumeMotion();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_pause_motion(
    WisteriaContext context,
    WisteriaModel model
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    entry->runtime->PauseMotion();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_resume_motion(
    WisteriaContext context,
    WisteriaModel model
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    entry->runtime->ResumeMotion();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_set_motion_looping(
    WisteriaContext context,
    WisteriaModel model,
    int32_t looping
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    entry->runtime->SetMotionLooping(looping != 0);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_set_motion_frame(
    WisteriaContext context,
    WisteriaModel model,
    double frame
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    if (!std::isfinite(frame) || frame < 0.0)
    {
        SetError(*handle, "Motion frame must be finite and non-negative");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    entry->runtime->SetMotionFrame(frame);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_motion_frame(
    WisteriaContext context,
    WisteriaModel model,
    double* out_frame
)
{
    if (out_frame == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    *out_frame = entry->runtime->MotionFrame();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_motion_max_frame(
    WisteriaContext context,
    WisteriaModel model,
    double* out_max_frame
)
{
    if (out_max_frame == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    *out_max_frame = entry->runtime->MotionMaxFrame();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_update(
    WisteriaContext context,
    WisteriaModel model,
    float delta_time
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    if (!std::isfinite(delta_time) || delta_time < 0.0f)
    {
        SetError(*handle, "Delta time must be finite and non-negative");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    try
    {
        entry->runtime->Update(delta_time);
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        SetError(*handle, "Unknown C++ exception during update");
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_set_physics_settings(
    WisteriaContext context,
    WisteriaModel model,
    float fixed_time_step,
    int32_t max_sub_steps,
    float gravity_x,
    float gravity_y,
    float gravity_z
)
{
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");
    if (!std::isfinite(fixed_time_step) || fixed_time_step <= 0.0f ||
        max_sub_steps <= 0 ||
        !std::isfinite(gravity_x) || !std::isfinite(gravity_y) ||
        !std::isfinite(gravity_z))
    {
        SetError(*handle, "Physics settings contain invalid values");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    SabaPhysicsSettings settings;
    settings.fixedTimeStep = fixed_time_step;
    settings.maxSubSteps = max_sub_steps;
    settings.gravity = glm::vec3(gravity_x, gravity_y, gravity_z);
    entry->runtime->SetPhysicsSettings(settings);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_vertex_bounds(
    WisteriaContext context,
    WisteriaModel model,
    struct WisteriaVertexBounds* out_bounds
)
{
    if (out_bounds == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");

    const SabaMmdRuntimeModel::VertexDiagnostics diagnostics =
        entry->runtime->DiagnoseVertices();
    out_bounds->finite = diagnostics.finite ? 1 : 0;
    out_bounds->minimum[0] = diagnostics.minimumPosition.x;
    out_bounds->minimum[1] = diagnostics.minimumPosition.y;
    out_bounds->minimum[2] = diagnostics.minimumPosition.z;
    out_bounds->maximum[0] = diagnostics.maximumPosition.x;
    out_bounds->maximum[1] = diagnostics.maximumPosition.y;
    out_bounds->maximum[2] = diagnostics.maximumPosition.z;
    out_bounds->maximumDisplacementFromBind =
        diagnostics.maximumDisplacementFromBind;
    out_bounds->vertexCount = diagnostics.vertexCount;
    return WISTERIA_OK;
}

/* --- Window (M4) --------------------------------------------------------- */

} /* extern "C" */
