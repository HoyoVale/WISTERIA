#pragma once

#include "manager.hpp"
#include "timer.hpp"
#include "window_manager.hpp"
#include <cstddef>
#include <memory>

// Owns process-wide GLFW state, the frame clock and shared resources.
// WindowManager owns native windows and context-local rendering state.
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
