// WISTERIA native C ABI demo - Node N-API addon.
//
// Loads wisteria_native (DLL/SO) at runtime, runs the same headless demo as
// the Python example: load model -> load VMD motion -> Saba physics ->
// step N frames -> vertex bounds diagnostics -> pause/resume control.
//
// The library is located via WISTERIA_NATIVE_LIB or the default build paths
// relative to the current working directory (project root).

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

using WisteriaContext = std::uint64_t;
using WisteriaModel = std::uint64_t;
using WisteriaMotion = std::uint64_t;
using WisteriaWindow = std::uint64_t;

constexpr int kWisteriaOk = 0;

struct WisteriaVertexBounds
{
    std::int32_t finite;
    float minimum[3];
    float maximum[3];
    float maximumDisplacementFromBind;
    std::uint64_t vertexCount;
};

struct Api
{
    int (*createContext)(WisteriaContext*);
    int (*destroyContext)(WisteriaContext);
    int (*lastErrorMessage)(WisteriaContext, char*, size_t);
    const char* (*statusName)(int);
    int (*loadModel)(WisteriaContext, const char*, WisteriaModel*);
    int (*unloadModel)(WisteriaContext, WisteriaModel);
    int (*loadMotion)(
        WisteriaContext,
        WisteriaModel,
        const char*,
        WisteriaMotion*
    );
    int (*unloadMotion)(WisteriaContext, WisteriaModel, WisteriaMotion);
    int (*playMotion)(WisteriaContext, WisteriaModel, WisteriaMotion);
    int (*pauseMotion)(WisteriaContext, WisteriaModel);
    int (*resumeMotion)(WisteriaContext, WisteriaModel);
    int (*setMotionLooping)(WisteriaContext, WisteriaModel, std::int32_t);
    int (*motionFrame)(WisteriaContext, WisteriaModel, double*);
    int (*motionMaxFrame)(WisteriaContext, WisteriaModel, double*);
    int (*update)(WisteriaContext, WisteriaModel, float);
    int (*setPhysicsSettings)(
        WisteriaContext,
        WisteriaModel,
        float,
        std::int32_t,
        float,
        float,
        float
    );
    int (*vertexBounds)(WisteriaContext, WisteriaModel, WisteriaVertexBounds*);
    int (*windowCreate)(
        WisteriaContext,
        int,
        int,
        const char*,
        WisteriaWindow*
    );
    int (*windowDestroy)(WisteriaContext, WisteriaWindow);
    int (*windowLoadDemo)(
        WisteriaContext,
        WisteriaWindow,
        const char*,
        const char*,
        const char*,
        float,
        std::int32_t
    );
    int (*windowPollAndRender)(WisteriaContext, WisteriaWindow, float);
    int (*windowShouldClose)(WisteriaContext, WisteriaWindow, std::int32_t*);
    int (*windowCameraPose)(
        WisteriaContext,
        WisteriaWindow,
        float*,
        float*,
        float*
    );
    int (*windowIsKeyDown)(WisteriaContext, WisteriaWindow, int, std::int32_t*);
    int (*windowCursorDelta)(
        WisteriaContext,
        WisteriaWindow,
        float*,
        float*
    );
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
            lastLoadError = "LoadLibraryA failed with code " +
                std::to_string(static_cast<long>(GetLastError()));
        }
#else
        handle = dlopen(candidate.c_str(), RTLD_NOW);
        if (handle == nullptr)
        {
            const char* detail = dlerror();
            lastLoadError = detail != nullptr ? detail : "dlopen failed";
        }
#endif
        if (handle != nullptr)
            break;
    }
    if (handle == nullptr)
    {
        error = "cannot load wisteria_native (" + lastLoadError +
            "). Build it first or set WISTERIA_NATIVE_LIB.";
        return false;
    }

#ifdef _WIN32
#define WISTERIA_LOAD_SYMBOL(member, name)                                  \
    api.member = reinterpret_cast<decltype(api.member)>(                    \
        GetProcAddress(static_cast<HMODULE>(handle), name)                  \
    );                                                                      \
    if (api.member == nullptr)                                              \
    {                                                                       \
        error = std::string("missing export: ") + name;                     \
        return false;                                                       \
    }
