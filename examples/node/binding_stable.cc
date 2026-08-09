// WISTERIA Stable Runtime/Render C ABI - Node N-API addon (R1.9 Phase 0E).
//
// Non-blocking compatibility smoke: loads wisteria_native at runtime and
// exposes the frozen stable surface to JavaScript. It intentionally speaks
// only the stable headers (wisteria_stable_runtime.h + render.h), never the
// legacy v0.7 surface.

#include <node_api.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{

using WisteriaStableContext = std::uint64_t;
using WisteriaEntity = std::uint64_t;
using WisteriaCheckpoint = std::uint64_t;
using WisteriaRenderSession = std::uint64_t;

constexpr std::uint32_t kWisteriaStatusOk = 0U;
constexpr std::uint32_t kWisteriaStatusNotFound = 2U;
constexpr std::uint32_t kWisteriaStatusUnsupported = 17U;

constexpr std::uint32_t kWisteriaBackendIdGeneric = 2U;

struct WisteriaRuntimeCreationOptionsV1
{
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t compatibility;
    std::uint32_t reserved;
    float fixed_time_step;
    std::int32_t max_sub_steps;
    float gravity[3];
    std::int32_t physics_enabled;
    std::uint32_t reserved2[4];
};

struct WisteriaRuntimeCapabilitiesV1
{
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t capability_flags;
    std::uint32_t runtime_backend_id;
    std::uint32_t runtime_backend_version;
    std::uint32_t deterministic_profile_id;
    std::uint32_t checkpoint_payload_kind;
    std::uint64_t structural_frame_limit;
    std::uint64_t max_deterministic_motion_frame;
    std::uint32_t reserved2[4];
};

struct WisteriaRenderSessionOptionsV1
{
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t force_software;
    std::uint32_t reserved[4];
};

struct WisteriaRenderCameraV1
{
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    float position[3];
    float target[3];
    float up[3];
    float vertical_fov_degrees;
    float near_clip;
    float far_clip;
    std::uint32_t reserved[4];
};

struct Api
{
    std::uint32_t (*contextCreate)(WisteriaStableContext*);
    std::uint32_t (*contextDestroy)(WisteriaStableContext);
    std::uint32_t (*entityCreate)(
        WisteriaStableContext,
        const WisteriaRuntimeCreationOptionsV1*,
        const char*,
        WisteriaEntity*
    );
    std::uint32_t (*entityDestroy)(WisteriaStableContext, WisteriaEntity);
    std::uint32_t (*entityCapabilities)(
        WisteriaStableContext,
        WisteriaEntity,
        WisteriaRuntimeCapabilitiesV1*
    );
    std::uint32_t (*entityPrepareFrameZero)(
        WisteriaStableContext,
        WisteriaEntity
    );
    std::uint32_t (*entityStepExact)(
        WisteriaStableContext,
        WisteriaEntity,
        std::uint64_t
    );
    std::uint32_t (*entityReplayExact)(
        WisteriaStableContext,
        WisteriaEntity,
        std::uint64_t
    );
    std::uint32_t (*checkpointCreate)(
        WisteriaStableContext,
        WisteriaEntity,
        WisteriaCheckpoint*
    );
    std::uint32_t (*checkpointDestroy)(
        WisteriaStableContext,
        WisteriaCheckpoint
    );
    std::uint32_t (*renderSessionCreate)(
        WisteriaStableContext,
        const WisteriaRenderSessionOptionsV1*,
        WisteriaRenderSession*
    );
    std::uint32_t (*renderSessionDestroy)(
        WisteriaStableContext,
        WisteriaRenderSession
    );
    std::uint32_t (*renderSessionRender)(
        WisteriaStableContext,
        WisteriaRenderSession,
        WisteriaEntity,
        const WisteriaRenderCameraV1*,
        std::uint32_t,
        std::uint32_t,
        std::uint8_t*,
        std::uint64_t*
    );
    const char* (*lastError)(WisteriaStableContext);
};

struct StableAddon
{
    Api api;
    bool loaded;
    std::string loadError;
};

