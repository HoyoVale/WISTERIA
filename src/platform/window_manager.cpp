#include "wisteria/common/pch.hpp"
#include "wisteria/platform/window_manager.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string_view>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace wisteria
{
namespace
{
std::string SafeFileStem(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text)
    {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_')
        {
            result.push_back(static_cast<char>(character));
        }
        else if (character == ' ')
        {
            result.push_back('_');
        }
    }
    if (result.empty())
        result = "window";
    return result;
}

void ReportGlErrors(
    const char* stage,
    std::size_t frameIndex,
    std::string_view windowTitle
)
{
    if (std::getenv("WISTERIA_GL_DIAGNOSTICS") == nullptr)
        return;

    bool found = false;
    for (int index = 0; index < 32; ++index)
    {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR)
            break;
        found = true;
        std::cerr << "[GL ERROR] frame=" << frameIndex
                  << " stage=" << stage
                  << " window=\"" << windowTitle << "\""
                  << " code=0x" << std::hex << std::uppercase << error
                  << std::dec << std::nouppercase << std::endl;
    }
    if (!found && (frameIndex <= 3U || frameIndex % 60U == 0U))
    {
        std::cout << "[GL CHECK] frame=" << frameIndex
                  << " stage=" << stage
                  << " status=OK" << std::endl;
    }
}

void ReportScenePixel(
    const char* stage,
    std::size_t frameIndex,
    const SceneFramebuffer& framebuffer
)
{
    if (std::getenv("WISTERIA_GL_PIXEL_PROBE") == nullptr ||
        !framebuffer.IsValid() ||
        !(frameIndex <= 3U || frameIndex % 30U == 1U))
    {
        return;
    }

    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = GL_BACK;
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer.Id());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    unsigned char rgba[4] = {0U, 0U, 0U, 0U};
    glReadPixels(
        framebuffer.Width() / 2,
        framebuffer.Height() / 2,
        1,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba
    );

    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previousReadFramebuffer)
    );
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    std::cout << "[GL PIXEL] frame=" << frameIndex
              << " stage=" << stage
              << " center="
              << static_cast<unsigned int>(rgba[0]) << ","
              << static_cast<unsigned int>(rgba[1]) << ","
              << static_cast<unsigned int>(rgba[2]) << ","
              << static_cast<unsigned int>(rgba[3])
              << " fbo=" << framebuffer.Id()
              << " colorTexture=" << framebuffer.ColorTexture()
              << std::endl;
}

void SaveWindowScreenshotBmp(
    const std::string& directory,
    std::string_view windowTitle,
    std::size_t frameIndex,
    int width,
    int height,
    GLuint readFramebuffer,
    GLenum readBuffer,
    std::string_view sourceName
)
{
    if (width <= 0 || height <= 0)
        return;

    std::error_code directoryError;
    std::filesystem::create_directories(directory, directoryError);
    if (directoryError)
    {
        std::cerr << "[FRAME CAPTURE] cannot create directory: "
                  << directoryError.message() << std::endl;
        return;
    }

    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = GL_BACK;
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glReadBuffer(readBuffer);

    std::vector<unsigned char> rgba(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U
    );
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0,
        0,
        width,
        height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba.data()
    );

    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previousReadFramebuffer)
    );
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    std::uint64_t hash = 1469598103934665603ULL;
    unsigned int minimumRgb = 255U;
    unsigned int maximumRgb = 0U;
    for (std::size_t index = 0; index < rgba.size(); index += 4U)
    {
        for (std::size_t component = 0; component < 3U; ++component)
        {
            const unsigned int value = rgba[index + component];
            minimumRgb = std::min(minimumRgb, value);
            maximumRgb = std::max(maximumRgb, value);
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ULL;
        }
    }

    const int rowSize = ((width * 3 + 3) / 4) * 4;
    const int dataSize = rowSize * height;
    const int fileSize = 54 + dataSize;
    char fileName[160];
    std::snprintf(
        fileName,
        sizeof(fileName),
        "%s_frame_%05zu.bmp",
        SafeFileStem(windowTitle).c_str(),
        frameIndex
    );
    const std::filesystem::path outputPath =
        std::filesystem::path(directory) / fileName;
    std::ofstream output(outputPath, std::ios::binary);
    if (!output)
    {
        std::cerr << "[FRAME CAPTURE] cannot open "
                  << outputPath.string() << std::endl;
        return;
    }

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
    output.close();

    std::cout << "[FRAME CAPTURE] frame=" << frameIndex
              << " source=" << sourceName
              << " readFbo=" << readFramebuffer
              << " readBuffer=0x" << std::hex << readBuffer << std::dec
              << " size=" << width << "x" << height
              << " rgbRange=" << minimumRgb << ".." << maximumRgb
              << " fnv1a=0x" << std::hex << hash << std::dec
              << " file=" << outputPath.string() << std::endl;
}
}

