#include "wisteria/common/pch.hpp"
#include "wisteria/platform/window.hpp"
#include <glad/gl.h>
#include <cstring>
#include <iostream>
#include <utility>

namespace wisteria
{
Window::Window(
    int width,
    int height,
    std::string title,
    GLFWwindow* sharedContext
)
    : size{width, height},
      title(std::move(title)),
      scene(std::make_shared<Scene>())
{
    this->camera = CameraHandle(this->scene, &this->scene->ActiveCamera());
    try
    {
        this->window = glfwCreateWindow(
            this->size.width,
            this->size.height,
            this->title.c_str(),
            nullptr,
            sharedContext
        );
        if (this->window == nullptr)
            throw std::runtime_error("GLFW window creation failed");

        this->init();
    }
    catch (...)
    {
        this->Release();
        throw;
    }
}

Window::~Window()
{
    this->Release();
}

void Window::Release() noexcept
{
    if (this->window != nullptr)
    {
        glfwMakeContextCurrent(this->window);
        GraphicsDevice::SetCurrentContext(this->window);
    }
    this->cameraController.reset();
    this->camera.reset();
    this->scene.reset();
    this->input.Detach();
    if (this->window != nullptr)
    {
        glfwDestroyWindow(this->window);
        this->window = nullptr;
    }
}

void Window::init()
{
    glfwMakeContextCurrent(this->GetGLFWwindow());
    GraphicsDevice::SetCurrentContext(this->GetGLFWwindow());

    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("Failed to load OpenGL functions");

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer =
        reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version =
        reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::cout << "[GL] vendor=" << (vendor != nullptr ? vendor : "?")
              << " renderer=" << (renderer != nullptr ? renderer : "?")
              << " version=" << (version != nullptr ? version : "?")
              << std::endl;
    if (renderer != nullptr &&
        (std::strstr(renderer, "llvmpipe") != nullptr ||
         std::strstr(renderer, "softpipe") != nullptr ||
         std::strstr(renderer, "swrast") != nullptr))
    {
        std::cerr << "[WARN] OpenGL is running on a software renderer ("
                  << renderer
                  << "). WSLg should provide the D3D12 hardware driver; "
                     "performance will be poor otherwise."
                  << std::endl;
    }
    // WSLg + Mesa D3D12 is a known compatibility issue: the default back
    // buffer can read back black after the first frame even though animation,
    // skinning and GL calls keep progressing. Native Linux and llvmpipe are
    // unaffected, so this is a driver/platform problem, not a WISTERIA
    // rendering failure. Give the user a concrete escape hatch instead of
    // silently running into black frames.
    if (vendor != nullptr &&
        renderer != nullptr &&
        std::strstr(vendor, "Microsoft") != nullptr &&
        std::strstr(renderer, "D3D12") != nullptr)
    {
        std::cerr << "[WSLG COMPATIBILITY WARNING] Mesa D3D12 renderer "
                     "detected (vendor=\""
                  << vendor << "\" renderer=\"" << renderer
                  << "\"). This driver may produce black frames after the "
                     "first frame. Retry with LIBGL_ALWAYS_SOFTWARE=1, or "
                     "run on native Linux."
                  << std::endl;
    }

    this->input.Attach(*this->window);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    //glCullFace(GL_BACK); 

    // GLint depthBits = 0;
    // glGetIntegerv(GL_DEPTH_BITS, &depthBits);
    // std::cout << "Depth bits: "<< depthBits<< '\n';
}

WindowSize Window::GetFramebufferSize() const noexcept
{
    WindowSize result{0, 0};
    if (this->window != nullptr)
        glfwGetFramebufferSize(this->window, &result.width, &result.height);
    return result;
}

glm::mat4 Window::Projection(float aspect) const
{
    if (aspect <= 0.0f)
        throw std::invalid_argument("Projection aspect ratio must be positive");
    return this->GetCamera().GetProjection(aspect);
}

bool Window::ShouldClose() const noexcept
{
    return this->window == nullptr || glfwWindowShouldClose(this->window);
}

void Window::MakeContextCurrent() const
{
    if (this->window == nullptr)
        throw std::logic_error("Cannot activate a destroyed window");
    glfwMakeContextCurrent(this->window);
    GraphicsDevice::SetCurrentContext(this->window);
}

void Window::SwapBuffers() const
{
    if (this->window == nullptr)
        throw std::logic_error("Cannot swap a destroyed window");
    glfwSwapBuffers(this->window);
}

const std::string& Window::Title() const noexcept
{
    return this->title;
}

void Window::SetTitle(std::string nextTitle)
{
    if (nextTitle.empty())
        throw std::invalid_argument("Window title cannot be empty");
    if (this->window == nullptr)
        throw std::logic_error("Cannot rename a destroyed window");

    this->title = std::move(nextTitle);
    glfwSetWindowTitle(this->window, this->title.c_str());
}

void Window::BeginInputFrame() noexcept
{
    this->input.BeginFrame();
}

void Window::Update(float deltaTime)
{
    if (this->cameraController != nullptr)
        this->cameraController->Update(deltaTime);
}

void Window::EnableFreeCameraController(
    const FreeCameraControllerSettings& settings
)
{
    this->cameraController =
        std::make_unique<FreeCameraControllerBehaviour>(
            this->GetCamera(),
            this->input,
            settings
        );
}

void Window::DisableFreeCameraController() noexcept
{
    this->cameraController.reset();
}

const FreeCameraControllerSettings&
Window::GetFreeCameraControllerSettings() const noexcept
{
    static const FreeCameraControllerSettings fallback;
    return this->cameraController != nullptr
        ? this->cameraController->Settings()
        : fallback;
}

void Window::SetFreeCameraControllerSettings(
    const FreeCameraControllerSettings& settings
)
{
    if (this->cameraController != nullptr)
        this->cameraController->SetSettings(settings);
}

void Window::BindScene(SceneHandle nextScene)
{
    if (nextScene == nullptr)
        throw std::invalid_argument("Window scene binding cannot be null");

    CameraHandle activeCamera(nextScene, &nextScene->ActiveCamera());
    this->BindRenderView(std::move(nextScene), std::move(activeCamera));
}

void Window::BindCamera(CameraHandle nextCamera)
{
    if (nextCamera == nullptr)
        throw std::invalid_argument("Window camera binding cannot be null");

    this->cameraController.reset();
    this->camera = std::move(nextCamera);
}

void Window::BindRenderView(
    SceneHandle nextScene,
    CameraHandle nextCamera
)
{
    if (nextScene == nullptr)
        throw std::invalid_argument("Window scene binding cannot be null");
    if (nextCamera == nullptr)
        throw std::invalid_argument("Window camera binding cannot be null");

    this->cameraController.reset();
    this->scene = std::move(nextScene);
    this->camera = std::move(nextCamera);
}
}  // namespace wisteria
