#include "pch.hpp"
#include "application.hpp"
#include "window.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

Application::ManagedWindow::ManagedWindow(
    std::unique_ptr<Window> nextWindow
)
    : window(std::move(nextWindow))
{
    if (this->window == nullptr)
        throw std::invalid_argument("Managed window cannot be null");
}

Application::Application()
    : ownerThread(std::this_thread::get_id())
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
    this->RequireOwnerThread();
    if (!this->glfwInitialized)
        throw std::logic_error("Application is not initialized");
    if (config.width <= 0 || config.height <= 0)
        throw std::invalid_argument("Window dimensions must be positive");
    if (config.title.empty())
        throw std::invalid_argument("Window title cannot be empty");
    if ((!this->windows.empty() || !this->pendingWindows.empty()) &&
        !config.shareOpenGlResources)
    {
        throw std::invalid_argument(
            "Every window must join the Application OpenGL resource share group"
        );
    }

    GLFWwindow* sharedContext = nullptr;
    if (config.shareOpenGlResources)
    {
        if (!this->windows.empty())
            sharedContext = this->windows.front()->window->GetGLFWwindow();
        else if (!this->pendingWindows.empty())
        {
            sharedContext =
                this->pendingWindows.front()->window->GetGLFWwindow();
        }
    }

    GLFWwindow* previousContext = glfwGetCurrentContext();
    std::unique_ptr<Window> window;
    try
    {
        window = std::unique_ptr<Window>(new Window(
            config.width,
            config.height,
            config.title,
            sharedContext
        ));
    }
    catch (...)
    {
        glfwMakeContextCurrent(previousContext);
        throw;
    }
    glfwMakeContextCurrent(previousContext);

    Window& result = *window;
    this->TrackScene(window->GetSceneHandle());
    auto managed = std::make_unique<ManagedWindow>(std::move(window));
    if (this->running)
        this->pendingWindows.push_back(std::move(managed));
    else
        this->windows.push_back(std::move(managed));
    return result;
}

void Application::DestroyWindow(Window& window)
{
    this->RequireOwnerThread();
    this->Find(window);
    if (window.GetGLFWwindow() != nullptr)
        glfwSetWindowShouldClose(window.GetGLFWwindow(), GLFW_TRUE);
}

std::shared_ptr<Scene> Application::CreateScene()
{
    auto scene = std::make_shared<Scene>();
    this->TrackScene(scene);
    return scene;
}

std::shared_ptr<Camera> Application::CreateCamera(
    const CameraParam& parameters
)
{
    return std::make_shared<Camera>(parameters);
}

void Application::BindScene(
    Window& window,
    std::shared_ptr<Scene> scene
)
{
    this->Find(window);
    this->TrackScene(scene);
    window.BindScene(std::move(scene));
}

void Application::BindCamera(
    Window& window,
    std::shared_ptr<Camera> camera
)
{
    this->Find(window);
    window.BindCamera(std::move(camera));
}

void Application::BindRenderView(
    Window& window,
    std::shared_ptr<Scene> scene,
    std::shared_ptr<Camera> camera
)
{
    this->Find(window);
    this->TrackScene(scene);
    window.BindRenderView(std::move(scene), std::move(camera));
}

Scene& Application::GetScene(Window& window)
{
    this->Find(window);
    return window.GetScene();
}

const Scene& Application::GetScene(const Window& window) const
{
    this->Find(window);
    return window.GetScene();
}

Camera& Application::GetCamera(Window& window)
{
    this->Find(window);
    return window.GetCamera();
}

const Camera& Application::GetCamera(const Window& window) const
{
    this->Find(window);
    return window.GetCamera();
}

void Application::EnableFreeCameraController(
    Window& window,
    const FreeCameraControllerSettings& settings
)
{
    this->Find(window);
    window.EnableFreeCameraController(settings);
}

void Application::DisableFreeCameraController(Window& window) noexcept
{
    try
    {
        this->Find(window);
        window.DisableFreeCameraController();
    }
    catch (...)
    {
    }
}

