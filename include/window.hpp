#pragma once
#include "behaviour.hpp"
#include "input.hpp"
#include "timer.hpp"
#include "manager.hpp"
#include "scene.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <memory>

struct WindowSize {
        int width  = 640, height = 480;
};

class Window {
public:
    Window(int width = 640, int height = 480);
    ~Window();

    bool Run();
    inline GLFWwindow* GetGLFWwindow() const { return window; };
    const WindowSize& GetSize() const noexcept { return this->size; };
    glm::mat4 Projection() const{ return this->projection; };
    ResourceManager& GetResources() noexcept { return this->resources; };
    const ResourceManager& GetResources() const noexcept { return this->resources; };
    Scene& GetScene() noexcept { return this->scene; };
    const Scene& GetScene() const noexcept { return this->scene; };
    Input& GetInput() noexcept { return this->input; };
    const Input& GetInput() const noexcept { return this->input; };

private:
    void init();
    void computeParam();
private:
    WindowSize size;
    GLFWwindow* window = nullptr;
    Input input;
    Timer timer;
    ResourceManager resources;
    Scene scene;
    Renderer renderer;
    std::unique_ptr<FreeCameraControllerBehaviour> cameraController;
    float aspect = 1.0f;
    glm::mat4 projection = glm::mat4(1.0f);
};
