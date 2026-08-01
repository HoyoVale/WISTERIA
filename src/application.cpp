#include "pch.hpp"
#include "application.hpp"
#include "window.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <stdexcept>
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
    if (this->running)
        throw std::logic_error("CreateWindow is not supported during Run");
    if (config.width <= 0 || config.height <= 0)
        throw std::invalid_argument("Window dimensions must be positive");
    if (config.title.empty())
        throw std::invalid_argument("Window title cannot be empty");

    GLFWwindow* sharedContext = nullptr;
    if (config.shareOpenGlResources && !this->windows.empty())
        sharedContext = this->windows.front()->window->GetGLFWwindow();

    auto window = std::unique_ptr<Window>(new Window(
        config.width,
        config.height,
        config.title,
        sharedContext
    ));
    Window& result = *window;
    this->windows.push_back(
        std::make_unique<ManagedWindow>(std::move(window))
    );
    return result;
}

int Application::Run()
{
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
            for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
                managed->window->BeginInputFrame();

            glfwPollEvents();
            this->timer.Now();
            this->DestroyClosedWindows();

            const float deltaTime = this->timer.GetDeltaTime();
            for (const std::unique_ptr<ManagedWindow>& managed : this->windows)
                this->RenderWindow(*managed, deltaTime);
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
    return this->windows.size();
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
    if (iterator == this->windows.end())
        throw std::invalid_argument("Window is not managed by this Application");
    return **iterator;
}

void Application::RenderWindow(
    ManagedWindow& managedWindow,
    float deltaTime
)
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
    window.Update(deltaTime);

    const float aspect =
        static_cast<float>(framebufferSize.width) /
        static_cast<float>(framebufferSize.height);
    const glm::mat4 projection = window.Projection(aspect);
    managedWindow.framebuffer.Clear(glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
    managedWindow.renderer.Render(
        window.GetScene(),
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

void Application::Shutdown() noexcept
{
    this->running = false;
    this->closeRequested = true;

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