bool LoadApi(Api& api, std::string& error)
{
    const char* overrideLibrary = std::getenv("WISTERIA_NATIVE_LIB");
    std::vector<std::string> candidates;
    if (overrideLibrary != nullptr && overrideLibrary[0] != '\0')
        candidates.push_back(overrideLibrary);
#ifdef _WIN32
    if (candidates.empty())
    {
        candidates = {
            "build/RelWithDebInfo/wisteria_native.dll",
            "build/Release/wisteria_native.dll",
            "build/Debug/wisteria_native.dll",
        };
    }
#else
    if (candidates.empty())
        candidates = {"build-linux/libwisteria_native.so"};
#endif

    void* handle = nullptr;
    std::string lastLoadError;
    for (const std::string& candidate : candidates)
    {
#ifdef _WIN32
        handle = static_cast<void*>(LoadLibraryA(candidate.c_str()));
        if (handle == nullptr)
        {
            lastLoadError = "LoadLibrary failed";
            continue;
        }
#else
        handle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
        {
            const char* detail = dlerror();
            lastLoadError = detail != nullptr ? detail : "dlopen failed";
            continue;
        }
#endif
        break;
    }
    if (handle == nullptr)
    {
        error = "cannot load wisteria_native: " + lastLoadError;
        return false;
    }

    const struct
    {
        void** slot;
        const char* symbol;
    } symbols[] = {
        {reinterpret_cast<void**>(&api.contextCreate),
         "wisteria_stable_context_create"},
        {reinterpret_cast<void**>(&api.contextDestroy),
         "wisteria_stable_context_destroy"},
        {reinterpret_cast<void**>(&api.entityCreate),
         "wisteria_stable_entity_create"},
        {reinterpret_cast<void**>(&api.entityDestroy),
         "wisteria_stable_entity_destroy"},
        {reinterpret_cast<void**>(&api.entityCapabilities),
         "wisteria_stable_entity_capabilities"},
        {reinterpret_cast<void**>(&api.entityPrepareFrameZero),
         "wisteria_stable_entity_prepare_frame_zero"},
        {reinterpret_cast<void**>(&api.entityStepExact),
         "wisteria_stable_entity_step_exact"},
        {reinterpret_cast<void**>(&api.entityReplayExact),
         "wisteria_stable_entity_replay_exact"},
        {reinterpret_cast<void**>(&api.checkpointCreate),
         "wisteria_stable_checkpoint_create"},
        {reinterpret_cast<void**>(&api.checkpointDestroy),
         "wisteria_stable_checkpoint_destroy"},
        {reinterpret_cast<void**>(&api.renderSessionCreate),
         "wisteria_stable_render_session_create"},
        {reinterpret_cast<void**>(&api.renderSessionDestroy),
         "wisteria_stable_render_session_destroy"},
        {reinterpret_cast<void**>(&api.renderSessionRender),
         "wisteria_stable_render_session_render"},
        {reinterpret_cast<void**>(&api.lastError),
         "wisteria_stable_last_error"},
    };
    for (const auto& entry : symbols)
    {
#ifdef _WIN32
        *entry.slot = reinterpret_cast<void*>(
            GetProcAddress(static_cast<HMODULE>(handle), entry.symbol)
        );
#else
        *entry.slot = dlsym(handle, entry.symbol);
#endif
    }

    const auto checkLoaded = [&](const void* symbol, const char* name)
    {
        if (symbol == nullptr)
            error = std::string("missing stable symbol: ") + name;
        return symbol != nullptr;
    };
    if (!checkLoaded(api.contextCreate, "wisteria_stable_context_create") ||
        !checkLoaded(api.entityCreate, "wisteria_stable_entity_create") ||
        !checkLoaded(
            api.renderSessionRender,
            "wisteria_stable_render_session_render"
        ) ||
        !checkLoaded(api.lastError, "wisteria_stable_last_error"))
    {
        return false;
    }
    return true;
}

bool RequireAddon(
    napi_env env,
    napi_callback_info info,
    StableAddon*& addon,
    napi_value* this_arg = nullptr
)
{
    napi_value holder;
    if (this_arg != nullptr)
        holder = *this_arg;
    else
    {
        napi_get_cb_info(env, info, nullptr, nullptr, &holder, nullptr);
    }
    napi_value wrapped;
    if (napi_get_named_property(
            env,
            holder,
            "_stable",
            &wrapped
        ) != napi_ok ||
        napi_unwrap(env, wrapped, reinterpret_cast<void**>(&addon)) != napi_ok)
    {
        napi_throw_error(env, nullptr, "internal: addon state missing");
        return false;
    }
    if (!addon->loaded)
    {
        napi_throw_error(env, nullptr, addon->loadError.c_str());
        return false;
    }
    return true;
}

