#include "pch.hpp"
#include "window.hpp"
#include <iostream>
#include <glad/gl.h>

void FramebufferSizeCallback(GLFWwindow* window,int width,int height)
{
    glViewport(0, 0, width, height);
}

Window::Window(int width, int height)
{
    this->size = new WindowSize({width, height});
    this->timer = new Timer();
    this->model = new Cube();
    this->mesh = new Mesh(this->model->Data());
    this->material = new Material();
    this->scene.CreateEntity(*this->mesh, *this->material);
    this->scene.CreatePointLight(PointLightData{
        .Position = {2.5f, 1.5f, 2.5f},
        .Color = {1.0f, 0.65f, 0.4f},
        .Intensity = 1.6f,
        .Range = 8.0f
    });
    this->scene.CreateDirectionalLight(DirectionalLightData{
        .Direction = {-0.2f, -1.0f, -0.3f},
        .Color = {1.0f, 0.92f, 0.8f},
        .Intensity = 0.35f
    });
    this->scene.CreateSpotLight(SpotLightData{
        .Position = {2.5f, 2.5f, 3.0f},
        .Direction = {-0.55f, -0.55f, -0.65f},
        .Color = {1.0f, 0.35f, 0.65f},
        .Intensity = 2.0f,
        .Range = 8.0f,
        .InnerCutoffDegrees = 12.5f,
        .OuterCutoffDegrees = 22.0f
    });
    
    if(!glfwInit())
        std::cerr << "[ERROR]GLFW initialization failed!" << std::endl;
    window = glfwCreateWindow(
        this->size->width,
        this->size->height,
        "FLORAL WISTERIA",
        NULL,
        NULL
    );

    if(!window)
    {
        glfwTerminate();
        std::cerr << "[ERROR]Window initialization failed!" << std::endl;
    }

    this->init();
}

Window::~Window(){
    this->scene.ClearEntities();
    this->scene.ClearPointLights();
    this->scene.ClearDirectionalLights();
    this->scene.ClearSpotLights();
    delete this->size;
    delete this->timer;
    delete this->material;
    delete this->mesh;
    delete this->model;
    glfwDestroyWindow(this->window);
    glfwTerminate();
}

void Window::init()
{
    glfwMakeContextCurrent(this->GetGLFWwindow());

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cerr << "Failed to load OpenGL functions\n";
        glfwTerminate();
    }

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
    this->aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    this->projection = glm::perspective(glm::radians(45.0f),this->aspect, 0.1f, 1000.0f);
}

bool Window::Run()
{
    if (this->scene.Entities().empty())
        throw std::logic_error("Window requires at least one Scene entity");

    Entity& entity = *this->scene.Entities().front();

    float r = 0.0f, speed = 9.0f;
    
    this->timer->Start();
    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        this->timer->Now();
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        this->computeParam();
        entity.GetTransform().SetRotation({r, 2 * r, 3 * r});
        r += speed * this->timer->GetDeltaTime();
        if(r<=-180 or r>=180) speed *= -1.0f;
        this->renderer.Render(this->scene, this->Projection());

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    
    return false;
}