int Application::Run()
{
    this->RequireOwnerThread();
    if (this->running)
        throw std::logic_error("Application is already running");
    if (this->windows.empty())
        throw std::logic_error("Application requires at least one window");

    this->running = true;
    this->closeRequested = false;
    this->timer.Start();
    try
    {
        while (!this->windows.empty() && !this->closeRequested)
        {
            this->CommitPendingWindows();
            for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
                managed->window->BeginInputFrame();

            glfwPollEvents();
            this->CommitPendingWindows();
            this->timer.Now();
            const bool allWindowsClosed = std::all_of(
                this->windows.begin(),
                this->windows.end(),
                [](const std::unique_ptr<ManagedWindow>& managed)
                {
                    return managed->window->ShouldClose();
                }
            );
            if (allWindowsClosed)
                break;
            this->DestroyClosedWindows();

            const float deltaTime = this->timer.GetDeltaTime();
            for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
            {
                if (!managed->window->ShouldClose())
                    managed->window->Update(deltaTime);
            }

            std::unordered_set<Scene*> scheduledScenes;
            std::vector<SceneHandle> scenesToUpdate;
            scenesToUpdate.reserve(this->windows.size());
            for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
            {
                if (managed->window->ShouldClose())
                    continue;
                const SceneHandle& scene = managed->window->GetSceneHandle();
                if (scheduledScenes.insert(scene.get()).second)
                    scenesToUpdate.push_back(scene);
            }
            for (const SceneHandle& scene : scenesToUpdate)
                scene->Update(deltaTime);

            for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
                this->RenderWindow(*managed);
        }
    }
    catch (...)
    {
        this->running = false;
        throw;
    }

    this->running = false;
    return 0;
}

void Application::RequestClose() noexcept
{
    this->closeRequested = true;
}

std::size_t Application::WindowCount() const noexcept
{
    return this->windows.size() + this->pendingWindows.size();
}

bool Application::IsRunning() const noexcept
{
    return this->running;
}

Renderer& Application::GetRenderer(Window& window)
{
    return this->Find(window).renderer;
}

SceneFramebuffer& Application::GetFramebuffer(Window& window)
{
    return this->Find(window).framebuffer;
}

ResourceManager& Application::GetResources() noexcept
{
    return this->resources;
}

const ResourceManager& Application::GetResources() const noexcept
{
    return this->resources;
}

Application::ManagedWindow& Application::Find(Window& window)
{
    const auto iterator = std::find_if(
        this->windows.begin(),
        this->windows.end(),
        [&window](const std::unique_ptr<ManagedWindow>& managed)
        {
            return managed->window.get() == &window;
        }
    );
    if (iterator != this->windows.end())
        return **iterator;

    const auto pending = std::find_if(
        this->pendingWindows.begin(),
        this->pendingWindows.end(),
        [&window](const std::unique_ptr<ManagedWindow>& managed)
        {
            return managed->window.get() == &window;
        }
    );
    if (pending != this->pendingWindows.end())
        return **pending;
    throw std::invalid_argument("Window is not managed by this Application");
}

const Application::ManagedWindow& Application::Find(
    const Window& window
) const
{
    const auto findWindow = [&window](
        const std::unique_ptr<ManagedWindow>& managed
    ) {
        return managed->window.get() == &window;
    };
    const auto active = std::find_if(
        this->windows.begin(),
        this->windows.end(),
        findWindow
    );
    if (active != this->windows.end())
        return **active;

    const auto pending = std::find_if(
        this->pendingWindows.begin(),
        this->pendingWindows.end(),
        findWindow
    );
    if (pending != this->pendingWindows.end())
        return **pending;
    throw std::invalid_argument("Window is not managed by this Application");
}