#else
#define WISTERIA_LOAD_SYMBOL(member, name)                                  \
    api.member = reinterpret_cast<decltype(api.member)>(dlsym(handle, name)); \
    if (api.member == nullptr)                                              \
    {                                                                       \
        error = std::string("missing export: ") + name;                     \
        return false;                                                       \
    }
#endif

    WISTERIA_LOAD_SYMBOL(createContext, "wisteria_create_context")
    WISTERIA_LOAD_SYMBOL(destroyContext, "wisteria_destroy_context")
    WISTERIA_LOAD_SYMBOL(lastErrorMessage, "wisteria_last_error_message")
    WISTERIA_LOAD_SYMBOL(statusName, "wisteria_status_name")
    WISTERIA_LOAD_SYMBOL(loadModel, "wisteria_load_model")
    WISTERIA_LOAD_SYMBOL(unloadModel, "wisteria_unload_model")
    WISTERIA_LOAD_SYMBOL(loadMotion, "wisteria_load_motion")
    WISTERIA_LOAD_SYMBOL(unloadMotion, "wisteria_unload_motion")
    WISTERIA_LOAD_SYMBOL(playMotion, "wisteria_play_motion")
    WISTERIA_LOAD_SYMBOL(pauseMotion, "wisteria_pause_motion")
    WISTERIA_LOAD_SYMBOL(resumeMotion, "wisteria_resume_motion")
    WISTERIA_LOAD_SYMBOL(setMotionLooping, "wisteria_set_motion_looping")
    WISTERIA_LOAD_SYMBOL(motionFrame, "wisteria_motion_frame")
    WISTERIA_LOAD_SYMBOL(motionMaxFrame, "wisteria_motion_max_frame")
    WISTERIA_LOAD_SYMBOL(update, "wisteria_update")
    WISTERIA_LOAD_SYMBOL(setPhysicsSettings, "wisteria_set_physics_settings")
    WISTERIA_LOAD_SYMBOL(vertexBounds, "wisteria_vertex_bounds")
    WISTERIA_LOAD_SYMBOL(windowCreate, "wisteria_window_create")
    WISTERIA_LOAD_SYMBOL(windowDestroy, "wisteria_window_destroy")
    WISTERIA_LOAD_SYMBOL(windowLoadDemo, "wisteria_window_load_demo")
    WISTERIA_LOAD_SYMBOL(windowPollAndRender, "wisteria_window_poll_and_render")
    WISTERIA_LOAD_SYMBOL(windowShouldClose, "wisteria_window_should_close")
    WISTERIA_LOAD_SYMBOL(windowCameraPose, "wisteria_window_camera_pose")
    WISTERIA_LOAD_SYMBOL(windowIsKeyDown, "wisteria_window_is_key_down")
    WISTERIA_LOAD_SYMBOL(windowCursorDelta, "wisteria_window_cursor_delta")

#undef WISTERIA_LOAD_SYMBOL
    return true;
}

std::string GetStringProperty(
    napi_env env,
    napi_value object,
    const char* name,
    const std::string& fallback
)
{
    napi_value value = nullptr;
    if (napi_get_named_property(env, object, name, &value) != napi_ok ||
        value == nullptr)
    {
        return fallback;
    }
    napi_valuetype type = napi_undefined;
    napi_typeof(env, value, &type);
    if (type != napi_string)
        return fallback;
    std::size_t length = 0U;
    if (napi_get_value_string_utf8(env, value, nullptr, 0U, &length) !=
        napi_ok)
    {
        return fallback;
    }
    std::string result(length, '\0');
    napi_get_value_string_utf8(env, value, result.data(), length + 1U, &length);
    return result;
}

double GetNumberProperty(
    napi_env env,
    napi_value object,
    const char* name,
    double fallback
)
{
    napi_value value = nullptr;
    if (napi_get_named_property(env, object, name, &value) != napi_ok ||
        value == nullptr)
    {
        return fallback;
    }
    double number = 0.0;
    if (napi_get_value_double(env, value, &number) != napi_ok)
        return fallback;
    return number;
}

napi_value MakeString(napi_env env, const std::string& text)
{
    napi_value value = nullptr;
    napi_create_string_utf8(env, text.c_str(), text.size(), &value);
    return value;
}

napi_value MakeErrorResult(
    napi_env env,
    const std::string& message
)
{
    napi_value result = nullptr;
    napi_create_object(env, &result);
    napi_value ok = nullptr;
    napi_get_boolean(env, false, &ok);
    napi_set_named_property(env, result, "ok", ok);
    napi_set_named_property(env, result, "error", MakeString(env, message));
    return result;
}

