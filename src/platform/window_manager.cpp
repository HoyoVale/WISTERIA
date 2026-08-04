#include "wisteria/common/pch.hpp"
#include "wisteria/platform/window_manager.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
void SaveWindowScreenshotBmp(
    const std::string& directory,
    std::size_t frameIndex,
    int width,
    int height
)
{
    if (width <= 0 || height <= 0)
        return;

    std::vector<unsigned char> rgba(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U
    );
    glReadPixels(
        0,
        0,
        width,
        height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba.data()
    );

    const int rowSize = ((width * 3 + 3) / 4) * 4;
    const int dataSize = rowSize * height;
    const int fileSize = 54 + dataSize;
    char fileName[64];
    std::snprintf(
        fileName,
        sizeof(fileName),
        "frame_%05zu.bmp",
        frameIndex
    );
    std::ofstream output(
        (std::filesystem::path(directory) / fileName).string(),
        std::ios::binary
    );
    if (!output)
        return;

    const auto put32 = [&output](int value)
    {
        const unsigned char bytes[4] = {
            static_cast<unsigned char>(value & 0xFF),
            static_cast<unsigned char>((value >> 8) & 0xFF),
            static_cast<unsigned char>((value >> 16) & 0xFF),
            static_cast<unsigned char>((value >> 24) & 0xFF)
        };
        output.write(
            reinterpret_cast<const char*>(bytes),
            sizeof(bytes)
        );
    };
    const auto put16 = [&output](int value)
    {
        const unsigned char bytes[2] = {
            static_cast<unsigned char>(value & 0xFF),
            static_cast<unsigned char>((value >> 8) & 0xFF)
        };
        output.write(
            reinterpret_cast<const char*>(bytes),
            sizeof(bytes)
        );
    };

    output.put('B');
    output.put('M');
    put32(fileSize);
    put16(0);
    put16(0);
    put32(54);
    put32(40);
    put32(width);
    put32(height);
    put16(1);
    put16(24);
    put32(0);
    put32(dataSize);
    put32(2835);
    put32(2835);
    put32(0);
    put32(0);

    std::vector<unsigned char> row(static_cast<std::size_t>(rowSize), 0U);
    for (int y = height - 1; y >= 0; --y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t pixel =
                (static_cast<std::size_t>(y) * width +
                 static_cast<std::size_t>(x)) * 4U;
            row[static_cast<std::size_t>(x) * 3U] = rgba[pixel + 2U];
            row[static_cast<std::size_t>(x) * 3U + 1U] = rgba[pixel + 1U];
            row[static_cast<std::size_t>(x) * 3U + 2U] = rgba[pixel];
        }
        output.write(
            reinterpret_cast<const char*>(row.data()),
            row.size()
        );
    }
}
}

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

void WindowManager::SetFreeCameraControllerSettings(
    Window& window,
    const FreeCameraControllerSettings& settings
)
{
    this->RequireOwnerThread();
    this->Find(window);
    window.SetFreeCameraControllerSettings(settings);
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

    const bool frameProfile =
        std::getenv("WISTERIA_FRAME_PROFILE") != nullptr;
    const auto profileStart = std::chrono::steady_clock::now();

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
    const auto afterClear = std::chrono::steady_clock::now();
    managedWindow.renderer.Render(
        window.GetScene(),
        window.GetCamera(),
        projection,
        managedWindow.framebuffer
    );
    const auto afterRender = std::chrono::steady_clock::now();
    managedWindow.renderer.Present(
        managedWindow.framebuffer,
        framebufferSize.width,
        framebufferSize.height
    );
    const auto afterPresent = std::chrono::steady_clock::now();
    if (const char* screenshotDirectory =
            std::getenv("WISTERIA_SCREENSHOT_DIR"))
    {
        static std::size_t screenshotFrame = 0U;
        ++screenshotFrame;
        if (screenshotFrame % 30U == 1U)
        {
            SaveWindowScreenshotBmp(
                screenshotDirectory,
                screenshotFrame,
                framebufferSize.width,
                framebufferSize.height
            );
        }
    }
    window.SwapBuffers();
    const auto afterSwap = std::chrono::steady_clock::now();

    if (frameProfile)
    {
        static std::size_t profileFrame = 0U;
        static double totalUpdateMs = 0.0;
        static double totalRenderMs = 0.0;
        static double totalPresentMs = 0.0;
        static double totalSwapMs = 0.0;
        const auto millis = [](const auto& start, const auto& end)
        {
            return std::chrono::duration<double, std::milli>(end - start)
                .count();
        };
        totalUpdateMs += millis(profileStart, afterClear);
        totalRenderMs += millis(afterClear, afterRender);
        totalPresentMs += millis(afterRender, afterPresent);
        totalSwapMs += millis(afterPresent, afterSwap);
        ++profileFrame;
        if (profileFrame % 60U == 1U)
        {
            const double frames = static_cast<double>(profileFrame);
            std::cout << "[FRAME PROFILE] frames=" << profileFrame
                      << " updateMs=" << (totalUpdateMs / frames)
                      << " renderMs=" << (totalRenderMs / frames)
                      << " presentMs=" << (totalPresentMs / frames)
                      << " swapMs=" << (totalSwapMs / frames)
                      << " totalMs="
                      << ((totalUpdateMs + totalRenderMs +
                           totalPresentMs + totalSwapMs) / frames)
                      << std::endl;
        }
    }
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
