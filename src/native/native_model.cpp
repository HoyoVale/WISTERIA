#include "wisteria/native/wisteria_native.h"
#include "internal/native_context.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"

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
    return InvokeAbi(context, [&](Context& ctx)
    {

    const std::filesystem::path path = PathFromUtf8(model_path);
    if (path.empty())
    {
        TrySetError(&ctx, "Model path is not valid UTF-8");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    if (!std::filesystem::is_regular_file(path))
    {
        TrySetError(&ctx, "Model file does not exist: " +
            std::string(model_path));
        return WISTERIA_ERROR_IO;
    }

    try
    {
        std::unique_ptr<MmdRuntimeModel> runtime =
            std::make_unique<SabaMmdRuntimeModel>(path);
        if (!runtime->Initialize())
        {
            TrySetError(&ctx, "Saba runtime failed to initialize: " +
                std::string(model_path));
            return WISTERIA_ERROR_INITIALIZATION;
        }
        auto entry = std::make_unique<ModelEntry>();
        entry->runtime = std::move(runtime);
        const WisteriaModel modelHandle = AllocateOpaqueHandle();
        ctx.models.emplace(modelHandle, std::move(entry));
        *out_model = modelHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        TrySetError(&ctx, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        TrySetError(&ctx, "Unknown C++ exception while loading the model");
        return WISTERIA_ERROR_INTERNAL;
    }
    });
}

enum WisteriaStatus wisteria_unload_model(
    WisteriaContext context,
    WisteriaModel model
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    const auto iterator = ctx.models.find(model);
    if (iterator == ctx.models.end())
        return InvalidHandle(ctx, "Model handle is invalid");
    ctx.models.erase(iterator);
    return WISTERIA_OK;
    });
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
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");

    const std::filesystem::path path = PathFromUtf8(vmd_path);
    if (path.empty())
    {
        TrySetError(&ctx, "Motion path is not valid UTF-8");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    if (!std::filesystem::is_regular_file(path))
    {
        TrySetError(&ctx, "Motion file does not exist: " +
            std::string(vmd_path));
        return WISTERIA_ERROR_IO;
    }

    try
    {
        if (!entry->runtime->LoadMotion(path))
        {
            TrySetError(&ctx, "Failed to load motion: " +
                std::string(vmd_path));
            return WISTERIA_ERROR_PARSE;
        }
        const WisteriaMotion motionHandle = AllocateOpaqueHandle();
        entry->currentMotion = motionHandle;
        entry->hasMotion = true;
        *out_motion = motionHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        TrySetError(&ctx, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        TrySetError(&ctx, "Unknown C++ exception while loading the motion");
        return WISTERIA_ERROR_INTERNAL;
    }
    });
}