napi_value RunDemo(napi_env env, napi_callback_info info)
{
    std::size_t argumentCount = 1U;
    napi_value arguments[1] = {nullptr};
    if (napi_get_cb_info(
            env,
            info,
            &argumentCount,
            arguments,
            nullptr,
            nullptr
        ) != napi_ok ||
        argumentCount < 1U)
    {
        return MakeErrorResult(env, "runDemo(options) expects one object");
    }

    const std::string modelPath = GetStringProperty(
        env,
        arguments[0],
        "model",
        ""
    );
    const std::string motionPath = GetStringProperty(
        env,
        arguments[0],
        "motion",
        ""
    );
    const int frameCount = static_cast<int>(GetNumberProperty(
        env,
        arguments[0],
        "frames",
        720.0
    ));
    const double fps = GetNumberProperty(env, arguments[0], "fps", 60.0);
    const double physicsFps = GetNumberProperty(
        env,
        arguments[0],
        "physicsFps",
        120.0
    );
    const int maxSubSteps = static_cast<int>(GetNumberProperty(
        env,
        arguments[0],
        "maxSubSteps",
        10.0
    ));

    Api api{};
    std::string loadError;
    if (!LoadApi(api, loadError))
        return MakeErrorResult(env, loadError);

    WisteriaContext context = 0U;
    WisteriaModel model = 0U;
    WisteriaMotion motion = 0U;
    double frameValue = 0.0;
    double maxFrameValue = 0.0;
    WisteriaVertexBounds bounds{};
    char errorBuffer[1024] = {};

    const auto check = [&](int status, const std::string& what) -> napi_value
    {
        if (status == kWisteriaOk)
            return nullptr;
        std::string message = what + " failed: " + api.statusName(status);
        if (api.lastErrorMessage(
                context,
                errorBuffer,
                sizeof(errorBuffer)
            ) == kWisteriaOk &&
            errorBuffer[0] != '\0')
        {
            message += ": ";
            message += errorBuffer;
        }
        return MakeErrorResult(env, message);
    };

    if (api.createContext(&context) != kWisteriaOk)
        return MakeErrorResult(env, "create context failed");
    if (napi_value error = check(
            api.loadModel(context, modelPath.c_str(), &model),
            "load model"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.loadMotion(
            context,
            model,
            motionPath.c_str(),
            &motion
            ),
            "load motion"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.motionMaxFrame(context, model, &maxFrameValue),
            "query max frame"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.setPhysicsSettings(
            context,
            model,
            static_cast<float>(1.0 / physicsFps),
            maxSubSteps,
            0.0f,
            -98.0f,
            0.0f
            ),
            "set physics settings"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.setMotionLooping(context, model, 1),
            "set motion looping"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.playMotion(context, model, motion),
            "play motion"
        ))
    {
        return error;
    }

    napi_value samples = nullptr;
    napi_create_array(env, &samples);
    std::uint32_t sampleIndex = 0U;
    const float deltaTime = static_cast<float>(1.0 / fps);
    for (int index = 0; index < frameCount; ++index)
    {
        if (napi_value error = check(
                api.update(context, model, deltaTime),
                "update"
            ))
        {
            return error;
        }
        if ((index + 1) % 60 != 0)
            continue;
        if (napi_value error = check(
                api.motionFrame(context, model, &frameValue),
                "query motion frame"
            ))
        {
            return error;
        }
        if (napi_value error = check(
                api.vertexBounds(context, model, &bounds),
                "query vertex bounds"
            ))
        {
            return error;
        }
        napi_value sample = nullptr;
        napi_create_object(env, &sample);
        napi_value frameNumber = nullptr;
        napi_create_int32(env, index + 1, &frameNumber);
        napi_set_named_property(env, sample, "frame", frameNumber);
        napi_value motionNumber = nullptr;
        napi_create_double(env, frameValue, &motionNumber);
        napi_set_named_property(env, sample, "motionFrame", motionNumber);
        napi_value finite = nullptr;
        napi_get_boolean(env, bounds.finite != 0, &finite);
        napi_set_named_property(env, sample, "finite", finite);
        napi_value min = nullptr;
        napi_create_array_with_length(env, 3U, &min);
        for (std::uint32_t axis = 0U; axis < 3U; ++axis)
        {
            napi_value component = nullptr;
            napi_create_double(env, bounds.minimum[axis], &component);
            napi_set_element(env, min, axis, component);
        }
        napi_set_named_property(env, sample, "min", min);
        napi_value max = nullptr;
        napi_create_array_with_length(env, 3U, &max);
        for (std::uint32_t axis = 0U; axis < 3U; ++axis)
        {
            napi_value component = nullptr;
            napi_create_double(env, bounds.maximum[axis], &component);
            napi_set_element(env, max, axis, component);
        }
        napi_set_named_property(env, sample, "max", max);
        napi_value displacement = nullptr;
        napi_create_double(
            env,
            bounds.maximumDisplacementFromBind,
            &displacement
        );
        napi_set_named_property(env, sample, "displacement", displacement);
        napi_value vertices = nullptr;
        napi_create_bigint_uint64(env, bounds.vertexCount, &vertices);
        napi_set_named_property(env, sample, "vertices", vertices);
        napi_set_element(env, samples, sampleIndex++, sample);
    }

    // Pause -> update must not advance -> resume.
    double pausedFrame = 0.0;
    if (napi_value error = check(
            api.pauseMotion(context, model),
            "pause motion"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.motionFrame(context, model, &pausedFrame),
            "query paused frame"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.update(context, model, deltaTime),
            "update while paused"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.motionFrame(context, model, &frameValue),
            "query frame after paused update"
        ))
    {
        return error;
    }
    if (napi_value error = check(
            api.resumeMotion(context, model),
            "resume motion"
        ))
    {
        return error;
    }

    napi_value result = nullptr;
    napi_create_object(env, &result);
    napi_value ok = nullptr;
    napi_get_boolean(env, true, &ok);
    napi_set_named_property(env, result, "ok", ok);
    napi_set_named_property(env, result, "samples", samples);
    napi_value maxFrame = nullptr;
    napi_create_double(env, maxFrameValue, &maxFrame);
    napi_set_named_property(env, result, "maxFrame", maxFrame);
    napi_value finalFrame = nullptr;
    napi_create_double(env, frameValue, &finalFrame);
    napi_set_named_property(env, result, "finalFrame", finalFrame);
    napi_value finite = nullptr;
    napi_get_boolean(env, bounds.finite != 0, &finite);
    napi_set_named_property(env, result, "finite", finite);
    napi_value displacement = nullptr;
    napi_create_double(
        env,
        bounds.maximumDisplacementFromBind,
        &displacement
    );
    napi_set_named_property(env, result, "maxDisplacement", displacement);
    napi_value vertices = nullptr;
    napi_create_bigint_uint64(env, bounds.vertexCount, &vertices);
    napi_set_named_property(env, result, "vertices", vertices);
    napi_value paused = nullptr;
    napi_create_double(env, pausedFrame, &paused);
    napi_set_named_property(env, result, "pausedFrame", paused);

    api.unloadMotion(context, model, motion);
    api.unloadModel(context, model);
    api.destroyContext(context);
    return result;
}

