#include "wisteria/common/pch.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/core/asset_paths.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/assets/manager.hpp"
#include "wisteria/assets/saba_mmd_importer.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"
#include "wisteria/scene/scene.hpp"
#include "wisteria/platform/input.hpp"
#include "wisteria/platform/window.hpp"
#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/primitives/cube.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace wisteria
{
namespace
{
std::filesystem::path DemoModelPath(bool alternate)
{
    return wisteria::assets::Root() / "models" /
        "mmd" /
        (alternate ? u8"叶瞬光皮肤_pmx" : u8"蕾米埃尔-白") /
        u8"蕾米埃尔-白.pmx";
}

std::filesystem::path DemoScenePath(bool alternate)
{
    return wisteria::assets::Root() / "models" /
        "mmd" /
        (alternate ? u8"星穹列车-观景车厢" : u8"随便观") /
        (alternate ? u8"Stage0514.pmx" : u8"随便观.pmx");
}

std::filesystem::path DemoDreamWingMotionPath()
{
    return wisteria::assets::Root() / "motions" /
        u8"梦的翅膀" / u8"梦的翅膀motion.vmd";
}

std::filesystem::path DemoDreamWingCameraPath()
{
    return wisteria::assets::Root() / "motions" /
        u8"梦的翅膀" / u8"梦的翅膀camera.vmd";
}

std::string ToNarrowUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(u8.data()),
        u8.size()
    );
}

void ConfigureCharacterLighting(Scene& scene)
{
    // Main directional light also drives the CSM shadow map and the MMD
    // ground shadow projection.
    scene.CreateDirectionalLight(DirectionalLightData{
        .Direction = {-0.35f, -0.75f, -0.45f},
        .Color = {1.0f, 0.96f, 0.92f},
        .Intensity = 0.75f
    });
    scene.CreatePointLight(PointLightData{
        .Position = {5.0f, 13.0f, 9.0f},
        .Color = {1.0f, 0.88f, 0.78f},
        .Intensity = 1.4f,
        .Range = 35.0f,
        .Linear = 0.035f,
        .Quadratic = 0.006f
    });
    // scene.CreatePointLight(PointLightData{
    //     .Position = {-6.0f, 9.0f, 5.0f},
    //     .Color = {0.62f, 0.72f, 1.0f},
    //     .Intensity = 1.3f,
    //     .Range = 30.0f,
    //     .Linear = 0.045f,
    //     .Quadratic = 0.008f
    // });
}

namespace
{
constexpr std::size_t GroundVertexStride = 15U;  // pos3 color3 uv2 normal3 tangent4

DefaultModelData BuildGroundMeshData(float size)
{
    const float half = size * 0.5f;
    DefaultModelData data;
    data.layout = {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT},
        {"normal", 3, FLOAT},
        {"tangent", 4, FLOAT, false, false, 4U}
    };
    const float positions[4][2] = {
        {-half, -half},
        {half, -half},
        {half, half},
        {-half, half}
    };
    const float uvs[4][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    for (int index = 0; index < 4; ++index)
    {
        const float vertex[GroundVertexStride] = {
            positions[index][0], 0.0f, positions[index][1],
            0.75f, 0.75f, 0.75f,
            uvs[index][0], uvs[index][1],
            0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f
        };
        for (std::size_t component = 0U;
             component < GroundVertexStride;
             ++component)
        {
            data.vertices.push_back(vertex[component]);
        }
    }
    // Winding must be counter-clockwise when viewed from +Y so the plane's
    // front face points up: the renderer back-face culls non-double-sided
    // materials, and the previous (0,1,2) winding produced a -Y geometric
    // normal that got culled by every camera above the ground.
    data.indices = {0U, 2U, 1U, 0U, 3U, 2U};
    return data;
}

void AddDemoGround(Scene& scene, ResourceManager& resources)
{
    Mesh& groundMesh = resources.CreateMesh(
        "demo::groundMesh",
        BuildGroundMeshData(60.0f)
    );
    MaterialData groundData;
    groundData.groundPlane = true;
    Material& groundMaterial = resources.CreateMaterial(
        "demo::groundMaterial",
        groundData
    );
    scene.CreateEntity(groundMesh, groundMaterial);
    std::cout << "[GROUND] demo ground added: size=60 planeY=0"
              << " texture=chessboard frontFace=+Y" << std::endl;
}
}

