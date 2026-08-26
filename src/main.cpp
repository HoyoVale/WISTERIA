#include "wisteria/common/pch.hpp"
#include "wisteria/platform/application.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/platform/window.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace wisteria;

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

std::optional<std::string_view> ArgumentValue(
    int argumentCount,
    char* arguments[],
    std::string_view option
)
{
    for (int index = 1; index + 1 < argumentCount; ++index)
    {
        if (arguments[index] != nullptr &&
            std::string_view(arguments[index]) == option &&
            arguments[index + 1] != nullptr)
        {
            return std::string_view(arguments[index + 1]);
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> PathArgument(
    int argumentCount,
    char* arguments[],
    std::string_view option
)
{
    const std::optional<std::string_view> value =
        ArgumentValue(argumentCount, arguments, option);
    if (!value.has_value())
        return std::nullopt;
    return std::filesystem::path(std::string(*value));
}

std::optional<std::size_t> PositiveSizeArgument(
    int argumentCount,
    char* arguments[],
    std::string_view option
)
{
    const std::optional<std::string_view> value =
        ArgumentValue(argumentCount, arguments, option);
    if (!value.has_value())
        return std::nullopt;

    std::size_t parsedCharacters = 0U;
    const unsigned long long parsed = std::stoull(
        std::string(*value),
        &parsedCharacters
    );
    if (parsedCharacters != value->size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()))
    {
        throw std::invalid_argument(
            std::string(option) + " must be a positive integer"
        );
    }
    return static_cast<std::size_t>(parsed);
}

std::optional<float> PositiveFloatArgument(
    int argumentCount,
    char* arguments[],
    std::string_view option
)
{
    const std::optional<std::string_view> value =
        ArgumentValue(argumentCount, arguments, option);
    if (!value.has_value())
        return std::nullopt;

    std::size_t parsedCharacters = 0U;
    const float parsed = std::stof(std::string(*value), &parsedCharacters);
    if (parsedCharacters != value->size() || !std::isfinite(parsed) ||
        parsed <= 0.0f)
    {
        throw std::invalid_argument(
            std::string(option) + " must be a positive finite number"
        );
    }
    return parsed;
}

void PrintHelp()
{
    std::cout
        << "WISTERIA desktop MMD demo\n\n"
        << "Usage:\n"
        << "  wisteria [options]\n\n"
        << "Options:\n"
        << "  --model <pmx>       Override character PMX path\n"
        << "  --motion <vmd>      Override character VMD path\n"
        << "  --scene <pmx>       Enable scene mode and load a stage PMX\n"
        << "  --gltf <glb|vrm>    Generic glTF/GLB/VRM viewer mode\n"
        << "  --ground-lab        Fixed-camera ground + cube render lab\n"
        << "  --alternate-model   Use the alternate built-in model preset\n"
        << "  --frames <n>        Run exactly n pull-model frames, then exit\n"
        << "  --fixed-dt <sec>    Delta time used with --frames (default 1/60)\n"
        << "  --render-smoke      Shorthand for --frames 180 --fixed-dt 1/60\n"
        << "  --help              Show this help\n\n"
        << "Useful diagnostics environment variables:\n"
        << "  WISTERIA_ASSET_ROOT=<project>/assets\n"
        << "  WISTERIA_FRAME_PROFILE=1\n"
        << "  WISTERIA_SCREENSHOT_DIR=<output directory>\n"
        << "  WISTERIA_GL_DIAGNOSTICS=1\n";
}
}

int main(int argumentCount, char* arguments[])
{
    try
    {
        if (HasArgument(argumentCount, arguments, "--help"))
        {
            PrintHelp();
            return 0;
        }

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
        const bool groundLab = HasArgument(
            argumentCount,
            arguments,
            "--ground-lab"
        );
        if (groundLab && sceneMode)
        {
            throw std::invalid_argument(
                "--ground-lab cannot be combined with --scene"
            );
        }
        const std::optional<std::filesystem::path> gltfPath =
            PathArgument(argumentCount, arguments, "--gltf");
        const bool gltfMode = gltfPath.has_value();
        if (gltfMode && (groundLab || sceneMode))
        {
            throw std::invalid_argument(
                "--gltf cannot be combined with --ground-lab or --scene"
            );
        }
        const std::optional<std::filesystem::path> modelPath =
            PathArgument(argumentCount, arguments, "--model");
        const std::optional<std::filesystem::path> motionPath =
            PathArgument(argumentCount, arguments, "--motion");
        const std::optional<std::filesystem::path> scenePath =
            PathArgument(argumentCount, arguments, "--scene");
        if (gltfMode && (modelPath.has_value() || motionPath.has_value()))
        {
            throw std::invalid_argument(
                "--gltf cannot be combined with --model or --motion"
            );
        }

        std::optional<std::size_t> frameLimit =
            PositiveSizeArgument(argumentCount, arguments, "--frames");
        std::optional<float> fixedDelta =
            PositiveFloatArgument(argumentCount, arguments, "--fixed-dt");
        if (HasArgument(argumentCount, arguments, "--render-smoke"))
        {
            if (!frameLimit.has_value())
                frameLimit = 180U;
            if (!fixedDelta.has_value())
                fixedDelta = 1.0f / 60.0f;
        }
        if (fixedDelta.has_value() && !frameLimit.has_value())
        {
            throw std::invalid_argument(
                "--fixed-dt requires --frames or --render-smoke"
            );
        }

        Window& primaryWindow = application.CreateWindow(WindowConfig{
            .width = 960,
            .height = 720,
            .title = groundLab
                ? "FLORAL WISTERIA - GROUND LAB"
                : (gltfMode
                    ? "FLORAL WISTERIA - GLTF VIEWER"
                    : (sceneMode
                        ? "FLORAL WISTERIA - MMD SCENE"
                        : "FLORAL WISTERIA - MMD DREAM WINGS"))
        });
        const std::shared_ptr<Scene> scene = application.CreateScene();
        if (groundLab)
        {
            SetupGroundShadowLabScene(
                *scene,
                application.GetResources()
            );
        }
        else if (gltfMode)
        {
            SetupGenericGltfDemoScene(
                *scene,
                application.GetResources(),
                *gltfPath
            );
        }
        else
        {
            SetupSabaMmdDemoScene(
                *scene,
                application.GetResources(),
                primaryWindow,
                alternateModel,
                modelPath.value_or(std::filesystem::path{}),
                scenePath.value_or(std::filesystem::path{}),
                sceneMode,
                motionPath.value_or(std::filesystem::path{})
            );
        }
        windowManager.BindScene(primaryWindow, scene);
        FreeCameraControllerSettings cameraSettings;
        cameraSettings.moveSpeed = groundLab
            ? 6.0f
            : (gltfMode ? 3.0f : (sceneMode ? 12.0f : 2.5f));
        windowManager.EnableFreeCameraController(
            primaryWindow,
            cameraSettings
        );

        int result = 0;
        if (frameLimit.has_value())
        {
            const float deltaTime = fixedDelta.value_or(1.0f / 60.0f);
            std::cout << "[RENDER SMOKE] begin frames=" << *frameLimit
                      << " fixedDt=" << deltaTime << std::endl;
            std::size_t completedFrames = 0U;
            while (completedFrames < *frameLimit &&
                   !primaryWindow.ShouldClose())
            {
                application.PollEventsAndRender(deltaTime);
                ++completedFrames;
                if (completedFrames <= 3U || completedFrames % 60U == 0U)
                {
                    std::cout << "[RENDER SMOKE] frame="
                              << completedFrames << std::endl;
                }
            }
            std::cout << "[RENDER SMOKE] completed=" << completedFrames
                      << " requested=" << *frameLimit
                      << " closed="
                      << (primaryWindow.ShouldClose() ? "true" : "false")
                      << std::endl;
        }
        else
        {
            result = application.Run();
        }

        std::cout << "[INFO] Application was closed" << std::endl;
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ERROR] " << error.what() << std::endl;
        return 1;
    }
}
