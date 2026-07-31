#pragma once
#include "timer.hpp"
#include "Models/cube.hpp"
#include "mesh.hpp"
#include "material.hpp"
#include "scene.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>

struct WindowSize {
        int width  = 640, height = 480;
};

class Window {
public:
    Window(int width = 640, int height = 480);
    ~Window();

    bool Run();
    inline GLFWwindow* GetGLFWwindow() const { return window; };
    inline const WindowSize GetSize() const { return *size; };
    glm::mat4 Projection() const{ return this->projection; };
    Scene& GetScene() noexcept { return this->scene; };
    const Scene& GetScene() const noexcept { return this->scene; };

private:
    void init();
    void computeParam();
private:
    WindowSize* size = nullptr;  
    GLFWwindow* window = nullptr;
    Timer* timer = nullptr;
    Model* model = nullptr;
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    Scene scene;
    Renderer renderer;
    float aspect = 1.0f;
    glm::mat4 projection = glm::mat4(1.0f);
};