bool GetU64(napi_env env, napi_value value, std::uint64_t* out)
{
    return napi_get_value_int64(
               env,
               value,
               reinterpret_cast<std::int64_t*>(out)
           ) == napi_ok;
}

napi_value MakeStatus(
    napi_env env,
    std::uint32_t status,
    const char* label
)
{
    napi_value object;
    napi_create_object(env, &object);
    napi_value statusValue;
    napi_create_uint32(env, status, &statusValue);
    napi_set_named_property(env, object, "status", statusValue);
    if (status != kWisteriaStatusOk && label != nullptr)
    {
        napi_value labelValue;
        napi_create_string_utf8(env, label, NAPI_AUTO_LENGTH, &labelValue);
        napi_set_named_property(env, object, "error", labelValue);
    }
    return object;
}

napi_value CreateContext(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    if (!RequireAddon(env, info, addon))
        return nullptr;
    WisteriaStableContext context = 0U;
    const std::uint32_t status = addon->api.contextCreate(&context);
    napi_value object = MakeStatus(env, status, "context create");
    if (status == kWisteriaStatusOk)
    {
        napi_value contextValue;
        napi_create_double(
            env,
            static_cast<double>(context),
            &contextValue
        );
        napi_set_named_property(env, object, "context", contextValue);
    }
    return object;
}

napi_value DestroyContext(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 1;
    napi_value args[1];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 1)
    {
        napi_throw_type_error(env, nullptr, "destroyContext(context)");
        return nullptr;
    }
    std::uint64_t context = 0U;
    if (!GetU64(env, args[0], &context))
        return nullptr;
    return MakeStatus(env, addon->api.contextDestroy(context), nullptr);
}

napi_value CreateEntity(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 3;
    napi_value args[3];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 3)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "createEntity(context, modelPath, options)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    char modelPath[4096];
    std::size_t pathLength = 0U;
    if (!GetU64(env, args[0], &context) ||
        napi_get_value_string_utf8(
            env,
            args[1],
            modelPath,
            sizeof(modelPath),
            &pathLength
        ) != napi_ok)
    {
        napi_throw_type_error(env, nullptr, "invalid entity arguments");
        return nullptr;
    }

    WisteriaRuntimeCreationOptionsV1 options;
    std::memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.struct_version = 1U;
    options.compatibility = 1U;  // WISTERIA_PROFILE_ID_RAW
    options.fixed_time_step = 1.0f / 120.0f;
    options.max_sub_steps = 10;
    options.gravity[1] = -98.0f;
    options.physics_enabled = 1;

    WisteriaEntity entity = 0U;
    const std::uint32_t status =
        addon->api.entityCreate(context, &options, modelPath, &entity);
    napi_value object = MakeStatus(env, status, "entity create");
    if (status == kWisteriaStatusOk)
    {
        napi_value entityValue;
        napi_create_double(env, static_cast<double>(entity), &entityValue);
        napi_set_named_property(env, object, "entity", entityValue);
    }
    return object;
}

napi_value DestroyEntity(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 2;
    napi_value args[2];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 2)
    {
        napi_throw_type_error(env, nullptr, "destroyEntity(context, entity)");
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t entity = 0U;
    if (!GetU64(env, args[0], &context) || !GetU64(env, args[1], &entity))
        return nullptr;
    return MakeStatus(env, addon->api.entityDestroy(context, entity), nullptr);
}

napi_value Capabilities(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 2;
    napi_value args[2];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 2)
    {
        napi_throw_type_error(env, nullptr, "capabilities(context, entity)");
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t entity = 0U;
    if (!GetU64(env, args[0], &context) || !GetU64(env, args[1], &entity))
        return nullptr;
    WisteriaRuntimeCapabilitiesV1 capabilities;
    std::memset(&capabilities, 0, sizeof(capabilities));
    capabilities.struct_size = sizeof(capabilities);
    capabilities.struct_version = 1U;
    const std::uint32_t status =
        addon->api.entityCapabilities(context, entity, &capabilities);
    napi_value object = MakeStatus(env, status, "capabilities");
    if (status == kWisteriaStatusOk)
    {
        napi_value backend;
        napi_create_uint32(
            env,
            capabilities.runtime_backend_id,
            &backend
        );
        napi_set_named_property(env, object, "backend_id", backend);
        napi_value flags;
        napi_create_uint32(env, capabilities.capability_flags, &flags);
        napi_set_named_property(env, object, "capability_flags", flags);
        napi_value profile;
        napi_create_uint32(
            env,
            capabilities.deterministic_profile_id,
            &profile
        );
        napi_set_named_property(env, object, "profile_id", profile);
        napi_value payload;
        napi_create_uint32(
            env,
            capabilities.checkpoint_payload_kind,
            &payload
        );
        napi_set_named_property(env, object, "payload_kind", payload);
    }
    return object;
}

