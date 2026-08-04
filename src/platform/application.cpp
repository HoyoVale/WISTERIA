#include "wisteria/common/pch.hpp"
#include "wisteria/platform/application.hpp"
#include "wisteria/rendering/material.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <utility>

Application::Application()
{
    if (!glfwInit())
        throw std::runtime_error("GLFW initialization failed");

    this->glfwInitialized = true;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

Application::~Application()
{
    this->Shutdown();
}

Window& Application::CreateWindow(const WindowConfig& config)
{
    if (!this->glfwInitialized)
        throw std::logic_error("Application is not initialized");
    return this->windowManager.CreateWindow(config);
}

void Application::DestroyWindow(Window& window)
{
    this->windowManager.DestroyWindow(window);
}

std::shared_ptr<Scene> Application::CreateScene()
{
    return this->windowManager.CreateScene();
}

std::shared_ptr<Camera> Application::CreateCamera(
    const CameraParam& parameters
)
{
    return this->windowManager.CreateCamera(parameters);
}

void Application::BindScene(
    Window& window,
    std::shared_ptr<Scene> scene
)
{
    this->windowManager.BindScene(window, std::move(scene));
}

void Application::BindCamera(
    Window& window,
    std::shared_ptr<Camera> camera
)
{
    this->windowManager.BindCamera(window, std::move(camera));
}

void Application::BindRenderView(
    Window& window,
    std::shared_ptr<Scene> scene,
    std::shared_ptr<Camera> camera
)
{
    this->windowManager.BindRenderView(
        window,
        std::move(scene),
        std::move(camera)
    );
}

Scene& Application::GetScene(Window& window)
{
    return this->windowManager.GetScene(window);
}

const Scene& Application::GetScene(const Window& window) const
{
    return this->windowManager.GetScene(window);
}

Camera& Application::GetCamera(Window& window)
{
    return this->windowManager.GetCamera(window);
}

const Camera& Application::GetCamera(const Window& window) const
{
    return this->windowManager.GetCamera(window);
}

void Application::EnableFreeCameraController(
    Window& window,
    const FreeCameraControllerSettings& settings
)
{
    this->windowManager.EnableFreeCameraController(window, settings);
}

void Application::DisableFreeCameraController(Window& window) noexcept
{
    this->windowManager.DisableFreeCameraController(window);
}

int Application::Run()
{
    this->windowManager.RequireOwnerThread();
    if (this->running)
        throw std::logic_error("Application is already running");
    if (!this->windowManager.HasActiveWindows())
        throw std::logic_error("Application requires at least one window");

    this->running = true;
    this->closeRequested = false;
    this->windowManager.SetRunning(true);
    this->timer.Start();
    try
    {
        while (this->windowManager.HasActiveWindows() &&
            !this->closeRequested)
        {
            this->windowManager.CommitPendingWindows();
            this->windowManager.BeginInputFrames();

            glfwPollEvents();
            this->windowManager.CommitPendingWindows();
            this->timer.Now();
            if (this->windowManager.AllWindowsClosed())
                break;
            this->windowManager.DestroyClosedWindows();

            this->windowManager.Update(this->timer.GetDeltaTime());
            this->windowManager.RenderAll();
        }
    }
    catch (...)
    {
        this->windowManager.SetRunning(false);
        this->running = false;
        throw;
    }

    this->windowManager.SetRunning(false);
    this->running = false;
    return 0;
}

void Application::RequestClose() noexcept
{
    this->closeRequested = true;
}

std::size_t Application::WindowCount() const noexcept
{
    return this->windowManager.WindowCount();
}

bool Application::IsRunning() const noexcept
{
    return this->running;
}

Renderer& Application::GetRenderer(Window& window)
{
    return this->windowManager.GetRenderer(window);
}

SceneFramebuffer& Application::GetFramebuffer(Window& window)
{
    return this->windowManager.GetFramebuffer(window);
}

WindowManager& Application::GetWindowManager() noexcept
{
    return this->windowManager;
}

const WindowManager& Application::GetWindowManager() const noexcept
{
    return this->windowManager;
}

ResourceManager& Application::GetResources() noexcept
{
    return this->resources;
}

const ResourceManager& Application::GetResources() const noexcept
{
    return this->resources;
}

void Application::Shutdown() noexcept
{
    this->running = false;
    this->closeRequested = true;
    this->windowManager.SetRunning(false);

    // Context-local state must die in its owning context before shared objects.
    this->windowManager.ReleaseContextLocalResources();
    this->windowManager.ClearTrackedScenes();

    // Shared textures, buffers and programs still require one live context.
    GLFWwindow* resourceContext =
        this->windowManager.SharedResourceContext();
    if (resourceContext != nullptr)
    {
        glfwMakeContextCurrent(resourceContext);
        this->resources.Clear();
        // Shared programs must die while a GL context is still current;
        // otherwise glDeleteProgram would run without a context at exit.
        Material::ReleaseSharedPrograms();
    }

    this->windowManager.DestroyAllWindows();

    if (this->glfwInitialized)
    {
        glfwMakeContextCurrent(nullptr);
        glfwTerminate();
        this->glfwInitialized = false;
    }
}
