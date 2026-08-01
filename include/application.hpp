#pragma once

#include "behaviour.hpp"
#include "framebuffer.hpp"
#include "manager.hpp"
#include "renderer.hpp"
#include "timer.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class Window;

struct WindowConfig
{
    int width = 640;
    int height = 480;
    std::string title = "FLORAL WISTERIA";
    bool shareOpenGlResources = true;
};

// Owns process-wide GLFW state, the shared resource manager, and schedules
// every window from one event loop. A window binds a Scene/Camera render view;
// framebuffers, renderers and VAOs remain context-local.
class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // These window operations must be called on the Application/GLFW thread.
    // During Run, newly created windows are committed at a frame boundary and
    // destruction is deferred until it is safe to release context-local state.
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
    ResourceManager& GetResources() noexcept;
    const ResourceManager& GetResources() const noexcept;

private:
    struct ManagedWindow
    {
        explicit ManagedWindow(std::unique_ptr<Window> nextWindow);

        // Destruction order is framebuffer -> renderer -> window. Application
        // makes this window's context current before destroying the record.
        std::unique_ptr<Window> window;
        Renderer renderer;
        SceneFramebuffer framebuffer;
    };

    ManagedWindow& Find(Window& window);
    const ManagedWindow& Find(const Window& window) const;
    void RenderWindow(ManagedWindow& managedWindow);
    void CommitPendingWindows();
    void DestroyClosedWindows();
    void TrackScene(const std::shared_ptr<Scene>& scene);
    void ClearTrackedScenes() noexcept;
    void RequireOwnerThread() const;
    void Shutdown() noexcept;

private:
    ResourceManager resources;
    std::vector<std::unique_ptr<ManagedWindow>> windows;
    std::vector<std::unique_ptr<ManagedWindow>> pendingWindows;
    std::vector<std::weak_ptr<Scene>> trackedScenes;
    Timer timer;
    std::thread::id ownerThread;
    bool glfwInitialized = false;
    bool running = false;
    bool closeRequested = false;
};
