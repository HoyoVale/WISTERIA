#include "pch.hpp"
#include "demo_scene.hpp"
#include "behaviour.hpp"
#include "manager.hpp"
#include "scene.hpp"
#include <filesystem>

namespace
{
std::filesystem::path DemoModelPath()
{
    return std::filesystem::current_path() / "assets" / "models" /
        u8"今汐_pmx" / u8"今汐.pmx";
}

std::filesystem::path DemoSecondModelPath()
{
    return std::filesystem::current_path() / "assets" / "models" /
        u8"爱弥斯_pmx" / u8"爱弥斯.pmx";
}
}

void SetupDemoScene(Scene& scene, ResourceManager& resources)
{
    EnvironmentMap* existingEnvironment =
        resources.FindEnvironment("defaultSky");
    EnvironmentMap& environment = existingEnvironment != nullptr
        ? *existingEnvironment
        : resources.CreateEnvironment(
            "defaultSky",
            EnvironmentMapData::ProceduralSky()
        );
    scene.SetEnvironment(&environment);

    ModelAsset& firstModel = resources.LoadModel(
        "yixuan",
        DemoModelPath()
    );
    Entity& firstEntity = scene.InstantiateModel(
        firstModel,
        Transform(
            glm::vec3(0.8f, 0.0f, 0.0f),
            glm::vec3(0.0f),
            glm::vec3(0.1f)
        )
    );

    ModelAsset& secondModel = resources.LoadModel(
        "yixuan2",
        DemoSecondModelPath()
    );
    Entity& secondEntity = scene.InstantiateModel(
        secondModel,
        Transform(
            glm::vec3(-0.8f, 0.0f, 0.0f),
            glm::vec3(0.0f),
            glm::vec3(0.1f)
        )
    );

    firstEntity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f));
    secondEntity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f));

    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {0.0f, 1.1f, 3.5f},
        .Target = {0.0f, 1.1f, 0.25f},
        .Up = {0.0f, 1.0f, 0.0f}
    });
    scene.CreatePointLight(PointLightData{
        .Position = {2.5f, 1.5f, 2.5f},
        .Color = {1.0f, 1.0f, 1.0f},
        .Intensity = 1.6f,
        .Range = 8.0f
    });
}