class SabaDemoBehaviour final : public Behaviour
{
public:
    SabaDemoBehaviour(
        std::shared_ptr<SabaMmdRuntimeModel> runtime,
        std::vector<Mesh*> meshes,
        Camera& camera,
        Window& window,
        Input& input,
        bool sceneMode,
        float cameraSpeed
    )
        : runtime(std::move(runtime)),
          meshes(std::move(meshes)),
          camera(&camera),
          window(&window),
          input(&input),
          sceneMode(sceneMode),
          cameraFollowEnabled(
              std::getenv("WISTERIA_DISABLE_CAMERA_MOTION") == nullptr
          ),
          cameraSpeed(cameraSpeed)
    {
    }

    void Update(Entity&, float deltaTime) override
    {
        if (this->input->WasKeyPressed(InputKey::Space))
        {
            if (this->runtime->IsMotionPaused())
                this->runtime->ResumeMotion();
            else
                this->runtime->PauseMotion();
            this->titleDirty = true;
        }
        if (this->input->WasKeyPressed(InputKey::C))
        {
            this->cameraFollowEnabled = !this->cameraFollowEnabled;
            this->titleDirty = true;
        }
        bool speedChanged = false;
        if (this->input->WasKeyPressed(InputKey::Left))
        {
            this->cameraSpeed = std::max(0.1f, this->cameraSpeed * 0.5f);
            speedChanged = true;
        }
        if (this->input->WasKeyPressed(InputKey::Right))
        {
            this->cameraSpeed = std::min(200.0f, this->cameraSpeed * 2.0f);
            speedChanged = true;
        }
        if (speedChanged)
        {
            FreeCameraControllerSettings settings;
            settings.moveSpeed = this->cameraSpeed;
            this->window->SetFreeCameraControllerSettings(settings);
            this->titleDirty = true;
        }

        this->runtime->Update(deltaTime);
        if (this->cameraFollowEnabled && !this->runtime->IsMotionPaused())
        {
            this->runtime->ApplyCameraMotion(
                static_cast<float>(this->runtime->MotionFrame()),
                *this->camera
            );
        }

        this->titleElapsed += deltaTime;
        if (this->titleDirty || this->titleElapsed >= 0.2f)
        {
            this->titleElapsed = 0.0f;
            std::string title = this->sceneMode
                ? "FLORAL WISTERIA - MMD SCENE"
                : "FLORAL WISTERIA - MMD DREAM WINGS";
            if (this->runtime->IsMotionPaused())
                title += " [PAUSED]";
            title += " | Space: pause | C: VMD camera ";
            if (this->sceneMode)
                title += "N/A";
            else
                title += this->cameraFollowEnabled ? "ON" : "OFF";
            std::ostringstream speedText;
            speedText << std::fixed << std::setprecision(1)
                      << this->cameraSpeed;
            title += " | Speed " + speedText.str();
            this->window->SetTitle(std::move(title));
            this->titleDirty = false;
        }

        if ((++this->diagnosticCounter % 60) == 1)
        {
            const SabaMmdRuntimeModel::VertexDiagnostics diagnostics =
                this->runtime->DiagnoseVertices();
            std::cout << "[SABA SKIN] finite="
                      << (diagnostics.finite ? "true" : "false")
                      << " min=(" << diagnostics.minimumPosition.x << ", "
                      << diagnostics.minimumPosition.y << ", "
                      << diagnostics.minimumPosition.z << ")"
                      << " max=(" << diagnostics.maximumPosition.x << ", "
                      << diagnostics.maximumPosition.y << ", "
                      << diagnostics.maximumPosition.z << ")"
                      << " maxBindDisplacement="
                      << diagnostics.maximumDisplacementFromBind
                      << std::endl;
            const SabaMmdRuntimeModel::ProfileSnapshot profile =
                this->runtime->Profile();
            std::cout << "[SABA PROFILE] frames=" << profile.frameCount
                      << " updateAvgMs="
                      << profile.averageUpdateMilliseconds
                      << " uploadAvgMs="
                      << profile.averageUploadMilliseconds
                      << std::endl;
        }
    }

private:
    std::shared_ptr<SabaMmdRuntimeModel> runtime;
    std::vector<Mesh*> meshes;
    Camera* camera = nullptr;
    Window* window = nullptr;
    Input* input = nullptr;
    bool sceneMode = false;
    bool cameraFollowEnabled = true;
    float cameraSpeed = 2.5f;
    float titleElapsed = 0.0f;
    bool titleDirty = true;
    std::size_t diagnosticCounter = 0U;
};

