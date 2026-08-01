#include "pch.hpp"
#include "application.hpp"
#include "demo_scene.hpp"
#include "window.hpp"

#include <iostream>
#include <string_view>

namespace
{
bool HasArgument(
    int argumentCount,
    char* arguments[],
    std::string_view expected
)
{
    for (int index = 1; index < argumentCount; ++index)
    {
        if (arguments[index] != nullptr && arguments[index] == expected)
            return true;
    }
    return false;
}

class DynamicWindowDemoBehaviour final : public Behaviour
{
public:
    DynamicWindowDemoBehaviour(
        WindowManager& windowManager,
        Window& primaryWindow,
        std::shared_ptr<Scene> scene
    )
        : windowManager(windowManager),
          primaryWindow(primaryWindow),
          scene(std::move(scene))
    {
    }

    void Update(Entity&, float deltaTime) override
    {
        this->elapsed += deltaTime;
        if (!this->createdSecondary && this->elapsed >= 1.0f)
        {
            Window& window = this->windowManager.CreateWindow(WindowConfig{
                .width = 480,
                .height = 480,
                .title = "FLORAL WISTERIA - DYNAMIC VIEW"
            });
            const std::shared_ptr<Camera> camera =
                this->windowManager.CreateCamera(CameraParam{
                    .Position = {3.5f, 1.1f, 0.25f},
                    .Target = {0.0f, 1.1f, 0.25f},
                    .Up = {0.0f, 1.0f, 0.0f}
                });
            this->windowManager.BindRenderView(window, this->scene, camera);
            this->secondaryWindow = &window;
            this->createdSecondary = true;
        }

        if (this->secondaryWindow != nullptr && this->elapsed >= 3.0f)
        {
            this->windowManager.DestroyWindow(*this->secondaryWindow);
            this->secondaryWindow = nullptr;
        }

        if (!this->requestedPrimaryClose && this->elapsed >= 5.0f)
        {
            this->windowManager.DestroyWindow(this->primaryWindow);
            this->requestedPrimaryClose = true;
        }
    }

private:
    WindowManager& windowManager;
    Window& primaryWindow;
    std::shared_ptr<Scene> scene;
    Window* secondaryWindow = nullptr;
    float elapsed = 0.0f;
    bool createdSecondary = false;
    bool requestedPrimaryClose = false;
};
}

int main(int argumentCount, char* arguments[])
{
    try
    {
        Application application;
        WindowManager& windowManager = application.GetWindowManager();
        Window& window1 = windowManager.CreateWindow(WindowConfig{
            .width = 600,
            .height = 600,
            .title = "WINDOW 1"
        });
        Window& window2 = windowManager.CreateWindow(WindowConfig{
            .width = 600,
            .height = 600,
            .title = "WINDOW 2"
        });
        const std::shared_ptr<Scene> Scene1 = windowManager.CreateScene();
        const std::shared_ptr<Scene> Scene2 = windowManager.CreateScene();
        SetupDemoScene1(*Scene1, application.GetResources());
        SetupDemoScene2(*Scene2, application.GetResources());
        windowManager.BindScene(window1, Scene1);
        windowManager.EnableFreeCameraController(window1);
        windowManager.BindScene(window2, Scene2);
        windowManager.EnableFreeCameraController(window2);

        if (HasArgument(argumentCount, arguments, "--multi-window"))
        {
            Window& secondWindow = windowManager.CreateWindow(WindowConfig{
                .width = 600,
                .height = 600,
                .title = "WINDOW 1 - SECOND VIEW"
            });
            const std::shared_ptr<Camera> sideCamera =
                windowManager.CreateCamera(CameraParam{
                    .Position = {3.5f, 1.1f, 0.25f},
                    .Target = {0.0f, 1.1f, 0.25f},
                    .Up = {0.0f, 1.0f, 0.0f}
                });
            windowManager.BindRenderView(
                secondWindow,
                Scene1,
                sideCamera
            );
            windowManager.EnableFreeCameraController(secondWindow);
        }

        if (HasArgument(argumentCount, arguments, "--dynamic-window"))
        {
            Entity* host = Scene1->EntityAt(0);
            if (host == nullptr)
                throw std::runtime_error("Dynamic window demo requires an Entity");
            host->AddBehaviour<DynamicWindowDemoBehaviour>(
                windowManager,
                window1,
                Scene1
            );
        }

        const int result = application.Run();
        std::cout << "[INFO] Application was closed" << std::endl;
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ERROR] " << error.what() << std::endl;
        return 1;
    }
}
