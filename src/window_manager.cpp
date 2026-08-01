#include "pch.hpp"
#include "window_manager.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

WindowManager::ManagedWindow::ManagedWindow(
    std::unique_ptr<Window> nextWindow
)
    : window(std::move(nextWindow))
{
    if (this->window == nullptr)
        throw std::invalid_argument("Managed window cannot be null");
}

WindowManager::WindowManager()
    : ownerThread(std::this_thread::get_id())
{
}

WindowManager::~WindowManager()
{
    this->ReleaseContextLocalResources();
    this->ClearTrackedScenes();
    this->DestroyAllWindows();
}

Window& WindowManager::CreateWindow(const WindowConfig& config)
{
    this->RequireOwnerThread();
    if (config.width <= 0 || config.height <= 0)
        throw std::invalid_argument("Window dimensions must be positive");
    if (config.title.empty())
        throw std::invalid_argument("Window title cannot be empty");
    if ((!this->windows.empty() || !this->pendingWindows.empty()) &&
        !config.shareOpenGlResources)
    {
        throw std::invalid_argument(
            "Every window must join the WindowManager OpenGL resource share group"
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

void WindowManager::DestroyWindow(Window& window)
{
    this->RequireOwnerThread();
    this->Find(window);
    if (window.GetGLFWwindow() != nullptr)
        glfwSetWindowShouldClose(window.GetGLFWwindow(), GLFW_TRUE);
}

std::shared_ptr<Scene> WindowManager::CreateScene()
{
    this->RequireOwnerThread();
    auto scene = std::make_shared<Scene>();
    this->TrackScene(scene);
    return scene;
}

std::shared_ptr<Camera> WindowManager::CreateCamera(
    const CameraParam& parameters
)
{
    return std::make_shared<Camera>(parameters);
}

void WindowManager::BindScene(
    Window& window,
    std::shared_ptr<Scene> scene
)
{
    this->RequireOwnerThread();
    this->Find(window);
    this->TrackScene(scene);
    window.BindScene(std::move(scene));
}

void WindowManager::BindCamera(
    Window& window,
    std::shared_ptr<Camera> camera
)
{
    this->RequireOwnerThread();
    this->Find(window);
    window.BindCamera(std::move(camera));
}

void WindowManager::BindRenderView(
    Window& window,
    std::shared_ptr<Scene> scene,
    std::shared_ptr<Camera> camera
)
{
    this->RequireOwnerThread();
    this->Find(window);
    this->TrackScene(scene);
    window.BindRenderView(std::move(scene), std::move(camera));
}

Scene& WindowManager::GetScene(Window& window)
{
    this->Find(window);
    return window.GetScene();
}

const Scene& WindowManager::GetScene(const Window& window) const
{
    this->Find(window);
    return window.GetScene();
}

Camera& WindowManager::GetCamera(Window& window)
{
    this->Find(window);
    return window.GetCamera();
}

const Camera& WindowManager::GetCamera(const Window& window) const
{
    this->Find(window);
    return window.GetCamera();
}

Renderer& WindowManager::GetRenderer(Window& window)
{
    return this->Find(window).renderer;
}

SceneFramebuffer& WindowManager::GetFramebuffer(Window& window)
{
    return this->Find(window).framebuffer;
}

void WindowManager::EnableFreeCameraController(
    Window& window,
    const FreeCameraControllerSettings& settings
)
{
    this->RequireOwnerThread();
    this->Find(window);
    window.EnableFreeCameraController(settings);
}

void WindowManager::DisableFreeCameraController(Window& window) noexcept
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

std::size_t WindowManager::WindowCount() const noexcept
{
    return this->windows.size() + this->pendingWindows.size();
}

WindowManager::ManagedWindow& WindowManager::Find(Window& window)
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
    throw std::invalid_argument("Window is not managed by this WindowManager");
}

const WindowManager::ManagedWindow& WindowManager::Find(
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
    throw std::invalid_argument("Window is not managed by this WindowManager");
}

void WindowManager::SetRunning(bool nextRunning) noexcept
{
    this->running = nextRunning;
}

bool WindowManager::HasActiveWindows() const noexcept
{
    return !this->windows.empty();
}

bool WindowManager::AllWindowsClosed() const noexcept
{
    return !this->windows.empty() && std::all_of(
        this->windows.begin(),
        this->windows.end(),
        [](const std::unique_ptr<ManagedWindow>& managed)
        {
            return managed->window->ShouldClose();
        }
    );
}

void WindowManager::CommitPendingWindows()
{
    for (std::unique_ptr<ManagedWindow>& managed : this->pendingWindows)
        this->windows.push_back(std::move(managed));
    this->pendingWindows.clear();
}

void WindowManager::BeginInputFrames() noexcept
{
    for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
        managed->window->BeginInputFrame();
}

void WindowManager::DestroyClosedWindows()
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

void WindowManager::Update(float deltaTime)
{
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
}

void WindowManager::RenderAll()
{
    for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
        this->RenderWindow(*managed);
}

void WindowManager::RenderWindow(ManagedWindow& managedWindow)
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

void WindowManager::TrackScene(const std::shared_ptr<Scene>& scene)
{
    if (scene == nullptr)
        throw std::invalid_argument("WindowManager cannot track a null Scene");

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

void WindowManager::ClearTrackedScenes() noexcept
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

void WindowManager::ReleaseContextLocalResources() noexcept
{
    const auto release = [](auto& managedWindows)
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
    release(this->windows);
    release(this->pendingWindows);
}

GLFWwindow* WindowManager::SharedResourceContext() const noexcept
{
    if (!this->windows.empty())
        return this->windows.front()->window->GetGLFWwindow();
    if (!this->pendingWindows.empty())
        return this->pendingWindows.front()->window->GetGLFWwindow();
    return nullptr;
}

void WindowManager::DestroyAllWindows() noexcept
{
    const auto destroy = [](auto& managedWindows)
    {
        while (!managedWindows.empty())
        {
            std::unique_ptr<ManagedWindow> managed =
                std::move(managedWindows.back());
            managedWindows.pop_back();
            if (managed->window != nullptr &&
                managed->window->GetGLFWwindow() != nullptr)
            {
                glfwMakeContextCurrent(managed->window->GetGLFWwindow());
            }
            managed.reset();
        }
    };
    destroy(this->pendingWindows);
    destroy(this->windows);
}

void WindowManager::RequireOwnerThread() const
{
    if (std::this_thread::get_id() != this->ownerThread)
    {
        throw std::logic_error(
            "WindowManager operations must run on the GLFW owner thread"
        );
    }
}
