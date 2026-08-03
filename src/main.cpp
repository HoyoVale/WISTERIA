#include "wisteria/common/pch.hpp"
#include "wisteria/platform/application.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/platform/window.hpp"

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
                    .Position = {18.0f, 9.0f, 8.0f},
                    .Target = {0.0f, 9.0f, 0.3f},
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
        const bool morphLab = HasArgument(
            argumentCount,
            arguments,
            "--morph-lab"
        );
        const bool alternateModel = HasArgument(
            argumentCount,
            arguments,
            "--alternate-model"
        ) || HasArgument(argumentCount, arguments, "--character-pair");

        Window& primaryWindow = windowManager.CreateWindow(WindowConfig{
            .width = 720,
            .height = 720,
            .title = morphLab
                ? "FLORAL WISTERIA - MORPH LAB"
                : "FLORAL WISTERIA - MMD LEGACY"
        });
        std::shared_ptr<Scene> legacyScene;
        if (morphLab)
        {
            const std::shared_ptr<Scene> scene = windowManager.CreateScene();
            SetupMorphDemoScene(
                *scene,
                application.GetResources(),
                primaryWindow
            );
            windowManager.BindScene(primaryWindow, scene);
            windowManager.EnableFreeCameraController(primaryWindow);
        }
        else
        {
            legacyScene = windowManager.CreateScene();
            SetupMmdCharacterDemo(
                *legacyScene,
                application.GetResources(),
                primaryWindow,
                alternateModel,
                false
            );
            windowManager.BindScene(primaryWindow, legacyScene);
            windowManager.EnableFreeCameraController(primaryWindow);

            Window& compatWindow = windowManager.CreateWindow(WindowConfig{
                .width = 720,
                .height = 720,
                .title = "FLORAL WISTERIA - MMD COMPAT (Saba)"
            });
            const std::shared_ptr<Scene> compatScene =
                windowManager.CreateScene();
            SetupMmdCharacterDemo(
                *compatScene,
                application.GetResources(),
                compatWindow,
                alternateModel,
                true
            );
            windowManager.BindScene(compatWindow, compatScene);
            windowManager.EnableFreeCameraController(compatWindow);
        }

        if (HasArgument(argumentCount, arguments, "--multi-window") &&
            legacyScene != nullptr)
        {
            Window& secondWindow = windowManager.CreateWindow(WindowConfig{
                .width = 600,
                .height = 600,
                .title = "FLORAL WISTERIA - SECOND VIEW"
            });
            const std::shared_ptr<Camera> sideCamera =
                windowManager.CreateCamera(CameraParam{
                    .Position = {18.0f, 9.0f, 8.0f},
                    .Target = {0.0f, 9.0f, 0.3f},
                    .Up = {0.0f, 1.0f, 0.0f}
                });
            windowManager.BindRenderView(secondWindow, legacyScene, sideCamera);
            windowManager.EnableFreeCameraController(secondWindow);
        }

        if (HasArgument(argumentCount, arguments, "--dynamic-window") &&
            legacyScene != nullptr)
        {
            Entity* host = legacyScene->EntityAt(0);
            if (host == nullptr)
                throw std::runtime_error("Dynamic window demo requires an Entity");
            host->AddBehaviour<DynamicWindowDemoBehaviour>(
                windowManager,
                primaryWindow,
                legacyScene
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