napi_value RunWindowDemo(napi_env env, napi_callback_info info)
{
    std::size_t argumentCount = 1U;
    napi_value arguments[1] = {nullptr};
    if (napi_get_cb_info(
            env,
            info,
            &argumentCount,
            arguments,
            nullptr,
            nullptr
        ) != napi_ok ||
        argumentCount < 1U)
    {
        return MakeErrorResult(env, "runWindowDemo(options) expects one object");
    }

    const std::string modelPath = GetStringProperty(
        env,
        arguments[0],
        "model",
        ""
    );
    const std::string motionPath = GetStringProperty(
        env,
        arguments[0],
        "motion",
        ""
    );
    const std::string scenePath = GetStringProperty(
        env,
        arguments[0],
        "scene",
        ""
    );
    const int frameCount = static_cast<int>(GetNumberProperty(
        env,
        arguments[0],
        "frames",
        360.0
    ));
    const double fps = GetNumberProperty(env, arguments[0], "fps", 60.0);
    const double physicsFps = GetNumberProperty(
        env,
        arguments[0],
        "physicsFps",
        120.0
    );
    const int maxSubSteps = static_cast<int>(GetNumberProperty(
        env,
        arguments[0],
        "maxSubSteps",
        10.0
    ));

    Api api{};
    std::string loadError;
    if (!LoadApi(api, loadError))
        return MakeErrorResult(env, loadError);

    WisteriaContext context = 0U;
    WisteriaWindow window = 0U;
    char errorBuffer[1024] = {};

    const auto fail = [&](int status, const std::string& what) -> napi_value
    {
        std::string message = what + " failed: " + api.statusName(status);
        if (api.lastErrorMessage(
                context,
                errorBuffer,
                sizeof(errorBuffer)
            ) == kWisteriaOk &&
            errorBuffer[0] != '\0')
        {
            message += ": ";
            message += errorBuffer;
        }
        if (window != 0U)
            api.windowDestroy(context, window);
        if (context != 0U)
            api.destroyContext(context);
        return MakeErrorResult(env, message);
    };

    if (api.createContext(&context) != kWisteriaOk)
        return MakeErrorResult(env, "create context failed");
    const int createStatus = api.windowCreate(
        context,
        960,
        720,
        "WISTERIA M4 - Node N-API window",
        &window
    );
    if (createStatus != kWisteriaOk)
        return fail(createStatus, "create window");
    const int loadStatus = api.windowLoadDemo(
        context,
        window,
        modelPath.c_str(),
        motionPath.c_str(),
        scenePath.empty() ? nullptr : scenePath.c_str(),
        static_cast<float>(1.0 / physicsFps),
        maxSubSteps
    );
    if (loadStatus != kWisteriaOk)
        return fail(loadStatus, "load demo");

    const float deltaTime = static_cast<float>(1.0 / fps);
    std::int32_t closed = 0;
    napi_value samples = nullptr;
    napi_create_array(env, &samples);
    std::uint32_t sampleIndex = 0U;
    for (int index = 0; index < frameCount; ++index)
    {
        if (api.windowPollAndRender(context, window, deltaTime) != kWisteriaOk)
            return fail(1, "render");
        if (api.windowShouldClose(context, window, &closed) != kWisteriaOk ||
            closed != 0)
        {
            closed = 1;
            break;
        }
        if ((index + 1) % 60 != 0)
            continue;
        float position[3] = {};
        float target[3] = {};
        float up[3] = {};
        if (api.windowCameraPose(
                context,
                window,
                position,
                target,
                up
            ) != kWisteriaOk)
        {
            return fail(1, "query camera");
        }
        std::int32_t spaceDown = 0;
        api.windowIsKeyDown(context, window, 18, &spaceDown);
        napi_value sample = nullptr;
        napi_create_object(env, &sample);
        napi_value frameNumber = nullptr;
        napi_create_int32(env, index + 1, &frameNumber);
        napi_set_named_property(env, sample, "frame", frameNumber);
        napi_value cam = nullptr;
        napi_create_array_with_length(env, 6U, &cam);
        for (std::uint32_t axis = 0U; axis < 6U; ++axis)
        {
            const float value = axis < 3U ? position[axis] : target[axis - 3U];
            napi_value component = nullptr;
            napi_create_double(env, value, &component);
            napi_set_element(env, cam, axis, component);
        }
        napi_set_named_property(env, sample, "camera", cam);
        napi_value space = nullptr;
        napi_create_int32(env, spaceDown, &space);
        napi_set_named_property(env, sample, "space", space);
        napi_set_element(env, samples, sampleIndex++, sample);
    }

    napi_value result = nullptr;
    napi_create_object(env, &result);
    napi_value ok = nullptr;
    napi_get_boolean(env, true, &ok);
    napi_set_named_property(env, result, "ok", ok);
    napi_set_named_property(env, result, "samples", samples);
    napi_value closedValue = nullptr;
    napi_create_int32(env, closed, &closedValue);
    napi_set_named_property(env, result, "closed", closedValue);

    api.windowDestroy(context, window);
    api.destroyContext(context);
    return result;
}

napi_value Init(napi_env env, napi_value exports)
{
    napi_value function = nullptr;
    napi_create_function(
        env,
        "runDemo",
        NAPI_AUTO_LENGTH,
        RunDemo,
        nullptr,
        &function
    );
    napi_set_named_property(env, exports, "runDemo", function);
    napi_value windowFunction = nullptr;
    napi_create_function(
        env,
        "runWindowDemo",
        NAPI_AUTO_LENGTH,
        RunWindowDemo,
        nullptr,
        &windowFunction
    );
    napi_set_named_property(env, exports, "runWindowDemo", windowFunction);
    return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
