#pragma once
#include <glm/glm.hpp>

#include "wisteria/scene/behaviour.hpp"
#include "wisteria/platform/input.hpp"
#include "wisteria/rendering/graphics_context.hpp"
#include "wisteria/scene/scene.hpp"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

namespace wisteria
{
class Application;
class WindowManager;
class GraphicsDevice;

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
    // R1.7 Phase 0C: share-group identity assigned by WindowManager. All
    // windows of one Application map to the same token.
    GraphicsShareGroupToken ShareGroupToken() const noexcept
    {
        return this->shareGroupToken;
    }
    // R1.7 Final Fix: native context identity (GLFWwindow*). Each window is
    // its own context; context-local GPU objects belong to this identity.
    GraphicsContextToken ContextToken() const noexcept
    {
        return this->contextToken;
    }
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
    const FreeCameraControllerSettings& GetFreeCameraControllerSettings()
        const noexcept;
    void SetFreeCameraControllerSettings(
        const FreeCameraControllerSettings& settings
    );
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
        GLFWwindow* sharedContext = nullptr,
        bool visible = true
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
    GraphicsContextToken contextToken = nullptr;
    GraphicsShareGroupToken shareGroupToken = nullptr;
    // Non-owning device reference for the MakeCurrent lifecycle transaction
    // (flush pending GPU deletes after the share group becomes current).
    GraphicsDevice* device = nullptr;
    Input input;
    SceneHandle scene;
    CameraHandle camera;
    std::unique_ptr<FreeCameraControllerBehaviour> cameraController;
};
}  // namespace wisteria
