#pragma once
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/platform/input.hpp"
#include "wisteria/scene/scene.hpp"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

class Application;
class WindowManager;

using SceneHandle = std::shared_ptr<Scene>;
using CameraHandle = std::shared_ptr<Camera>;

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
    const std::string& Title() const noexcept;
    void SetTitle(std::string title);
    void BeginInputFrame() noexcept;
    void Update(float deltaTime);
    void EnableFreeCameraController(
        const FreeCameraControllerSettings& settings = {}
    );
    void DisableFreeCameraController() noexcept;
    Scene& GetScene() noexcept { return *this->scene; };
    const Scene& GetScene() const noexcept { return *this->scene; };
    Camera& GetCamera() noexcept { return *this->camera; };
    const Camera& GetCamera() const noexcept { return *this->camera; };
    const SceneHandle& GetSceneHandle() const noexcept { return this->scene; };
    const CameraHandle& GetCameraHandle() const noexcept { return this->camera; };
    Input& GetInput() noexcept { return this->input; };
    const Input& GetInput() const noexcept { return this->input; };

private:
    friend class Application;
    friend class WindowManager;
    Window(
        int width,
        int height,
        std::string title,
        GLFWwindow* sharedContext = nullptr
    );
    void init();
    void BindScene(SceneHandle nextScene);
    void BindCamera(CameraHandle nextCamera);
    void BindRenderView(SceneHandle nextScene, CameraHandle nextCamera);
    void Release() noexcept;
private:
    WindowSize size;
    std::string title;
    GLFWwindow* window = nullptr;
    Input input;
    SceneHandle scene;
    CameraHandle camera;
    std::unique_ptr<FreeCameraControllerBehaviour> cameraController;
};