napi_value StepExact(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 3;
    napi_value args[3];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 3)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "stepExact(context, entity, frame)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t entity = 0U;
    std::uint64_t frame = 0U;
    if (!GetU64(env, args[0], &context) ||
        !GetU64(env, args[1], &entity) ||
        !GetU64(env, args[2], &frame))
    {
        return nullptr;
    }
    return MakeStatus(
        env,
        addon->api.entityStepExact(context, entity, frame),
        "step_exact"
    );
}

napi_value ReplayExact(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 3;
    napi_value args[3];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 3)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "replayExact(context, entity, target)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t entity = 0U;
    std::uint64_t target = 0U;
    if (!GetU64(env, args[0], &context) ||
        !GetU64(env, args[1], &entity) ||
        !GetU64(env, args[2], &target))
    {
        return nullptr;
    }
    return MakeStatus(
        env,
        addon->api.entityReplayExact(context, entity, target),
        "replay_exact"
    );
}

napi_value PrepareFrameZero(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 2;
    napi_value args[2];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 2)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "prepareFrameZero(context, entity)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t entity = 0U;
    if (!GetU64(env, args[0], &context) || !GetU64(env, args[1], &entity))
        return nullptr;
    return MakeStatus(
        env,
        addon->api.entityPrepareFrameZero(context, entity),
        "prepare_frame_zero"
    );
}

napi_value CheckpointCreate(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 2;
    napi_value args[2];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 2)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "checkpointCreate(context, entity)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t entity = 0U;
    if (!GetU64(env, args[0], &context) || !GetU64(env, args[1], &entity))
        return nullptr;
    WisteriaCheckpoint checkpoint = 0U;
    const std::uint32_t status =
        addon->api.checkpointCreate(context, entity, &checkpoint);
    napi_value object = MakeStatus(env, status, "checkpoint create");
    if (status == kWisteriaStatusOk)
    {
        napi_value checkpointValue;
        napi_create_double(
            env,
            static_cast<double>(checkpoint),
            &checkpointValue
        );
        napi_set_named_property(env, object, "checkpoint", checkpointValue);
    }
    return object;
}

napi_value CheckpointDestroy(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 2;
    napi_value args[2];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 2)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "checkpointDestroy(context, checkpoint)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t checkpoint = 0U;
    if (!GetU64(env, args[0], &context) ||
        !GetU64(env, args[1], &checkpoint))
    {
        return nullptr;
    }
    return MakeStatus(
        env,
        addon->api.checkpointDestroy(context, checkpoint),
        nullptr
    );
}

napi_value RenderSessionCreate(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 1;
    napi_value args[1];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 1)
    {
        napi_throw_type_error(env, nullptr, "renderSessionCreate(context)");
        return nullptr;
    }
    std::uint64_t context = 0U;
    if (!GetU64(env, args[0], &context))
        return nullptr;
    WisteriaRenderSessionOptionsV1 options;
    std::memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.struct_version = 1U;
    WisteriaRenderSession session = 0U;
    const std::uint32_t status =
        addon->api.renderSessionCreate(context, &options, &session);
    napi_value object = MakeStatus(env, status, "render session create");
    if (status == kWisteriaStatusOk)
    {
        napi_value sessionValue;
        napi_create_double(env, static_cast<double>(session), &sessionValue);
        napi_set_named_property(env, object, "session", sessionValue);
    }
    return object;
}

napi_value RenderSessionDestroy(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 2;
    napi_value args[2];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 2)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "renderSessionDestroy(context, session)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t session = 0U;
    if (!GetU64(env, args[0], &context) || !GetU64(env, args[1], &session))
        return nullptr;
    return MakeStatus(
        env,
        addon->api.renderSessionDestroy(context, session),
        nullptr
    );
}