ModelAsset& CreateSabaMeshModel(
    ResourceManager& resources,
    const std::string& name,
    const std::filesystem::path& modelPath,
    glm::vec3* outMinimum = nullptr,
    glm::vec3* outMaximum = nullptr
)
{
    SabaMmdImporter importer;
    ImportedModelData imported = importer.Import(modelPath);
    if (outMinimum != nullptr && outMaximum != nullptr)
    {
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(-std::numeric_limits<float>::max());
        for (const ImportedMeshData& mesh : imported.meshes)
        {
            std::size_t stride = 0U;
            std::size_t positionOffset = 0U;
            for (const Layout& attribute : mesh.data.layout)
            {
                if (attribute.name == "position")
                    positionOffset = stride;
                stride += attribute.size;
            }
            if (stride == 0U ||
                mesh.data.vertices.size() % stride != 0U)
            {
                continue;
            }
            for (std::size_t vertex = 0U;
                 vertex < mesh.data.vertices.size();
                 vertex += stride)
            {
                const glm::vec3 position(
                    mesh.data.vertices[vertex + positionOffset],
                    mesh.data.vertices[vertex + positionOffset + 1U],
                    mesh.data.vertices[vertex + positionOffset + 2U]
                );
                minimum = glm::min(minimum, position);
                maximum = glm::max(maximum, position);
            }
        }
        if (minimum.x <= maximum.x)
        {
            *outMinimum = minimum;
            *outMaximum = maximum;
        }
    }
    return resources.CreateModel(name, std::move(imported));
}
}

