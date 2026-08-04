#include "wisteria/native/wisteria_native.h"

#include "wisteria/runtime/saba_mmd_runtime_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
struct ModelEntry
{
    std::unique_ptr<SabaMmdRuntimeModel> runtime;
    WisteriaMotion currentMotion = 0U;
    bool hasMotion = false;
};

struct Context
{
    std::unordered_map<WisteriaModel, std::unique_ptr<ModelEntry>> models;
    WisteriaModel nextModelHandle = 1U;
    WisteriaMotion nextMotionHandle = 1U;
    std::string lastError;
};

std::unordered_map<WisteriaContext, std::unique_ptr<Context>> gContexts;
std::mutex gContextMutex;
WisteriaContext gNextContextHandle = 1U;

Context* FindContext(WisteriaContext handle)
{
    std::lock_guard<std::mutex> lock(gContextMutex);
    const auto iterator = gContexts.find(handle);
    return iterator == gContexts.end() ? nullptr : iterator->second.get();
}

ModelEntry* FindModel(Context& context, WisteriaModel handle)
{
    const auto iterator = context.models.find(handle);
    return iterator == context.models.end() ? nullptr : iterator->second.get();
}

void SetError(Context& context, std::string message)
{
    context.lastError = std::move(message);
}

enum WisteriaStatus InvalidHandle(
    Context& context,
    const char* message
)
{
    SetError(context, message);
    return WISTERIA_ERROR_NOT_FOUND;
}

bool CopyErrorMessage(
    const std::string& message,
    char* buffer,
    size_t bufferSize
)
{
    if (buffer == nullptr || bufferSize == 0U)
        return false;
    const size_t copyLength = std::min(message.size(), bufferSize - 1U);
    std::memcpy(buffer, message.data(), copyLength);
    buffer[copyLength] = '\0';
    return true;
}
}

extern "C"
{

const char* wisteria_status_name(enum WisteriaStatus status)
{
    switch (status)
    {
    case WISTERIA_OK: return "OK";
    case WISTERIA_ERROR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case WISTERIA_ERROR_NOT_FOUND: return "NOT_FOUND";
    case WISTERIA_ERROR_IO: return "IO";
    case WISTERIA_ERROR_PARSE: return "PARSE";
    case WISTERIA_ERROR_INITIALIZATION: return "INITIALIZATION";
    case WISTERIA_ERROR_ALREADY_EXISTS: return "ALREADY_EXISTS";
    case WISTERIA_ERROR_INTERNAL: return "INTERNAL";
    }
    return "UNKNOWN";
}

uint32_t wisteria_version_major(void)
{
    return WISTERIA_NATIVE_VERSION_MAJOR;
}

uint32_t wisteria_version_minor(void)
{
    return WISTERIA_NATIVE_VERSION_MINOR;
}

enum WisteriaStatus wisteria_create_context(WisteriaContext* out_context)
{
    if (out_context == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;

    auto context = std::make_unique<Context>();
    WisteriaContext handle = 0U;
    {
        std::lock_guard<std::mutex> lock(gContextMutex);
        handle = gNextContextHandle++;
        gContexts.emplace(handle, std::move(context));
    }
    *out_context = handle;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_destroy_context(WisteriaContext context)
{
    std::lock_guard<std::mutex> lock(gContextMutex);
    const auto iterator = gContexts.find(context);
    if (iterator == gContexts.end())
        return WISTERIA_ERROR_NOT_FOUND;
    gContexts.erase(iterator);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_last_error_message(
    WisteriaContext context,
    char* buffer,
    size_t buffer_size
)
{
    if (buffer == nullptr || buffer_size == 0U)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
    {
        buffer[0] = '\0';
        return WISTERIA_ERROR_NOT_FOUND;
    }
    CopyErrorMessage(handle->lastError, buffer, buffer_size);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_load_model(
    WisteriaContext context,
    const char* model_path,
    WisteriaModel* out_model
)
{
    if (model_path == nullptr || model_path[0] == '\0' ||
        out_model == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;

    const std::filesystem::path path(model_path);
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
}

enum WisteriaStatus wisteria_unload_model(
    WisteriaContext context,
    WisteriaModel model
)
{
    Context* handle = FindContext(context);
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
    if (vmd_path == nullptr || vmd_path[0] == '\0' ||
        out_motion == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    ModelEntry* entry = FindModel(*handle, model);
    if (entry == nullptr)
        return InvalidHandle(*handle, "Model handle is invalid");

    const std::filesystem::path path(vmd_path);
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
}

enum WisteriaStatus wisteria_unload_motion(
    WisteriaContext context,
    WisteriaModel model,
    WisteriaMotion motion
)
{
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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
    Context* handle = FindContext(context);
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

} /* extern "C" */
