#pragma once

#include "wisteria/scene/behaviour.hpp"
#include "wisteria/rendering/framebuffer.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/platform/window.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace wisteria
{
class Application;

struct WindowConfig
{
    int width = 640;
    int height = 480;
    std::string title = "FLORAL WISTERIA";
    bool shareOpenGlResources = true;
};

// Owns native windows and all context-local rendering state. Application owns
// GLFW itself and coordinates destruction of shared ResourceManager objects.
class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;
    WindowManager(WindowManager&&) = delete;
    WindowManager& operator=(WindowManager&&) = delete;

    // These operations must run on the GLFW owner thread. During Application::
    // Run, creation and destruction are committed at safe frame boundaries.
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
    Renderer& GetRenderer(Window& window);
    SceneFramebuffer& GetFramebuffer(Window& window);

    void EnableFreeCameraController(
        Window& window,
        const FreeCameraControllerSettings& settings = {}
    );
    void DisableFreeCameraController(Window& window) noexcept;
    void SetFreeCameraControllerSettings(
        Window& window,
        const FreeCameraControllerSettings& settings
    );

    std::size_t WindowCount() const noexcept;

private:
    friend class Application;

    struct ManagedWindow
    {
        ManagedWindow(
            std::unique_ptr<Window> nextWindow,
            std::string captureStem
        );

        // Reverse member destruction gives framebuffer -> renderer -> window.
        std::unique_ptr<Window> window;
        std::string captureStem;
        Renderer renderer;
        SceneFramebuffer framebuffer;
        std::size_t renderedFrames = 0U;
        double profileUpdateMilliseconds = 0.0;
        double profileRenderMilliseconds = 0.0;
        double profilePresentMilliseconds = 0.0;
        double profileSwapMilliseconds = 0.0;
    };

    ManagedWindow& Find(Window& window);
    const ManagedWindow& Find(const Window& window) const;
    void SetRunning(bool nextRunning) noexcept;
    bool HasActiveWindows() const noexcept;
    bool AllWindowsClosed() const noexcept;
    void CommitPendingWindows();
    void BeginInputFrames() noexcept;
    void DestroyClosedWindows();
    // Per-window input/camera controllers. Application::Tick calls this
    // before the scene pipeline and once per render.
    void UpdateWindowControllers(float deltaTime);
    // Distinct non-closed scenes, deduplicated so a shared scene advances
    // exactly once per frame even when several windows render it.
    std::vector<Scene*> UniqueScenes() const;
    void RenderAll();
    void RenderWindow(ManagedWindow& managedWindow);
    void TrackScene(const std::shared_ptr<Scene>& scene);
    void ClearTrackedScenes() noexcept;
    void ReleaseContextLocalResources() noexcept;
    GLFWwindow* SharedResourceContext() const noexcept;
    void DestroyAllWindows() noexcept;
    void RequireOwnerThread() const;

private:
    std::vector<std::unique_ptr<ManagedWindow>> windows;
    std::vector<std::unique_ptr<ManagedWindow>> pendingWindows;
    std::vector<std::weak_ptr<Scene>> trackedScenes;
    std::thread::id ownerThread;
    bool running = false;
};
}  // namespace wisteria