void SetupGroundShadowLabScene(Scene& scene, ResourceManager& resources)
{
    // Fixed camera: level-ish view that clearly includes the ground plane.
    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {9.0f, 8.0f, 11.0f},
        .Target = {0.0f, 0.8f, 0.0f},
        .Up = {0.0f, 1.0f, 0.0f},
        .VerticalFovDegrees = 45.0f
    });

    scene.CreateDirectionalLight(DirectionalLightData{
        .Direction = {-0.35f, -0.75f, -0.45f},
        .Color = {1.0f, 0.96f, 0.92f},
        .Intensity = 1.0f
    });

    // Ground: large, clearly visible light-gray plane.
    Mesh& groundMesh = resources.CreateMesh(
        "lab::groundMesh",
        BuildGroundMeshData(40.0f)
    );
    MaterialData groundData;
    groundData.baseColorFactor = {0.85f, 0.85f, 0.85f, 1.0f};
    groundData.emissiveFactor = {0.25f, 0.25f, 0.25f};
    groundData.roughnessFactor = 1.0f;
    groundData.groundPlane = true;
    Material& groundMaterial = resources.CreateMaterial(
        "lab::groundMaterial",
        groundData
    );
    scene.CreateEntity(groundMesh, groundMaterial);

    // Cube: floats above the ground and casts the ground shadow.
    Mesh& cubeMesh = resources.CreateMesh("lab::cubeMesh", cubeData);
    MaterialData cubeDataMaterial;
    cubeDataMaterial.baseColorFactor = {0.9f, 0.25f, 0.25f, 1.0f};
    cubeDataMaterial.emissiveFactor = {0.15f, 0.0f, 0.0f};
    cubeDataMaterial.castSelfShadow = true;
    cubeDataMaterial.receiveSelfShadow = true;
    cubeDataMaterial.groundShadow = true;
    Material& cubeMaterial = resources.CreateMaterial(
        "lab::cubeMaterial",
        cubeDataMaterial
    );
    scene.CreateEntity(
        cubeMesh,
        cubeMaterial,
        Transform(glm::vec3(0.0f, 2.5f, 0.0f))
    );
    std::cout << "[GROUND LAB] ground size=40 planeY=0 cubeY=2.5"
              << " camera=" << scene.ActiveCamera().GetParam().Position.x
              << "," << scene.ActiveCamera().GetParam().Position.y
              << "," << scene.ActiveCamera().GetParam().Position.z
              << std::endl;
}

void SetupSabaMmdDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window,
    bool alternateModel,
    std::filesystem::path modelPath,
    std::filesystem::path scenePath,
    bool sceneMode,
    std::filesystem::path motionPath,
    float physicsFps,
    int maxSubSteps
)
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

    if (modelPath.empty())
        modelPath = DemoModelPath(alternateModel);
    const std::u8string modelFileName = modelPath.filename().u8string();
    const std::string modelResourceName(
        reinterpret_cast<const char*>(modelFileName.data()),
        modelFileName.size()
    );
    if (scenePath.empty())
        scenePath = DemoScenePath(alternateModel);
    const std::u8string sceneFileName = scenePath.filename().u8string();
    const std::string sceneResourceName(
        reinterpret_cast<const char*>(sceneFileName.data()),
        sceneFileName.size()
    );
    glm::vec3 sceneMinimum{0.0f};
    glm::vec3 sceneMaximum{0.0f};
    ModelAsset& model = CreateSabaMeshModel(
        resources,
        "saba::" + modelResourceName,
        modelPath,
        sceneMode ? &sceneMinimum : nullptr,
        sceneMode ? &sceneMaximum : nullptr
    );
    ModelAsset& sceneModel = CreateSabaMeshModel(
        resources,
        "saba::" + sceneResourceName,
        scenePath,
        sceneMode ? &sceneMinimum : nullptr,
        sceneMode ? &sceneMaximum : nullptr
    );
    Entity& entity = scene.InstantiateModel(
        model,
        Transform(
            glm::vec3(0.0f, 0.0f, 0.1f),
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        )
    );
    if (sceneMode)
    {
        scene.InstantiateModel(
            sceneModel,
            Transform(
                glm::vec3(0.0f, 0.0f, 50.1f),
                glm::vec3(0.0f),
                glm::vec3(1.0f)
            )
        );
    }
    else
    {
        // Character demo: stand the character on a visible ground plane so
        // the MMD ground shadow has a surface to land on, and restore the
        // distant 随便观 stage backdrop for the original
        // stage + character + motion composition.
        AddDemoGround(scene, resources);
        scene.InstantiateModel(
            sceneModel,
            Transform(
                glm::vec3(0.0f, 0.0f, 50.1f),
                glm::vec3(0.0f),
                glm::vec3(1.0f)
            )
        );
    }
    if (motionPath.empty() && !sceneMode)
        motionPath = DemoDreamWingMotionPath();
    const std::filesystem::path cameraPath = DemoDreamWingCameraPath();
    SabaPhysicsSettings physicsSettings;
    if (physicsFps > 0.0f)
    {
        physicsSettings.fixedTimeStep = 1.0f / physicsFps;
    }
    else if (const char* fpsValue =
                 std::getenv("WISTERIA_SABA_PHYSICS_FPS"))
    {
        const float fps = static_cast<float>(std::atof(fpsValue));
        if (fps > 0.0f)
            physicsSettings.fixedTimeStep = 1.0f / fps;
    }
    if (maxSubSteps > 0)
    {
        physicsSettings.maxSubSteps = maxSubSteps;
    }
    else if (const char* stepsValue =
                 std::getenv("WISTERIA_SABA_PHYSICS_MAXSTEPS"))
    {
        const int steps = std::atoi(stepsValue);
        if (steps > 0)
            physicsSettings.maxSubSteps = steps;
    }
    auto runtime = std::make_shared<SabaMmdRuntimeModel>(
        modelPath,
        motionPath,
        physicsSettings
    );
    if (!runtime->Initialize())
    {
        throw std::runtime_error(
            "Saba demo runtime failed to initialize"
        );
    }
    runtime->SetMotionLooping(true);

    bool cameraLoaded = false;
    if (!sceneMode && std::filesystem::is_regular_file(cameraPath))
    {
        if (runtime->LoadCameraMotion(cameraPath))
        {
            cameraLoaded = true;
        }
        else
        {
            std::cerr << "[WARN] Camera VMD exists but has no camera frames: "
                      << ToNarrowUtf8(cameraPath) << std::endl;
        }
    }
    else if (!sceneMode)
    {
        std::cerr << "[WARN] Camera VMD not found: "
                  << ToNarrowUtf8(cameraPath) << std::endl;
    }

    std::vector<Mesh*> sabaMeshes;
    sabaMeshes.reserve(model.Parts().size());
    for (const RenderPart& part : model.Parts())
    {
        sabaMeshes.push_back(&part.GetMesh());
        Mesh& mesh = part.GetMesh();
        if (std::getenv("WISTERIA_SABA_NO_UPDATE") == nullptr)
        {
            mesh.SetDynamicVertexProvider(
                [runtime](Mesh& target)
                {
                    runtime->UploadDynamicVertices(target);
                }
            );
        }
    }
    entity.AddBehaviour<SabaDemoBehaviour>(
        runtime,
        std::move(sabaMeshes),
        scene.ActiveCamera(),
        window,
        window.GetInput(),
        sceneMode,
        sceneMode ? 12.0f : 2.5f
    );

    ConfigureCharacterLighting(scene);
    if (sceneMode)
    {
        const glm::vec3 center = (sceneMinimum + sceneMaximum) * 0.5f;
        const float radius =
            glm::length(sceneMaximum - sceneMinimum) * 0.5f;
        const float distance = std::max(1.0f, radius * 1.4f);
        scene.ActiveCamera().SetParam(CameraParam{
            .Position = center + glm::vec3(0.0f, radius * 0.5f, distance),
            .Target = center,
            .Up = {0.0f, 1.0f, 0.0f}
        });
    }
    const std::u8string modelPathU8 = modelPath.u8string();
    const std::string modelPathNarrow(
        reinterpret_cast<const char*>(modelPathU8.data()),
        modelPathU8.size()
    );
    std::cout << "[INFO] "
              << (sceneMode ? "MMD SCENE demo" : "MMD DREAM WINGS demo")
              << ": meshes="
              << model.Parts().size()
              << " model=" << modelPathNarrow
              << " motion="
              << (motionPath.empty() ? "none" : ToNarrowUtf8(motionPath))
              << " camera="
              << (cameraLoaded ? "loaded" : "missing")
              << " physicsFps="
              << (1.0f / physicsSettings.fixedTimeStep)
              << " maxSubSteps=" << physicsSettings.maxSubSteps
              << std::endl;
}
}  // namespace wisteria
