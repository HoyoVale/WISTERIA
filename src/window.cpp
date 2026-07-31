#include "pch.hpp"
#include "window.hpp"
#include "shader.hpp"
#include "texture.hpp"
#include <algorithm>
#include <iostream>
#include <glad/gl.h>

namespace
{
constexpr std::size_t MaxPointLights = 8;
constexpr std::size_t MaxDirectionalLights = 4;
constexpr std::size_t MaxSpotLights = 4;
}

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
    entity.GetMesh().Attach();
    entity.GetMaterial().Attach();

    float r = 0.0f, speed = 9.0f;
    
    this->timer->Start();
    while(!glfwWindowShouldClose(this->GetGLFWwindow()))
    {
        this->timer->Now();
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        this->computeParam();
        entity.GetTransform().SetRotation({r, 2 * r, 3 * r});
        const glm::mat4 model = entity.GetTransform().Matrix();
        r += speed * this->timer->GetDeltaTime();
        if(r<=-180 or r>=180) speed *= -1.0f;
        entity.GetMaterial().Bind();
        Program& program = entity.GetMaterial().GetProgram();
        program.UniformMat4f("model", model);
        program.UniformMat4f("view", this->View());
        program.UniformMat4f("projection", this->Projection());
        const int pointLightCount = static_cast<int>(std::min(
            this->scene.PointLights().size(),
            MaxPointLights
        ));
        program.Uniform1i("pointLightCount", pointLightCount);
        for (int index = 0; index < pointLightCount; ++index)
        {
            const PointLight& pointLight = *this->scene.PointLights()[index];
            const glm::vec3 radiance = pointLight.Radiance();
            const std::string uniformPrefix =
                "pointLights[" + std::to_string(index) + "].";

            program.Uniform3f(
                uniformPrefix + "position",
                pointLight.Position().x,
                pointLight.Position().y,
                pointLight.Position().z
            );
            program.Uniform3f(
                uniformPrefix + "radiance",
                radiance.x,
                radiance.y,
                radiance.z
            );
            program.Uniform1f(uniformPrefix + "range", pointLight.Range());
            program.Uniform1f(
                uniformPrefix + "constant",
                pointLight.Constant()
            );
            program.Uniform1f(uniformPrefix + "linear", pointLight.Linear());
            program.Uniform1f(
                uniformPrefix + "quadratic",
                pointLight.Quadratic()
            );
        }
        const int directionalLightCount = static_cast<int>(std::min(
            this->scene.DirectionalLights().size(),
            MaxDirectionalLights
        ));
        program.Uniform1i("directionalLightCount", directionalLightCount);
        for (int index = 0; index < directionalLightCount; ++index)
        {
            const DirectionalLight& directionalLight =
                *this->scene.DirectionalLights()[index];
            const glm::vec3 radiance = directionalLight.Radiance();
            const std::string uniformPrefix =
                "directionalLights[" + std::to_string(index) + "].";

            program.Uniform3f(
                uniformPrefix + "direction",
                directionalLight.Direction().x,
                directionalLight.Direction().y,
                directionalLight.Direction().z
            );
            program.Uniform3f(
                uniformPrefix + "radiance",
                radiance.x,
                radiance.y,
                radiance.z
            );
        }
        const int spotLightCount = static_cast<int>(std::min(
            this->scene.SpotLights().size(),
            MaxSpotLights
        ));
        program.Uniform1i("spotLightCount", spotLightCount);
        for (int index = 0; index < spotLightCount; ++index)
        {
            const SpotLight& spotLight = *this->scene.SpotLights()[index];
            const glm::vec3 radiance = spotLight.Radiance();
            const std::string uniformPrefix =
                "spotLights[" + std::to_string(index) + "].";

            program.Uniform3f(
                uniformPrefix + "position",
                spotLight.Position().x,
                spotLight.Position().y,
                spotLight.Position().z
            );
            program.Uniform3f(
                uniformPrefix + "direction",
                spotLight.Direction().x,
                spotLight.Direction().y,
                spotLight.Direction().z
            );
            program.Uniform3f(
                uniformPrefix + "radiance",
                radiance.x,
                radiance.y,
                radiance.z
            );
            program.Uniform1f(uniformPrefix + "range", spotLight.Range());
            program.Uniform1f(
                uniformPrefix + "constant",
                spotLight.Constant()
            );
            program.Uniform1f(uniformPrefix + "linear", spotLight.Linear());
            program.Uniform1f(
                uniformPrefix + "quadratic",
                spotLight.Quadratic()
            );
            program.Uniform1f(
                uniformPrefix + "innerCutoff",
                spotLight.InnerCutoffCos()
            );
            program.Uniform1f(
                uniformPrefix + "outerCutoff",
                spotLight.OuterCutoffCos()
            );
        }
        program.Uniform1f("ambientStrength", 0.15f);
        program.Uniform3f(
            "cameraPosition",
            this->scene.ActiveCamera().GetParam().Position.x,
            this->scene.ActiveCamera().GetParam().Position.y,
            this->scene.ActiveCamera().GetParam().Position.z
        );
        program.Uniform3f(
            "materialSpecularColor",
            entity.GetMaterial().SpecularColor().x,
            entity.GetMaterial().SpecularColor().y,
            entity.GetMaterial().SpecularColor().z
        );
        program.Uniform1f(
            "materialShininess",
            entity.GetMaterial().Shininess()
        );
        
        // 绘制
        entity.GetMesh().Bind();
        entity.GetMesh().Draw();
        entity.GetMesh().Unbind();
        entity.GetMaterial().Unbind();

        glfwSwapBuffers(this->GetGLFWwindow());
        glfwPollEvents();
    }
    
    return false;
}
