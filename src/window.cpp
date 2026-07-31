#include "pch.hpp"
#include "window.hpp"
#include <filesystem>
#include <iostream>
#include <glad/gl.h>

namespace
{
std::filesystem::path DemoModelPath()
{
    return std::filesystem::current_path() /
        "tests" / "assets" / "models" / u8"仪玄" / u8"仪玄.glb";
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
            .Position = {0.0f, 0.4f, 4.0f},
            .Target = {0.0f, 0.4f, 0.0f},
            .Up = {0.0f, 1.0f, 0.0f}
        });
        this->scene.CreatePointLight(PointLightData{
            .Position = {2.5f, 1.5f, 2.5f},
            .Color = {1.0f, 1.0f, 1.0f},
            .Intensity = 1.6f,
            .Range = 8.0f
        });
        // this->scene.CreateDirectionalLight(DirectionalLightData{
        //     .Direction = {-0.2f, -1.0f, -0.3f},
        //     .Color = {1.0f, 0.92f, 0.8f},
        //     .Intensity = 0.35f
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
        if (this->window != nullptr)
            glfwDestroyWindow(this->window);
        glfwTerminate();
        throw;
    }
}

Window::~Window(){
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
    this->projection = glm::perspective(glm::radians(45.0f),this->aspect, 0.1f, 1000.0f);
}

bool Window::Run()
{
    if (this->scene.EntityCount() == 0)
        throw std::logic_error("Window requires at least one Scene entity");

    this->timer.Start();
    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        this->timer.Now();
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        this->computeParam();
        this->scene.Update(this->timer.GetDeltaTime());
        this->renderer.Render(this->scene, this->Projection());

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    
    return false;
}
