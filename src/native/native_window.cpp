#include "wisteria/native/wisteria_native.h"
#include "internal/native_context.hpp"

#include "wisteria/platform/application.hpp"
#include "wisteria/platform/input.hpp"
#include "wisteria/platform/window.hpp"
#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/scene/scene.hpp"

#include <cmath>
#include <filesystem>
#include <memory>

using namespace wisteria::native;
using namespace wisteria;

extern "C"
{
enum WisteriaStatus wisteria_window_create(
    WisteriaContext context,
    int width,
    int height,
    const char* title,
    WisteriaWindow* out_window
)
{
    if (out_window != nullptr)
        *out_window = 0U;
    if (width <= 0 || height <= 0 || title == nullptr ||
        title[0] == '\0' || out_window == nullptr)
    {
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    const ContextLease handle = FindContext(context);
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

enum WisteriaStatus wisteria_window_create_hidden(
    WisteriaContext context,
    int width,
    int height,
    WisteriaWindow* out_window
)
{
    if (out_window != nullptr)
        *out_window = 0U;
    if (width <= 0 || height <= 0 || out_window == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
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
        config.title = "WISTERIA headless render target";
        config.shareOpenGlResources = true;
        config.visible = false;
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
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");

    try
    {
        // Scene handles bound to this window cannot outlive the Window* they
        // reference. Invalidate them before destroying the platform object.
        for (auto iterator = handle->scenes.begin();
             iterator != handle->scenes.end();)
        {
            if (iterator->second->windowHandle == window)
                iterator = handle->scenes.erase(iterator);
            else
                ++iterator;
        }
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
    const ContextLease handle = FindContext(context);
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

enum WisteriaStatus wisteria_poll_and_render(
    WisteriaContext context,
    float delta_time
)
{
    if (!std::isfinite(delta_time) || delta_time < 0.0f)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    if (handle->application == nullptr)
    {
        SetError(*handle, "Context has no desktop application");
        return WISTERIA_ERROR_INITIALIZATION;
    }

    bool hasLoadedWindow = false;
    for (const auto& [windowHandle, entry] : handle->windows)
    {
        (void)windowHandle;
        if (entry != nullptr && entry->window != nullptr && entry->demoLoaded)
        {
            hasLoadedWindow = true;
            break;
        }
    }
    if (!hasLoadedWindow)
    {
        SetError(*handle, "Context has no window with a loaded demo");
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
        SetError(*handle, "Unknown C++ exception during context render");
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_window_poll_and_render(
    WisteriaContext context,
    WisteriaWindow window,
    float delta_time
)
{
    const ContextLease handle = FindContext(context);
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
    return wisteria_poll_and_render(context, delta_time);
}

enum WisteriaStatus wisteria_window_should_close(
    WisteriaContext context,
    WisteriaWindow window,
    int32_t* out_closed
)
{
    if (out_closed == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    *out_closed = entry->window->ShouldClose() ? 1 : 0;
    return WISTERIA_OK;
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    Camera& camera = entry->window->GetCamera();
    return GuardAbi(*handle, [&]
    {
        CameraParam replacement = camera.GetParam();
        replacement.Position = glm::vec3(
            position[0],
            position[1],
            position[2]
        );
        replacement.Target = glm::vec3(target[0], target[1], target[2]);
        replacement.Up = glm::vec3(up[0], up[1], up[2]);
        camera.SetParam(replacement);
    });
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
    const ContextLease handle = FindContext(context);
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
    const ContextLease handle = FindContext(context);
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

enum WisteriaStatus wisteria_window_set_render_settings(
    WisteriaContext context,
    WisteriaWindow window,
    const struct WisteriaRenderSettings* settings
)
{
    if (settings == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    if (handle->application == nullptr)
        return InvalidHandle(*handle, "Context has no application");

    const bool validBias = settings->shadow_bias < 0.0f
        ? true
        : std::isfinite(settings->shadow_bias);
    if (!validBias ||
        (settings->shadow_map_size != 0 &&
         (settings->shadow_map_size < 256 ||
          settings->shadow_map_size > 4096)) ||
        (settings->shadow_pcf_radius != 0 &&
         (settings->shadow_pcf_radius < 1 ||
          settings->shadow_pcf_radius > 3)) ||
        (settings->shadows_enabled != -1 &&
         settings->shadows_enabled != 0 &&
         settings->shadows_enabled != 1) ||
        (settings->ground_shadow_enabled != -1 &&
         settings->ground_shadow_enabled != 0 &&
         settings->ground_shadow_enabled != 1))
    {
        SetError(*handle, "Render settings contain invalid values");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }

    Renderer::Config config =
        handle->application->GetRenderer(*entry->window).GetConfig();
    if (settings->shadow_map_size != 0)
        config.shadowMapSize = settings->shadow_map_size;
    if (settings->shadow_pcf_radius != 0)
        config.shadowPcfRadius = settings->shadow_pcf_radius;
    if (settings->shadows_enabled != -1)
        config.shadowsEnabled = settings->shadows_enabled != 0;
    if (settings->ground_shadow_enabled != -1)
        config.groundShadowEnabled = settings->ground_shadow_enabled != 0;
    if (settings->shadow_bias >= 0.0f)
        config.shadowBias = settings->shadow_bias;
    handle->application->GetRenderer(*entry->window).SetConfig(config);
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_framebuffer_size(
    WisteriaContext context,
    WisteriaWindow window,
    int32_t* out_width,
    int32_t* out_height
)
{
    if (out_width == nullptr || out_height == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    const SceneFramebuffer& framebuffer =
        handle->application->GetFramebuffer(*entry->window);
    *out_width = framebuffer.Width();
    *out_height = framebuffer.Height();
    return WISTERIA_OK;
}

enum WisteriaStatus wisteria_window_read_pixels(
    WisteriaContext context,
    WisteriaWindow window,
    unsigned char* rgba,
    size_t buffer_size
)
{
    if (rgba == nullptr || buffer_size == 0U)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
        return WISTERIA_ERROR_NOT_FOUND;
    WindowEntry* entry = FindWindow(*handle, window);
    if (entry == nullptr || entry->window == nullptr)
        return InvalidHandle(*handle, "Window handle is invalid");
    if (handle->application == nullptr)
        return InvalidHandle(*handle, "Context has no application");

    entry->window->MakeContextCurrent();
    const SceneFramebuffer& framebuffer =
        handle->application->GetFramebuffer(*entry->window);
    if (framebuffer.Width() <= 0 || framebuffer.Height() <= 0)
        return WISTERIA_ERROR_INITIALIZATION;
    const std::size_t requiredBytes =
        static_cast<std::size_t>(framebuffer.Width()) *
        static_cast<std::size_t>(framebuffer.Height()) * 4U;
    if (buffer_size < requiredBytes)
    {
        SetError(*handle, "Readback buffer is too small");
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }

    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = GL_BACK;
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer.Id());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0,
        0,
        framebuffer.Width(),
        framebuffer.Height(),
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba
    );
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previousReadFramebuffer)
    );
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    return WISTERIA_OK;
}

} /* extern "C" */
