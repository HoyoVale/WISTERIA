#include "wisteria/common/pch.hpp"
#include "wisteria/platform/application.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/platform/window.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
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

std::optional<std::filesystem::path> ModelPathArgument(
    int argumentCount,
    char* arguments[]
)
{
    for (int index = 1; index + 1 < argumentCount; ++index)
    {
        if (arguments[index] != nullptr &&
            std::string_view(arguments[index]) == "--model" &&
            arguments[index + 1] != nullptr)
        {
            return std::filesystem::path(arguments[index + 1]);
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ScenePathArgument(
    int argumentCount,
    char* arguments[]
)
{
    for (int index = 1; index + 1 < argumentCount; ++index)
    {
        if (arguments[index] != nullptr &&
            std::string_view(arguments[index]) == "--scene" &&
            arguments[index + 1] != nullptr)
        {
            return std::filesystem::path(arguments[index + 1]);
        }
    }
    return std::nullopt;
}
}

int main(int argumentCount, char* arguments[])
{
    try
    {
        Application application;
        WindowManager& windowManager = application.GetWindowManager();
        const bool alternateModel = HasArgument(
            argumentCount,
            arguments,
            "--alternate-model"
        );
        const bool sceneMode = HasArgument(
            argumentCount,
            arguments,
            "--scene"
        );
        const std::optional<std::filesystem::path> modelPath =
            ModelPathArgument(argumentCount, arguments);
        const std::optional<std::filesystem::path> scenePath =
            ScenePathArgument(argumentCount, arguments);

        Window& primaryWindow = windowManager.CreateWindow(WindowConfig{
            .width = 960,
            .height = 720,
            .title = sceneMode
                ? "FLORAL WISTERIA - MMD SCENE"
                : "FLORAL WISTERIA - MMD DREAM WINGS"
        });
        const std::shared_ptr<Scene> scene = windowManager.CreateScene();
        SetupSabaMmdDemoScene(
            *scene,
            application.GetResources(),
            primaryWindow,
            alternateModel,
            modelPath.value_or(std::filesystem::path{}),
            scenePath.value_or(std::filesystem::path{}),
            sceneMode
        );
        windowManager.BindScene(primaryWindow, scene);
        FreeCameraControllerSettings cameraSettings;
        // Scene PMX can span hundreds of units; give scene mode a faster
        // default so the user can cross the whole set without holding Shift.
        cameraSettings.moveSpeed = sceneMode ? 12.0f : 2.5f;
        windowManager.EnableFreeCameraController(
            primaryWindow,
            cameraSettings
        );

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