void Application::RenderWindow(ManagedWindow& managedWindow)
{
    Window& window = *managedWindow.window;
    if (window.ShouldClose())
        return;

    window.MakeContextCurrent();
    const WindowSize framebufferSize = window.GetFramebufferSize();
    if (framebufferSize.width <= 0 || framebufferSize.height <= 0)
        return;

    managedWindow.framebuffer.Resize(
        framebufferSize.width,
        framebufferSize.height
    );
    const float aspect =
        static_cast<float>(framebufferSize.width) /
        static_cast<float>(framebufferSize.height);
    const glm::mat4 projection = window.Projection(aspect);
    managedWindow.framebuffer.Clear(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
    managedWindow.renderer.Render(
        window.GetScene(),
        window.GetCamera(),
        projection,
        managedWindow.framebuffer
    );
    managedWindow.renderer.Present(
        managedWindow.framebuffer,
        framebufferSize.width,
        framebufferSize.height
    );
    window.SwapBuffers();
}

void Application::CommitPendingWindows()
{
    for (std::unique_ptr<ManagedWindow>& managed : this->pendingWindows)
        this->windows.push_back(std::move(managed));
    this->pendingWindows.clear();
}

void Application::DestroyClosedWindows()
{
    auto iterator = this->windows.begin();
    while (iterator != this->windows.end())
    {
        ManagedWindow& managed = **iterator;
        if (!managed.window->ShouldClose())
        {
            ++iterator;
            continue;
        }

        managed.window->MakeContextCurrent();
        iterator = this->windows.erase(iterator);
    }
}

void Application::TrackScene(const std::shared_ptr<Scene>& scene)
{
    if (scene == nullptr)
        throw std::invalid_argument("Application cannot track a null Scene");

    this->trackedScenes.erase(
        std::remove_if(
            this->trackedScenes.begin(),
            this->trackedScenes.end(),
            [](const std::weak_ptr<Scene>& tracked)
            {
                return tracked.expired();
            }
        ),
        this->trackedScenes.end()
    );

    const bool alreadyTracked = std::any_of(
        this->trackedScenes.begin(),
        this->trackedScenes.end(),
        [&scene](const std::weak_ptr<Scene>& tracked)
        {
            const std::shared_ptr<Scene> existing = tracked.lock();
            return existing != nullptr && existing.get() == scene.get();
        }
    );
    if (!alreadyTracked)
        this->trackedScenes.emplace_back(scene);
}

void Application::ClearTrackedScenes() noexcept
{
    std::unordered_set<Scene*> cleared;
    for (const std::weak_ptr<Scene>& tracked : this->trackedScenes)
    {
        const std::shared_ptr<Scene> scene = tracked.lock();
        if (scene != nullptr && cleared.insert(scene.get()).second)
            scene->Clear();
    }
    this->trackedScenes.clear();
}

void Application::RequireOwnerThread() const
{
    if (std::this_thread::get_id() != this->ownerThread)
    {
        throw std::logic_error(
            "Application window operations must run on the GLFW owner thread"
        );
    }
}

void Application::Shutdown() noexcept
{
    this->running = false;
    this->closeRequested = true;

    // Pending windows own valid contexts too; include them in the same orderly
    // teardown without allocating memory from this noexcept function.
    const auto releaseLocalResources = [](auto& managedWindows)
    {
        for (const std::unique_ptr<ManagedWindow>& managed : managedWindows)
        {
            if (managed->window != nullptr &&
                managed->window->GetGLFWwindow() != nullptr)
            {
                managed->window->MakeContextCurrent();
                managed->framebuffer.Release();
                managed->renderer.Release();
                managed->window->DisableFreeCameraController();
            }
        }
    };

    // First release every context-local object while its own context is current.
    // Scenes must also stop referring to shared resources before those resources
    // are destroyed.
    releaseLocalResources(this->windows);
    releaseLocalResources(this->pendingWindows);
    this->ClearTrackedScenes();

    // Texture, buffer, shader and program objects are shared, but deleting them
    // still requires one living context from the share group.
    ManagedWindow* resourceContext = !this->windows.empty()
        ? this->windows.front().get()
        : (!this->pendingWindows.empty()
            ? this->pendingWindows.front().get()
            : nullptr);
    if (resourceContext != nullptr &&
        resourceContext->window != nullptr &&
        resourceContext->window->GetGLFWwindow() != nullptr)
    {
        resourceContext->window->MakeContextCurrent();
        this->resources.Clear();
    }

    while (!this->pendingWindows.empty())
    {
        std::unique_ptr<ManagedWindow> managed =
            std::move(this->pendingWindows.back());
        this->pendingWindows.pop_back();
        if (managed->window != nullptr &&
            managed->window->GetGLFWwindow() != nullptr)
        {
            glfwMakeContextCurrent(managed->window->GetGLFWwindow());
        }
        managed.reset();
    }

    while (!this->windows.empty())
    {
        std::unique_ptr<ManagedWindow> managed =
            std::move(this->windows.back());
        this->windows.pop_back();
        if (managed->window != nullptr &&
            managed->window->GetGLFWwindow() != nullptr)
        {
            glfwMakeContextCurrent(managed->window->GetGLFWwindow());
        }
        managed.reset();
    }

    if (this->glfwInitialized)
    {
        glfwMakeContextCurrent(nullptr);
        glfwTerminate();
        this->glfwInitialized = false;
    }
}