napi_value RenderFrame(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 5;
    napi_value args[5];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 5)
    {
        napi_throw_type_error(
            env,
            nullptr,
            "renderFrame(context, session, entity, width, height)"
        );
        return nullptr;
    }
    std::uint64_t context = 0U;
    std::uint64_t session = 0U;
    std::uint64_t entity = 0U;
    std::int64_t width = 0;
    std::int64_t height = 0;
    if (!GetU64(env, args[0], &context) ||
        !GetU64(env, args[1], &session) ||
        !GetU64(env, args[2], &entity) ||
        napi_get_value_int64(env, args[3], &width) != napi_ok ||
        napi_get_value_int64(env, args[4], &height) != napi_ok ||
        width <= 0 ||
        height <= 0 ||
        width > 4096 ||
        height > 4096)
    {
        napi_throw_type_error(env, nullptr, "invalid render dimensions");
        return nullptr;
    }

    WisteriaRenderCameraV1 camera;
    std::memset(&camera, 0, sizeof(camera));
    camera.struct_size = sizeof(camera);
    camera.struct_version = 1U;
    camera.position[1] = 3.0f;
    camera.position[2] = 3.0f;
    camera.up[1] = 1.0f;
    camera.vertical_fov_degrees = 45.0f;
    camera.near_clip = 0.1f;
    camera.far_clip = 100.0f;

    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) * 4U;
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(byteCount),
        0U
    );
    std::uint64_t inOutSize = byteCount;
    const std::uint32_t status = addon->api.renderSessionRender(
        context,
        session,
        entity,
        &camera,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        pixels.data(),
        &inOutSize
    );
    napi_value object = MakeStatus(env, status, "render");
    if (status == kWisteriaStatusOk)
    {
        std::uint64_t nonZero = 0U;
        for (const std::uint8_t value : pixels)
        {
            if (value != 0U)
                ++nonZero;
        }
        napi_value nonZeroValue;
        napi_create_double(env, static_cast<double>(nonZero), &nonZeroValue);
        napi_set_named_property(env, object, "non_zero_pixels", nonZeroValue);
        napi_value bytesValue;
        napi_create_double(env, static_cast<double>(byteCount), &bytesValue);
        napi_set_named_property(env, object, "byte_count", bytesValue);
    }
    return object;
}

napi_value LastError(napi_env env, napi_callback_info info)
{
    StableAddon* addon = nullptr;
    size_t argc = 1;
    napi_value args[1];
    if (!RequireAddon(env, info, addon) ||
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
        argc < 1)
    {
        napi_throw_type_error(env, nullptr, "lastError(context)");
        return nullptr;
    }
    std::uint64_t context = 0U;
    if (!GetU64(env, args[0], &context))
        return nullptr;
    const char* message = addon->api.lastError(context);
    napi_value result;
    napi_create_string_utf8(
        env,
        message != nullptr ? message : "",
        NAPI_AUTO_LENGTH,
        &result
    );
    return result;
}

napi_value Init(napi_env env, napi_value exports)
{
    auto* addon = new StableAddon();
    addon->loaded = LoadApi(addon->api, addon->loadError);

    napi_value holder;
    napi_create_object(env, &holder);
    napi_wrap(env, holder, addon, nullptr, nullptr, nullptr);
    napi_set_named_property(env, exports, "_stable", holder);

    const struct
    {
        const char* name;
        napi_callback callback;
    } functions[] = {
        {"createContext", CreateContext},
        {"destroyContext", DestroyContext},
        {"createEntity", CreateEntity},
        {"destroyEntity", DestroyEntity},
        {"capabilities", Capabilities},
        {"stepExact", StepExact},
        {"replayExact", ReplayExact},
        {"prepareFrameZero", PrepareFrameZero},
        {"checkpointCreate", CheckpointCreate},
        {"checkpointDestroy", CheckpointDestroy},
        {"renderSessionCreate", RenderSessionCreate},
        {"renderSessionDestroy", RenderSessionDestroy},
        {"renderFrame", RenderFrame},
        {"lastError", LastError},
    };
    for (const auto& function : functions)
    {
        napi_value wrapped;
        napi_create_function(
            env,
            function.name,
            NAPI_AUTO_LENGTH,
            function.callback,
            nullptr,
            &wrapped
        );
        napi_set_named_property(env, exports, function.name, wrapped);
    }
    return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
