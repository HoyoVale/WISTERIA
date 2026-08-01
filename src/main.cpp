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
        Application& application,
        Window& primaryWindow,
        std::shared_ptr<Scene> scene
    )
        : application(application),
          primaryWindow(primaryWindow),
          scene(std::move(scene))
    {
    }

    void Update(Entity&, float deltaTime) override
    {
        this->elapsed += deltaTime;
        if (!this->createdSecondary && this->elapsed >= 1.0f)
        {
            Window& window = this->application.CreateWindow(WindowConfig{
                .width = 480,
                .height = 480,
                .title = "FLORAL WISTERIA - DYNAMIC VIEW"
            });
            const std::shared_ptr<Camera> camera =
                this->application.CreateCamera(CameraParam{
                    .Position = {3.5f, 1.1f, 0.25f},
                    .Target = {0.0f, 1.1f, 0.25f},
                    .Up = {0.0f, 1.0f, 0.0f}
                });
            this->application.BindRenderView(window, this->scene, camera);
            this->secondaryWindow = &window;
            this->createdSecondary = true;
        }

        if (this->secondaryWindow != nullptr && this->elapsed >= 3.0f)
        {
            this->application.DestroyWindow(*this->secondaryWindow);
            this->secondaryWindow = nullptr;
        }

        if (!this->requestedPrimaryClose && this->elapsed >= 5.0f)
        {
            this->application.DestroyWindow(this->primaryWindow);
            this->requestedPrimaryClose = true;
        }
    }

private:
    Application& application;
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
        Window& window = application.CreateWindow(WindowConfig{
            .width = 600,
            .height = 600,
            .title = "FLORAL WISTERIA"
        });
        const std::shared_ptr<Scene> sharedScene = application.CreateScene();
        SetupDemoScene(*sharedScene, application.GetResources());
        application.BindScene(window, sharedScene);
        application.EnableFreeCameraController(window);

        if (HasArgument(argumentCount, arguments, "--multi-window"))
        {
            Window& secondWindow = application.CreateWindow(WindowConfig{
                .width = 600,
                .height = 600,
                .title = "FLORAL WISTERIA - SECOND VIEW"
            });
            const std::shared_ptr<Camera> sideCamera =
                application.CreateCamera(CameraParam{
                    .Position = {3.5f, 1.1f, 0.25f},
                    .Target = {0.0f, 1.1f, 0.25f},
                    .Up = {0.0f, 1.0f, 0.0f}
                });
            application.BindRenderView(
                secondWindow,
                sharedScene,
                sideCamera
            );
            application.EnableFreeCameraController(secondWindow);
        }

        if (HasArgument(argumentCount, arguments, "--dynamic-window"))
        {
            Entity* host = sharedScene->EntityAt(0);
            if (host == nullptr)
                throw std::runtime_error("Dynamic window demo requires an Entity");
            host->AddBehaviour<DynamicWindowDemoBehaviour>(
                application,
                window,
                sharedScene
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