enum WisteriaStatus wisteria_unload_motion(
    WisteriaContext context,
    WisteriaModel model,
    WisteriaMotion motion
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    if (!entry->hasMotion || entry->currentMotion != motion)
        return InvalidHandle(ctx, "Motion handle is invalid");
    entry->runtime->ClearMotion();
    entry->hasMotion = false;
    entry->currentMotion = 0U;
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_play_motion(
    WisteriaContext context,
    WisteriaModel model,
    WisteriaMotion motion
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    if (!entry->hasMotion || entry->currentMotion != motion)
        return InvalidHandle(ctx, "Motion handle is invalid");
    entry->runtime->RestartMotion(true);
    entry->runtime->ResumeMotion();
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_pause_motion(
    WisteriaContext context,
    WisteriaModel model
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    entry->runtime->PauseMotion();
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_resume_motion(
    WisteriaContext context,
    WisteriaModel model
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    entry->runtime->ResumeMotion();
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_set_motion_looping(
    WisteriaContext context,
    WisteriaModel model,
    int32_t looping
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    entry->runtime->SetMotionLooping(looping != 0);
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_set_motion_frame(
    WisteriaContext context,
    WisteriaModel model,
    double frame
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    if (!std::isfinite(frame) || frame < 0.0)
    {
        TrySetError(&ctx, "Motion frame must be finite and non-negative");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    entry->runtime->SetMotionFrame(frame);
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_motion_frame(
    WisteriaContext context,
    WisteriaModel model,
    double* out_frame
)
{
    if (out_frame == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    *out_frame = entry->runtime->MotionFrame();
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_motion_max_frame(
    WisteriaContext context,
    WisteriaModel model,
    double* out_max_frame
)
{
    if (out_max_frame == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    *out_max_frame = entry->runtime->MotionMaxFrame();
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_update(
    WisteriaContext context,
    WisteriaModel model,
    float delta_time
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    if (!std::isfinite(delta_time) || delta_time < 0.0f)
    {
        TrySetError(&ctx, "Delta time must be finite and non-negative");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    try
    {
        entry->runtime->Update(delta_time);
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        TrySetError(&ctx, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        TrySetError(&ctx, "Unknown C++ exception during update");
        return WISTERIA_ERROR_INTERNAL;
    }
    });
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
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    if (!std::isfinite(fixed_time_step) || fixed_time_step <= 0.0f ||
        max_sub_steps <= 0 ||
        !std::isfinite(gravity_x) || !std::isfinite(gravity_y) ||
        !std::isfinite(gravity_z))
    {
        TrySetError(&ctx, "Physics settings contain invalid values");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    MmdPhysicsRuntimeSettings settings;
    settings.fixedTimeStep = fixed_time_step;
    settings.maxSubSteps = max_sub_steps;
    settings.gravity = glm::vec3(gravity_x, gravity_y, gravity_z);
    entry->runtime->SetMmdPhysicsSettings(settings);
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_vertex_bounds(
    WisteriaContext context,
    WisteriaModel model,
    struct WisteriaVertexBounds* out_bounds
)
{
    if (out_bounds == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");

    const ModelVertexFrame frame = entry->runtime->VertexFrame();
    if (frame.positions.empty())
    {
        TrySetError(&ctx, "Model runtime has no vertex frame");
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
    out_bounds->vertexCount = static_cast<uint64_t>(frame.positions.size());
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_set_mmd_ik_enabled(
    WisteriaContext context,
    WisteriaModel model,
    uint32_t bone_index,
    int32_t enabled
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    entry->runtime->SetMmdIkEnabled(
        static_cast<wisteria::BoneIndex>(bone_index),
        enabled != 0
    );
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_find_bone_index(
    WisteriaContext context,
    WisteriaModel model,
    const char* bone_name,
    uint32_t* out_bone_index
)
{
    if (bone_name == nullptr || bone_name[0] == '\0' ||
        out_bone_index == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    const wisteria::BoneIndex found =
        entry->runtime->FindBoneIndex(bone_name);
    if (found == wisteria::InvalidBoneIndex)
    {
        TrySetError(&ctx, "Bone not found: " + std::string(bone_name));
        return WISTERIA_ERROR_NOT_FOUND;
    }
    *out_bone_index = static_cast<uint32_t>(found);
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_load_camera_motion(
    WisteriaContext context,
    WisteriaModel model,
    const char* vmd_path
)
{
    if (vmd_path == nullptr || vmd_path[0] == '\0')
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");

    const std::filesystem::path path = PathFromUtf8(vmd_path);
    if (path.empty())
    {
        TrySetError(&ctx, "Camera motion path is not valid UTF-8");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    if (!std::filesystem::is_regular_file(path))
    {
        TrySetError(&ctx, "Camera motion file does not exist: " +
            std::string(vmd_path));
        return WISTERIA_ERROR_IO;
    }
    if (!entry->runtime->LoadCameraMotion(path))
    {
        TrySetError(&ctx, "Failed to load camera motion: " +
            std::string(vmd_path));
        return WISTERIA_ERROR_PARSE;
    }
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_physics_capabilities(
    WisteriaContext context,
    WisteriaModel model,
    uint32_t* out_capabilities
)
{
    if (out_capabilities == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    // saba's real physics surface: fixed step, gravity and the activation
    // switch. Semantic collision filtering remains saba-internal.
    *out_capabilities =
        WISTERIA_PHYSICS_CAP_FIXED_STEP |
        WISTERIA_PHYSICS_CAP_GRAVITY |
        WISTERIA_PHYSICS_CAP_ENABLED;
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_set_physics_preset(
    WisteriaContext context,
    WisteriaModel model,
    const struct WisteriaPhysicsPreset* preset
)
{
    if (preset == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    const bool finiteGravity =
        std::isfinite(preset->gravity[0]) &&
        std::isfinite(preset->gravity[1]) &&
        std::isfinite(preset->gravity[2]);
    if (!std::isfinite(preset->fixed_time_step) ||
        preset->fixed_time_step <= 0.0f ||
        preset->max_sub_steps <= 0 ||
        !finiteGravity ||
        (preset->physics_enabled != 0 && preset->physics_enabled != 1))
    {
        TrySetError(&ctx, "Physics preset contains invalid values");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    MmdPhysicsRuntimeSettings settings;
    settings.fixedTimeStep = preset->fixed_time_step;
    settings.maxSubSteps = preset->max_sub_steps;
    settings.gravity = glm::vec3(
        preset->gravity[0],
        preset->gravity[1],
        preset->gravity[2]
    );
    settings.enabled = preset->physics_enabled != 0;
    entry->runtime->SetMmdPhysicsSettings(settings);
    return WISTERIA_OK;
    });
}

enum WisteriaStatus wisteria_physics_reset(
    WisteriaContext context,
    WisteriaModel model
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
    ModelEntry* entry = FindModel(ctx, model);
    if (entry == nullptr)
        return InvalidHandle(ctx, "Model handle is invalid");
    entry->runtime->ResetMmdPhysics();
    return WISTERIA_OK;
    });
}

/* --- Window (M4) --------------------------------------------------------- */

} /* extern "C" */