WindowManager::ManagedWindow::ManagedWindow(
    std::unique_ptr<Window> nextWindow,
    std::string nextCaptureStem
)
    : window(std::move(nextWindow)),
      captureStem(std::move(nextCaptureStem))
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
    auto managed = std::make_unique<ManagedWindow>(
        std::move(window),
        config.title
    );
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

void WindowManager::UpdateWindowControllers(float deltaTime)
{
    for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
    {
        if (!managed->window->ShouldClose())
            managed->window->Update(deltaTime);
    }
}

std::vector<Scene*> WindowManager::UniqueScenes() const
{
    std::unordered_set<Scene*> scheduledScenes;
    std::vector<Scene*> scenes;
    scenes.reserve(this->windows.size());
    for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
    {
        if (managed->window->ShouldClose())
            continue;
        Scene* scene = managed->window->GetSceneHandle().get();
        if (scheduledScenes.insert(scene).second)
            scenes.push_back(scene);
    }
    return scenes;
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

    ++managedWindow.renderedFrames;
    const std::size_t frameIndex = managedWindow.renderedFrames;
    const bool frameProfile =
        std::getenv("WISTERIA_FRAME_PROFILE") != nullptr;
    const auto profileStart = std::chrono::steady_clock::now();

    window.MakeContextCurrent();
    ReportGlErrors("frame-begin", frameIndex, window.Title());
    const WindowSize framebufferSize = window.GetFramebufferSize();
    if (framebufferSize.width <= 0 || framebufferSize.height <= 0)
    {
        if (frameIndex <= 3U || frameIndex % 60U == 0U)
        {
            std::cout << "[FRAME SKIP] frame=" << frameIndex
                      << " framebuffer=" << framebufferSize.width << "x"
                      << framebufferSize.height << std::endl;
        }
        return;
    }

    managedWindow.framebuffer.Resize(
        framebufferSize.width,
        framebufferSize.height
    );
    const float aspect =
        static_cast<float>(framebufferSize.width) /
        static_cast<float>(framebufferSize.height);
    const glm::mat4 projection = window.Projection(aspect);
    managedWindow.framebuffer.Clear(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
    ReportGlErrors("clear", frameIndex, window.Title());
    ReportScenePixel("after-clear", frameIndex, managedWindow.framebuffer);
    const auto afterClear = std::chrono::steady_clock::now();
    managedWindow.renderer.Render(
        window.GetScene(),
        window.GetCamera(),
        projection,
        managedWindow.framebuffer
    );
    ReportGlErrors("render", frameIndex, window.Title());
    ReportScenePixel("after-render", frameIndex, managedWindow.framebuffer);
    const auto afterRender = std::chrono::steady_clock::now();

    const char* screenshotDirectory = std::getenv("WISTERIA_SCREENSHOT_DIR");
    const std::string_view screenshotSource = []() -> std::string_view
    {
        const char* configured = std::getenv("WISTERIA_SCREENSHOT_SOURCE");
        return configured != nullptr ? std::string_view(configured) : "scene";
    }();
    std::size_t screenshotInterval = 30U;
    if (const char* configuredInterval =
            std::getenv("WISTERIA_SCREENSHOT_INTERVAL"))
    {
        try
        {
            screenshotInterval = std::max<std::size_t>(
                1U,
                std::stoull(configuredInterval)
            );
        }
        catch (...)
        {
            screenshotInterval = 30U;
        }
    }
    const bool captureFrame = screenshotDirectory != nullptr &&
        (frameIndex == 1U ||
         (frameIndex - 1U) % screenshotInterval == 0U);

    // The scene framebuffer is the authoritative renderer output. Reading it
    // avoids touching the platform swapchain. This matters on WSLg/Mesa D3D12,
    // where glReadPixels from the default back buffer can make later frames
    // appear black even though scene update and rendering continue normally.
    if (captureFrame && screenshotSource != "default")
    {
        SaveWindowScreenshotBmp(
            screenshotDirectory,
            managedWindow.captureStem,
            frameIndex,
            framebufferSize.width,
            framebufferSize.height,
            managedWindow.framebuffer.Id(),
            GL_COLOR_ATTACHMENT0,
            "scene"
        );
        ReportGlErrors("capture-scene", frameIndex, window.Title());
    }

    managedWindow.renderer.Present(
        managedWindow.framebuffer,
        framebufferSize.width,
        framebufferSize.height
    );
    ReportGlErrors("present", frameIndex, window.Title());
    const auto afterPresent = std::chrono::steady_clock::now();

    // Keep the old default-framebuffer readback as an explicit diagnostic. It
    // is intentionally not the default acceptance path because it interacts
    // with the platform compositor and driver swapchain.
    if (captureFrame && screenshotSource == "default")
    {
        GLboolean doubleBuffered = GL_TRUE;
        glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
        SaveWindowScreenshotBmp(
            screenshotDirectory,
            managedWindow.captureStem,
            frameIndex,
            framebufferSize.width,
            framebufferSize.height,
            0,
            doubleBuffered == GL_TRUE ? GL_BACK : GL_FRONT,
            "default"
        );
        ReportGlErrors("capture-default", frameIndex, window.Title());
    }
    window.SwapBuffers();
    ReportGlErrors("swap", frameIndex, window.Title());
    const auto afterSwap = std::chrono::steady_clock::now();

    if (frameProfile)
    {
        const auto millis = [](const auto& start, const auto& end)
        {
            return std::chrono::duration<double, std::milli>(end - start)
                .count();
        };
        managedWindow.profileUpdateMilliseconds +=
            millis(profileStart, afterClear);
        managedWindow.profileRenderMilliseconds +=
            millis(afterClear, afterRender);
        managedWindow.profilePresentMilliseconds +=
            millis(afterRender, afterPresent);
        managedWindow.profileSwapMilliseconds +=
            millis(afterPresent, afterSwap);
        if (frameIndex <= 3U || frameIndex % 60U == 0U)
        {
            const double frames = static_cast<double>(frameIndex);
            std::cout << "[FRAME PROFILE] window=\"" << window.Title()
                      << "\" frames=" << frameIndex
                      << " updateMs="
                      << (managedWindow.profileUpdateMilliseconds / frames)
                      << " renderMs="
                      << (managedWindow.profileRenderMilliseconds / frames)
                      << " presentMs="
                      << (managedWindow.profilePresentMilliseconds / frames)
                      << " swapMs="
                      << (managedWindow.profileSwapMilliseconds / frames)
                      << " totalMs="
                      << ((managedWindow.profileUpdateMilliseconds +
                           managedWindow.profileRenderMilliseconds +
                           managedWindow.profilePresentMilliseconds +
                           managedWindow.profileSwapMilliseconds) / frames)
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
}  // namespace wisteria
