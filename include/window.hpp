#pragma once
#include "behaviour.hpp"
#include "input.hpp"
#include "manager.hpp"
#include "scene.hpp"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

class Application;

struct WindowSize {
        int width  = 640, height = 480;
};

class Window {
public:
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    inline GLFWwindow* GetGLFWwindow() const { return window; };
    const WindowSize& GetSize() const noexcept { return this->size; };
    WindowSize GetFramebufferSize() const noexcept;
    glm::mat4 Projection(float aspect) const;
    bool ShouldClose() const noexcept;
    void MakeContextCurrent() const;
    void SwapBuffers() const;
    void BeginInputFrame() noexcept;
    void Update(float deltaTime);
    ResourceManager& GetResources() noexcept { return this->resources; };
    const ResourceManager& GetResources() const noexcept { return this->resources; };
    Scene& GetScene() noexcept { return this->scene; };
    const Scene& GetScene() const noexcept { return this->scene; };
    Input& GetInput() noexcept { return this->input; };
    const Input& GetInput() const noexcept { return this->input; };

private:
    friend class Application;
    Window(
        int width,
        int height,
        std::string title,
        GLFWwindow* sharedContext = nullptr
    );
    void init();
    void Release() noexcept;
private:
    WindowSize size;
    std::string title;
    GLFWwindow* window = nullptr;
    Input input;
    ResourceManager resources;
    Scene scene;
    std::unique_ptr<FreeCameraControllerBehaviour> cameraController;
};
