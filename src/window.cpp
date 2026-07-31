#include "pch.hpp"
#include "window.hpp"
#include "shader.hpp"
#include "texture.hpp"
#include <iostream>
#include <glad/gl.h>

void FramebufferSizeCallback(GLFWwindow* window,int width,int height)
{
    glViewport(0, 0, width, height);
}

Window::Window(int width, int height)
{
    this->size = new WindowSize({width, height});
    this->camera = new Camera();
    this->timer = new Timer();
    this->model = new Cube();
    this->mesh = new Mesh(this->model->Data());
    this->material = new Material();
    this->entity = new Entity(*this->mesh, *this->material);
    this->light = new PointLight({
        .Position = {2.0f, 2.0f, 2.0f},
        .Color = {1.0f, 0.85f, 0.65f},
        .Intensity = 1.2f,
        .Range = 10.0f
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
    delete this->size;
    delete this->camera;
    delete this->timer;
    delete this->entity;
    delete this->light;
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
    this->entity->GetMesh().Attach();
    this->entity->GetMaterial().Attach();

    float r = 0.0f, speed = 9.0f;
    
    this->timer->Start();
    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        this->timer->Now();
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        this->computeParam();
        this->entity->GetTransform().SetRotation({r, 2 * r, 3 * r});
        const glm::mat4 model = this->entity->GetTransform().Matrix();
        const glm::vec3 lightRadiance = this->light->Radiance();
        r += speed * this->timer->GetDeltaTime();
        if(r<=-180 or r>=180) speed *= -1.0f;
        this->entity->GetMaterial().Bind();
        Program& program = this->entity->GetMaterial().GetProgram();
        program.UniformMat4f("model", model);
        program.UniformMat4f("view", this->View());
        program.UniformMat4f("projection", this->Projection());
        program.Uniform3f(
            "lightPosition",
            this->light->Position().x,
            this->light->Position().y,
            this->light->Position().z
        );
        program.Uniform3f(
            "lightRadiance",
            lightRadiance.x,
            lightRadiance.y,
            lightRadiance.z
        );
        program.Uniform1f("ambientStrength", 0.15f);
        program.Uniform3f(
            "cameraPosition",
            this->camera->GetParam().Position.x,
            this->camera->GetParam().Position.y,
            this->camera->GetParam().Position.z
        );
        program.Uniform3f(
            "materialSpecularColor",
            this->material->SpecularColor().x,
            this->material->SpecularColor().y,
            this->material->SpecularColor().z
        );
        program.Uniform1f(
            "materialShininess",
            this->material->Shininess()
        );
        
        // 绘制
        this->entity->GetMesh().Bind();
        this->entity->GetMesh().Draw();
        this->entity->GetMesh().Unbind();
        this->entity->GetMaterial().Unbind();

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    
    return false;
}
