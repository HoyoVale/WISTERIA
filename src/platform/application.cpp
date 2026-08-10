#include "wisteria/common/pch.hpp"
#include "wisteria/platform/application.hpp"
#include "glfw_lifetime.hpp"
#include "rendering/backend/opengl/open_gl_render_device.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <utility>

namespace wisteria
{
namespace
{
using wisteria::platform::AcquireGlfwLifetime;
using wisteria::platform::ReleaseGlfwLifetime;

GraphicsDevice& LegacyGraphicsDevice(RenderDevice& device)
{
    auto& openGl = dynamic_cast<OpenGlRenderDevice&>(device);
    return openGl.LegacyGraphicsDevice();
}
}

Application::Application()
{
    if (!AcquireGlfwLifetime())
        throw std::runtime_error("GLFW initialization failed");

    this->glfwInitialized = true;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // All resources created through the manager share one program cache so
    // identical shader pairs compile once per application, not once per
    // material.
    this->renderDevice = std::make_unique<OpenGlRenderDevice>();
    GraphicsDevice& graphicsDevice =
        LegacyGraphicsDevice(*this->renderDevice);
    this->resources.BindGraphicsDevice(graphicsDevice);
    this->resources.SetRenderCache(
        dynamic_cast<OpenGlRenderDevice&>(*this->renderDevice).RenderCache()
    );
    this->windowManager.SetRenderDevice(*this->renderDevice);
    this->windowManager.SetGraphicsDevice(graphicsDevice);
}

Application::~Application()
{
    this->Shutdown();
}

Window& Application::CreateWindow(const WindowConfig& config)
{
    if (!this->glfwInitialized)
        throw std::logic_error("Application is not initialized");
    Window& window = this->windowManager.CreateWindow(config);
    // The first window registers the device's share-group identity. Every
    // window of this Application maps to the same token.
    GraphicsDevice& graphicsDevice =
        LegacyGraphicsDevice(*this->renderDevice);
    if (!graphicsDevice.HasShareGroupToken())
        graphicsDevice.SetShareGroupToken(window.ShareGroupToken());
    // Capabilities must be queried while a context of the owning share
    // group is current. WindowManager restores the previous context before
    // returning, so enter a short transaction on the new window's context.
    GLFWwindow* previousContext = glfwGetCurrentContext();
    glfwMakeContextCurrent(window.GetGLFWwindow());
    GraphicsDevice::SetCurrentContext(window.GetGLFWwindow());
    GraphicsDevice::SetCurrentShareGroup(window.ShareGroupToken());
    try
    {
        dynamic_cast<OpenGlRenderDevice&>(*this->renderDevice)
            .RefreshCapabilities();
    }
    catch (...)
    {
        glfwMakeContextCurrent(previousContext);
        GraphicsDevice::SetCurrentContext(previousContext);
        GraphicsDevice::SetCurrentShareGroup(
            previousContext != nullptr
                ? window.ShareGroupToken()
                : nullptr
        );
        throw;
    }
    glfwMakeContextCurrent(previousContext);
    GraphicsDevice::SetCurrentContext(previousContext);
    GraphicsDevice::SetCurrentShareGroup(
        previousContext != nullptr ? window.ShareGroupToken() : nullptr
    );
    return window;
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
            !this->closeRequested &&
            !this->windowManager.AllWindowsClosed())
        {
            this->timer.Now();
            this->Tick(this->timer.GetDeltaTime());
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

void Application::PollEventsAndRender(float deltaTime)
{
    this->Tick(deltaTime);
}

void Application::Tick(float deltaTime)
{
    this->windowManager.RequireOwnerThread();
    this->windowManager.CommitPendingWindows();
    this->windowManager.BeginInputFrames();
    glfwPollEvents();
    this->windowManager.CommitPendingWindows();
    this->windowManager.DestroyClosedWindows();
    if (this->windowManager.AllWindowsClosed())
        return;

    // Per-window input/camera controllers.
    this->windowManager.UpdateWindowControllers(deltaTime);

    // Frame pipeline: every unique scene advances exactly once per frame, in
    // explicit phase order, before any window renders it. Multi-window setups
    // render the same scene several times but never advance it more than once.
    for (Scene* scene : this->windowManager.UniqueScenes())
    {
        scene->UpdateAnimation(deltaTime);
        scene->UpdatePrePhysics(deltaTime);
        scene->StepPhysics(deltaTime);
        scene->UpdatePostPhysics();
        scene->UpdateWorldTransforms();
    }

    this->windowManager.RenderAll();
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

GraphicsDevice& Application::GetGraphicsDevice() noexcept
{
    return LegacyGraphicsDevice(*this->renderDevice);
}

const GraphicsDevice& Application::GetGraphicsDevice() const noexcept
{
    return LegacyGraphicsDevice(*this->renderDevice);
}

RenderDevice& Application::GetRenderDevice() noexcept
{
    return *this->renderDevice;
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
        const GraphicsShareGroupToken shareGroup =
            this->windowManager.ShareGroupToken();
        glfwMakeContextCurrent(resourceContext);
        GraphicsDevice::SetCurrentContext(resourceContext);
        GraphicsDevice::SetCurrentShareGroup(shareGroup);
        // Verify the registered share group, then release every GPU
        // resource owned by the device (programs first, then resources whose
        // materials hold references into the same cache). The token is only
        // registered by Application::CreateWindow; callers that create
        // windows through WindowManager directly skip the assertion.
        GraphicsDevice& graphicsDevice =
            LegacyGraphicsDevice(*this->renderDevice);
        bool releaseSharedGlResources = true;
        if (graphicsDevice.HasShareGroupToken())
        {
            try
            {
                graphicsDevice.RequireShareGroupToken(shareGroup);
            }
            catch (const std::exception& error)
            {
                std::cerr
                    << "[ERROR] share-group validation failed; skipping GL "
                       "resource teardown: "
                    << error.what() << '\n';
                releaseSharedGlResources = false;
            }
        }
        if (releaseSharedGlResources)
        {
            graphicsDevice.ReleaseAll();
            this->resources.Clear();
            // R2.0 Phase 0C 6A: shared realizations outlive ResourceManager
            // objects; clear them while a context is still current, before
            // the last window (and its context) is destroyed.
            dynamic_cast<OpenGlRenderDevice&>(*this->renderDevice)
                .RenderCache()
                .Clear();
        }
    }

    this->windowManager.DestroyAllWindows();

    if (this->glfwInitialized)
    {
        glfwMakeContextCurrent(nullptr);
        GraphicsDevice::SetCurrentContext(nullptr);
        GraphicsDevice::SetCurrentShareGroup(nullptr);
        ReleaseGlfwLifetime();
        this->glfwInitialized = false;
    }
}
}  // namespace wisteria
