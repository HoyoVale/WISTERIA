#include "pch.hpp"
#include "window.hpp"
#include <filesystem>
#include <iostream>
#include <glad/gl.h>
#include <utility>

namespace
{
std::filesystem::path DemoModelPath()
{
    return std::filesystem::current_path() / "assets" / "models" /
        u8"仪玄_pmx" / u8"仪玄.pmx";
}
std::filesystem::path Demo2ModelPath()
{
    return std::filesystem::current_path() / "assets" / "models" /
        u8"仪玄皮肤_pmx" / u8"仪玄.pmx";
}
}

Window::Window(
    int width,
    int height,
    std::string title,
    GLFWwindow* sharedContext
)
    : size{width, height}, title(std::move(title))
{
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

        EnvironmentMap& environment = this->resources.CreateEnvironment(
            "defaultSky",
            EnvironmentMapData::ProceduralSky()
        );
        this->scene.SetEnvironment(&environment);

        ModelAsset& yixuanModel = this->resources.LoadModel("yixuan",DemoModelPath());
        Entity& yixuanEntity = this->scene.InstantiateModel(yixuanModel,
            Transform(glm::vec3(0.8f, 0.0f, 0.0f),glm::vec3(0.0f),glm::vec3(0.1f)));
        ModelAsset& yixuan2Model = this->resources.LoadModel("yixuan2",Demo2ModelPath());
        Entity& yixuan2Entity = this->scene.InstantiateModel(yixuan2Model,
            Transform(glm::vec3(-0.8f, 0.0f, 0.0f),glm::vec3(0.0f),glm::vec3(0.1f)));
            
        yixuanEntity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f, 0.0f, 0.0f));
        yixuan2Entity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f, 0.0f, 0.0f));

        this->scene.ActiveCamera().SetParam(CameraParam{
            .Position = {0.0f, 1.1f, 3.5f},
            .Target = {0.0f, 1.1f, 0.25f},
            .Up = {0.0f, 1.0f, 0.0f}
        });
        this->cameraController =
            std::make_unique<FreeCameraControllerBehaviour>(
                this->scene.ActiveCamera(),
                this->input
            );
        this->scene.CreatePointLight(PointLightData{
            .Position = {2.5f, 1.5f, 2.5f},
            .Color = {1.0f, 1.0f, 1.0f},
            .Intensity = 1.6f,
            .Range = 8.0f
        });
        // this->scene.CreateDirectionalLight(DirectionalLightData{
        //     .Direction = {-0.2f, -1.0f, -0.3f},
        //     .Color = {1.0f, 1.0f, 1.0f},
        //     .Intensity = 2.0f
        // });
        // this->scene.CreateSpotLight(SpotLightData{
        //     .Position = {2.5f, 2.5f, 3.0f},
        //     .Direction = {-0.55f, -0.55f, -0.65f},
        //     .Color = {1.0f, 0.35f, 0.65f},
        //     .Intensity = 2.0f,
        //     .Range = 8.0f,
        //     .InnerCutoffDegrees = 12.5f,
        //     .OuterCutoffDegrees = 22.0f
        // });
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
    this->input.Detach();
    this->scene.ClearEntities();
    this->scene.ClearPointLights();
    this->scene.ClearDirectionalLights();
    this->scene.ClearSpotLights();
    this->scene.ClearEnvironment();
    this->resources.Clear();
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
    return this->scene.ActiveCamera().GetProjection(aspect);
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

void Window::BeginInputFrame() noexcept
{
    this->input.BeginFrame();
}

void Window::Update(float deltaTime)
{
    if (this->scene.EntityCount() == 0)
        throw std::logic_error("Window requires at least one Scene entity");
    if (this->cameraController != nullptr)
        this->cameraController->Update(deltaTime);
    this->scene.Update(deltaTime);
}
