#include "pch.hpp"
#include "demo_scene.hpp"
#include "behaviour.hpp"
#include "manager.hpp"
#include "scene.hpp"
#include <filesystem>

namespace
{
std::filesystem::path DemoModelPath1()
{
    return std::filesystem::current_path() / "assets" / "models" /
        "mmd" / u8"仪玄_pmx" / u8"仪玄.pmx";
}

std::filesystem::path DemoModelPath2()
{
    return std::filesystem::current_path() / "assets" / "models" /
        "mmd" / u8"仪玄皮肤_pmx" / u8"仪玄.pmx";
}
}

void SetupDemoScene1(Scene& scene, ResourceManager& resources)
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

    ModelAsset& Model = resources.LoadModel("yixuan1",DemoModelPath1());
    Entity& Entity = scene.InstantiateModel(
        Model,
        Transform(
            glm::vec3(0.0f, 0.0f, 0.1f),
            glm::vec3(0.0f),
            glm::vec3(0.3f)
        )
    );

    Entity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f));

    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {0.0f, 2.1f, 3.5f},
        .Target = {0.0f, 2.1f, 0.25f},
        .Up = {0.0f, 1.0f, 0.0f}
    });
    scene.CreatePointLight(PointLightData{
        .Position = {2.5f, 1.5f, 2.5f},
        .Color = {1.0f, 1.0f, 1.0f},
        .Intensity = 1.6f,
        .Range = 8.0f
    });
}

void SetupDemoScene2(Scene& scene, ResourceManager& resources)
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

    ModelAsset& Model = resources.LoadModel("yixuan2",DemoModelPath2());
    Entity& Entity = scene.InstantiateModel(
        Model,
        Transform(
            glm::vec3(0.0f, 0.0f, 0.1f),
            glm::vec3(0.0f),
            glm::vec3(0.3f)
        )
    );

    Entity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f));

    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {0.0f, 2.1f, 3.5f},
        .Target = {0.0f, 2.1f, 0.25f},
        .Up = {0.0f, 1.0f, 0.0f}
    });
    scene.CreatePointLight(PointLightData{
        .Position = {2.5f, 1.5f, 2.5f},
        .Color = {1.0f, 1.0f, 1.0f},
        .Intensity = 1.6f,
        .Range = 8.0f
    });
}

