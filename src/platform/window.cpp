#include "wisteria/common/pch.hpp"
#include "wisteria/platform/window.hpp"
#include <glad/gl.h>
#include <utility>

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
        glfwMakeContextCurrent(this->window);
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

    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("Failed to load OpenGL functions");

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
