#pragma once

#include "wisteria/assets/manager.hpp"
#include "wisteria/core/timer.hpp"
#include "wisteria/platform/window_manager.hpp"
#include <cstddef>
#include <memory>

// Holds one reference to process-wide GLFW state and owns one OpenGL
// resource share group. WindowManager owns its windows and context-local state.
namespace wisteria
{
class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Compatibility facade. New code may use GetWindowManager() directly.
    Window& CreateWindow(const WindowConfig& config = {});
    void DestroyWindow(Window& window);
    std::shared_ptr<Scene> CreateScene();
    std::shared_ptr<Camera> CreateCamera(
        const CameraParam& parameters = {}
    );
    void BindScene(Window& window, std::shared_ptr<Scene> scene);
    void BindCamera(Window& window, std::shared_ptr<Camera> camera);
    void BindRenderView(
        Window& window,
        std::shared_ptr<Scene> scene,
        std::shared_ptr<Camera> camera
    );
    Scene& GetScene(Window& window);
    const Scene& GetScene(const Window& window) const;
    Camera& GetCamera(Window& window);
    const Camera& GetCamera(const Window& window) const;
    void EnableFreeCameraController(
        Window& window,
        const FreeCameraControllerSettings& settings = {}
    );
    void DisableFreeCameraController(Window& window) noexcept;

    int Run();

    // Pull-model frame step used by the native C ABI window layer (M4):
    // input frame -> glfwPollEvents -> scene update -> render -> swap.
    // Frontends call this once per frame instead of blocking in Run().
    void PollEventsAndRender(float deltaTime);
    // One explicit frame: poll events, advance each unique scene through the
    // frame pipeline (animation -> pre-physics -> physics -> post-physics ->
    // world transforms), then render every window. Run() and
    // PollEventsAndRender() both delegate here.
    void Tick(float deltaTime);
    void RequestClose() noexcept;
    std::size_t WindowCount() const noexcept;
    bool IsRunning() const noexcept;

    Renderer& GetRenderer(Window& window);
    SceneFramebuffer& GetFramebuffer(Window& window);
    WindowManager& GetWindowManager() noexcept;
    const WindowManager& GetWindowManager() const noexcept;
    ResourceManager& GetResources() noexcept;
    const ResourceManager& GetResources() const noexcept;

private:
    void Shutdown() noexcept;

private:
    ResourceManager resources;
    WindowManager windowManager;
    Timer timer;
    bool glfwInitialized = false;
    bool running = false;
    bool closeRequested = false;
};
}  // namespace wisteria
