#pragma once
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

private:
    WindowSize* size = nullptr;  
    GLFWwindow* window = nullptr;
};