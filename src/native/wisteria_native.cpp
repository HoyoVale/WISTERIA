#include "wisteria/native/wisteria_native.h"

#include "wisteria/platform/application.hpp"
#include "wisteria/platform/input.hpp"
#include "wisteria/platform/window.hpp"
#include "wisteria/rendering/camera.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/scene/scene.hpp"
#include "windows_path.hpp"

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

struct WindowEntry
{
    Window* window = nullptr;
    bool demoLoaded = false;
};

struct Context
{
    std::unordered_map<WisteriaModel, std::unique_ptr<ModelEntry>> models;
    WisteriaModel nextModelHandle = 1U;
    WisteriaMotion nextMotionHandle = 1U;
    std::unique_ptr<Application> application;
    std::unordered_map<WisteriaWindow, std::unique_ptr<WindowEntry>> windows;
    WisteriaWindow nextWindowHandle = 1U;
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

WindowEntry* FindWindow(Context& context, WisteriaWindow handle)
{
    const auto iterator = context.windows.find(handle);
    return iterator == context.windows.end() ? nullptr : iterator->second.get();
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

// The C ABI contract is UTF-8 paths. On Windows, std::filesystem::path
// constructed from a narrow string interprets it with the ANSI code page
// (e.g. GBK on zh-CN systems), which corrupts UTF-8 FFI input and can throw.
// Convert explicitly to UTF-16 first.
std::filesystem::path PathFromUtf8(const char* utf8)
{
#ifdef _WIN32
    if (utf8 == nullptr)
        return {};
    const std::wstring wide = WisteriaNativeUtf8ToWide(utf8);
    if (wide.empty())
        return {};
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(utf8 != nullptr ? utf8 : "");
#endif
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

/* --- Window (M4) --------------------------------------------------------- */

enum WisteriaStatus wisteria_window_create(
    WisteriaContext context,
    int width,
    int height,
    const char* title,
    WisteriaWindow* out_window
)
{
    if (width <= 0 || height <= 0 || title == nullptr ||
        title[0] == '\0' || out_window == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;

    if (handle->application == nullptr)
    {
        try
        {
            handle->application = std::make_unique<Application>();
        }
        catch (const std::exception& error)
        {
            SetError(*handle, error.what());
            return WISTERIA_ERROR_INITIALIZATION;
        }
        catch (...)
        {
            SetError(*handle, "GLFW initialization failed");
            return WISTERIA_ERROR_INITIALIZATION;
        }
    }

    try
    {
        WindowConfig config;
        config.width = width;
        config.height = height;
        config.title = title;
        config.shareOpenGlResources = true;
        Window& window = handle->application->CreateWindow(config);
        auto entry = std::make_unique<WindowEntry>();
        entry->window = &window;
        const WisteriaWindow windowHandle = handle->nextWindowHandle++;
        handle->windows.emplace(windowHandle, std::move(entry));
        *out_window = windowHandle;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INITIALIZATION;
    }
    catch (...)
    {
        SetError(*handle, "Unknown C++ exception while creating the window");
        return WISTERIA_ERROR_INITIALIZATION;
    }
}

enum WisteriaStatus wisteria_window_destroy(
    WisteriaContext context,
    WisteriaWindow window
)
{
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");

    try
    {
        handle->application->DestroyWindow(*entry->window);
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    handle->windows.erase(window);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_load_demo(
    WisteriaContext context,
    WisteriaWindow window,
    const char* model_path,
    const char* motion_path,
    const char* scene_path,
    float physics_fps,
    int32_t max_sub_steps
)
{
    if (physics_fps < 0.0f || !std::isfinite(physics_fps) ||
        max_sub_steps < 0)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    if (entry->demoLoaded)
    {
        SetError(*handle, "Demo is already loaded for this window");
        return WISTERIA_ERROR_ALREADY_EXISTS;
    }

    const std::filesystem::path modelPath =
        model_path != nullptr ? PathFromUtf8(model_path) :
                                std::filesystem::path{};
    const std::filesystem::path motionPath =
        motion_path != nullptr ? PathFromUtf8(motion_path) :
                                 std::filesystem::path{};
    const std::filesystem::path scenePath =
        scene_path != nullptr ? PathFromUtf8(scene_path) :
                                std::filesystem::path{};
    const bool sceneMode = !scenePath.empty();

    try
    {
        WindowManager& windowManager = handle->application->GetWindowManager();
        const SceneHandle scene = windowManager.CreateScene();
        SetupSabaMmdDemoScene(
            *scene,
            handle->application->GetResources(),
            *entry->window,
            false,
            modelPath,
            scenePath,
            sceneMode,
            motionPath,
            physics_fps > 0.0f ? physics_fps : 0.0f,
            max_sub_steps > 0 ? max_sub_steps : 0
        );
        windowManager.BindScene(*entry->window, scene);
        FreeCameraControllerSettings settings;
        settings.moveSpeed = sceneMode ? 12.0f : 2.5f;
        windowManager.EnableFreeCameraController(*entry->window, settings);
        entry->demoLoaded = true;
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        SetError(*handle, "Unknown C++ exception while loading the demo");
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_window_poll_and_render(
    WisteriaContext context,
    WisteriaWindow window,
    float delta_time
)
{
    if (!std::isfinite(delta_time) || delta_time < 0.0f)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    if (!entry->demoLoaded)
    {
        SetError(*handle, "Window demo is not loaded");
        return WISTERIA_ERROR_INITIALIZATION;
    }

    try
    {
        handle->application->PollEventsAndRender(delta_time);
        return WISTERIA_OK;
    }
    catch (const std::exception& error)
    {
        SetError(*handle, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        SetError(*handle, "Unknown C++ exception during window render");
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_window_should_close(
    WisteriaContext context,
    WisteriaWindow window,
    int32_t* out_closed
)
{
    if (out_closed == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    *out_closed = entry->window->ShouldClose() ? 1 : 0;
    return WISTERIA_OK;
}

namespace
{
bool ValidKey(int key)
{
    return key >= 0 && key < WISTERIA_KEY_COUNT;
}

bool ValidMouseButton(int button)
{
    return button >= 0 && button < WISTERIA_MOUSE_COUNT;
}
}

enum WisteriaStatus wisteria_window_is_key_down(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaKey key,
    int32_t* out_down
)
{
    if (out_down == nullptr || !ValidKey(key))
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    const InputKey mapped = static_cast<InputKey>(
        static_cast<std::size_t>(key)
    );
    *out_down = entry->window->GetInput().IsKeyDown(mapped) ? 1 : 0;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_was_key_pressed(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaKey key,
    int32_t* out_pressed
)
{
    if (out_pressed == nullptr || !ValidKey(key))
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    const InputKey mapped = static_cast<InputKey>(
        static_cast<std::size_t>(key)
    );
    *out_pressed = entry->window->GetInput().WasKeyPressed(mapped) ? 1 : 0;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_was_key_released(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaKey key,
    int32_t* out_released
)
{
    if (out_released == nullptr || !ValidKey(key))
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    const InputKey mapped = static_cast<InputKey>(
        static_cast<std::size_t>(key)
    );
    *out_released = entry->window->GetInput().WasKeyReleased(mapped) ? 1 : 0;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_is_mouse_button_down(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaMouseButton button,
    int32_t* out_down
)
{
    if (out_down == nullptr || !ValidMouseButton(button))
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    const InputMouseButton mapped = static_cast<InputMouseButton>(
        static_cast<std::size_t>(button)
    );
    *out_down =
        entry->window->GetInput().IsMouseButtonDown(mapped) ? 1 : 0;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_cursor_delta(
    WisteriaContext context,
    WisteriaWindow window,
    float* out_x,
    float* out_y
)
{
    if (out_x == nullptr || out_y == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    const MouseDelta delta = entry->window->GetInput().CursorDelta();
    *out_x = static_cast<float>(delta.x);
    *out_y = static_cast<float>(delta.y);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_scroll_delta(
    WisteriaContext context,
    WisteriaWindow window,
    float* out_y
)
{
    if (out_y == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    *out_y = static_cast<float>(entry->window->GetInput().ScrollDeltaY());
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_set_cursor_captured(
    WisteriaContext context,
    WisteriaWindow window,
    int32_t captured
)
{
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    entry->window->GetInput().SetCursorCaptured(captured != 0);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_set_camera(
    WisteriaContext context,
    WisteriaWindow window,
    const float position[3],
    const float target[3],
    const float up[3]
)
{
    if (position == nullptr || target == nullptr || up == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    Camera& camera = entry->window->GetCamera();
    camera.SetPosition(glm::vec3(position[0], position[1], position[2]));
    camera.SetTarget(glm::vec3(target[0], target[1], target[2]));
    camera.SetUp(glm::vec3(up[0], up[1], up[2]));
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_camera_pose(
    WisteriaContext context,
    WisteriaWindow window,
    float out_position[3],
    float out_target[3],
    float out_up[3]
)
{
    if (out_position == nullptr || out_target == nullptr || out_up == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    const glm::vec3& position = entry->window->GetCamera().Position();
    const glm::vec3& target = entry->window->GetCamera().Target();
    const glm::vec3& up = entry->window->GetCamera().Up();
    out_position[0] = position.x;
    out_position[1] = position.y;
    out_position[2] = position.z;
    out_target[0] = target.x;
    out_target[1] = target.y;
    out_target[2] = target.z;
    out_up[0] = up.x;
    out_up[1] = up.y;
    out_up[2] = up.z;
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_set_camera_speed(
    WisteriaContext context,
    WisteriaWindow window,
    float move_speed
)
{
    if (!std::isfinite(move_speed) || move_speed <= 0.0f)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    Context* handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    FreeCameraControllerSettings settings;
    settings.moveSpeed = move_speed;
    handle->application->GetWindowManager().SetFreeCameraControllerSettings(
        *entry->window,
        settings
    );
    return WISTERIA_OK;
}

} /* extern "C" */
