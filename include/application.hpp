#pragma once

#include "framebuffer.hpp"
#include "renderer.hpp"
#include "timer.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Window;

struct WindowConfig
{
    int width = 640;
    int height = 480;
    std::string title = "FLORAL WISTERIA";
    bool shareOpenGlResources = true;
};

// Owns process-wide GLFW state and schedules every window from one event loop.
// Framebuffers and renderers remain per-window because those objects contain
// OpenGL state that is local to the window's context.
class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    Window& CreateWindow(const WindowConfig& config = {});
    int Run();
    void RequestClose() noexcept;

    std::size_t WindowCount() const noexcept;
    bool IsRunning() const noexcept;
    Renderer& GetRenderer(Window& window);
    SceneFramebuffer& GetFramebuffer(Window& window);

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
    void RenderWindow(ManagedWindow& managedWindow, float deltaTime);
    void DestroyClosedWindows();
    void Shutdown() noexcept;

private:
    std::vector<std::unique_ptr<ManagedWindow>> windows;
    Timer timer;
    bool glfwInitialized = false;
    bool running = false;
    bool closeRequested = false;
};
