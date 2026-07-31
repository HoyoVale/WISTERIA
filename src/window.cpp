#include "pch.hpp"
#include "window.hpp"
#include <filesystem>
#include <iostream>
#include <glad/gl.h>

namespace
{
std::filesystem::path DemoModelPath()
{
    return std::filesystem::current_path() / "assets" / "models" / u8"仪玄_obj" / u8"仪玄.obj";
}
}

void FramebufferSizeCallback(GLFWwindow* window,int width,int height)
{
    glViewport(0, 0, width, height);
}

Window::Window(int width, int height)
    : size{width, height}
{
    if (!glfwInit())
        throw std::runtime_error("GLFW initialization failed");

    try
    {
        this->window = glfwCreateWindow(
            this->size.width,
            this->size.height,
            "FLORAL WISTERIA",
            nullptr,
            nullptr
        );
        if (this->window == nullptr)
            throw std::runtime_error("GLFW window creation failed");

        this->init();

        ModelAsset& yixuanModel = this->resources.LoadModel("yixuan",DemoModelPath());
        Entity& yixuanEntity = this->scene.InstantiateModel(yixuanModel);
        yixuanEntity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f, 12.0f, 0.0f));

        this->scene.ActiveCamera().SetParam(CameraParam{
            .Position = {0.0f, 1.0f, 2.5f},
            .Target = {0.0f, 1.0f, 0.0f},
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
        this->input.Detach();
        if (this->window != nullptr)
            glfwDestroyWindow(this->window);
        glfwTerminate();
        throw;
    }
}

Window::~Window(){
    this->cameraController.reset();
    this->input.Detach();
    this->scene.ClearEntities();
    this->scene.ClearPointLights();
    this->scene.ClearDirectionalLights();
    this->scene.ClearSpotLights();
    this->resources.Clear();
    if (this->window != nullptr)
        glfwDestroyWindow(this->window);
    glfwTerminate();
}

void Window::init()
{
    glfwMakeContextCurrent(this->GetGLFWwindow());

    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("Failed to load OpenGL functions");

    glfwSetFramebufferSizeCallback(this->window, FramebufferSizeCallback);
    this->input.Attach(*this->window);
    this->computeParam();
    // TODO renderer init
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

void Window::computeParam()
{
    int framebufferWidth = 0;
    int framebufferHeight = 0;

    glfwGetFramebufferSize(this->window,&framebufferWidth,&framebufferHeight);
    glViewport(0, 0, framebufferWidth,framebufferHeight);

    // A minimized window may temporarily have a zero-sized framebuffer.
    // Keep the last valid projection until the framebuffer becomes drawable.
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
        return;

    this->aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    this->projection = this->scene.ActiveCamera().GetProjection(this->aspect);
}

bool Window::Run()
{
    if (this->scene.EntityCount() == 0)
        throw std::logic_error("Window requires at least one Scene entity");

    this->timer.Start();
    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        this->timer.Now();
        this->input.BeginFrame();
        glfwPollEvents();
        if (glfwWindowShouldClose(this->GetGLFWwindow()))
            break;

        if (this->cameraController != nullptr)
            this->cameraController->Update(this->timer.GetDeltaTime());

        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        this->computeParam();
        this->scene.Update(this->timer.GetDeltaTime());
        this->renderer.Render(this->scene, this->Projection());

        glfwSwapBuffers(this->GetGLFWwindow());
    }
    
    return false;
}
