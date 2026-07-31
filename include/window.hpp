#pragma once
#include "camera.hpp"
#include "timer.hpp"
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
    glm::mat4 View() const { return this->camera->GetView(); };
    glm::mat4 Projection() const{ return this->projection; };

private:
    bool init();
    void computeParam();
private:
    WindowSize* size = nullptr;  
    GLFWwindow* window = nullptr;
    Camera* camera = nullptr;
    Timer* timer = nullptr;
    float aspect = 1.0f;
    glm::mat4 projection = glm::mat4(1.0f);
};
