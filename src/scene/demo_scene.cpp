#include "wisteria/common/pch.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/core/asset_paths.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/assets/manager.hpp"
#include "wisteria/assets/saba_mmd_importer.hpp"
#include "wisteria/mmd/mmd_camera_conversion.hpp"
#include "wisteria/mmd/mmd_light_conversion.hpp"
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
#include <set>
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
        (alternate ? u8"叶瞬光皮肤_pmx" : u8"蕾米埃尔-黑") /
        u8"蕾米埃尔-黑.pmx";
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

bool ComputeModelWorldBounds(
    const ModelAsset& model,
    glm::vec3& minimum,
    glm::vec3& maximum
)
{
    bool hasGeometry = false;
    for (const RenderPart& part : model.Parts())
    {
        const DefaultModelData& data = part.GetMesh().Data();
        std::size_t stride = 0U;
        std::size_t positionOffset = 0U;
        bool hasPosition = false;
        for (const VertexAttribute& attribute : data.layout)
        {
            if (attribute.name == "position" &&
                attribute.format == VertexFormat::Float32)
            {
                positionOffset = stride;
                hasPosition = true;
            }
            stride += static_cast<std::size_t>(attribute.size);
        }
        if (!hasPosition || stride == 0U ||
            data.vertices.size() < stride)
        {
            continue;
        }

        const glm::mat4 localTransform = part.LocalTransform();
        const std::size_t vertexCount = data.vertices.size() / stride;
        for (std::size_t vertexIndex = 0U;
             vertexIndex < vertexCount;
             ++vertexIndex)
        {
            const std::size_t offset = vertexIndex * stride + positionOffset;
            const glm::vec3 localPosition(
                data.vertices[offset],
                data.vertices[offset + 1U],
                data.vertices[offset + 2U]
            );
            const glm::vec4 worldPosition =
                localTransform * glm::vec4(localPosition, 1.0f);
            if (!hasGeometry)
            {
                minimum = maximum = glm::vec3(worldPosition);
                hasGeometry = true;
            }
            else
            {
                minimum = glm::min(minimum, glm::vec3(worldPosition));
                maximum = glm::max(maximum, glm::vec3(worldPosition));
            }
        }
    }
    return hasGeometry;
}

void ValidateDemoModelFile(
    const std::filesystem::path& path,
    bool usedDefaultPath
)
{
    if (std::filesystem::is_regular_file(path))
        return;

    std::string message =
        "Demo model file not found: " + ToNarrowUtf8(path) + "\n";
    if (usedDefaultPath)
    {
        message +=
            "The repository does not ship demo models or motions.\n"
            "Prepare them first (see docs/ASSETS.md):\n"
            "  .\\script\\setup_demo_assets.ps1 -SourceRoot <assets-source>\n"
            "or start a dependency-free scene instead:\n"
            "  .\\run.ps1 run -ApplicationArguments '--ground-lab'";
    }
    else
    {
        message += "Check the --model / --scene path and try again.";
    }
    throw std::runtime_error(message);
}

DirectionalLight& ConfigureCharacterLighting(Scene& scene)
{
    // Main directional light also drives the CSM shadow map and the MMD
    // ground shadow projection.
    DirectionalLight& keyLight = scene.CreateDirectionalLight(
        DirectionalLightData{
        .Direction = {-0.35f, -0.75f, -0.45f},
        .Color = {1.0f, 0.96f, 0.92f},
        .Intensity = 0.75f
        }
    );
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
    return keyLight;
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

void MarkModelAsGroundShadowReceiver(ModelAsset& model)
{
    // Imported stages are not ground planes, but their floor should catch
    // the MMD ground shadow. Receivers are drawn before the shadow pass so
    // the flattened silhouette can depth-test against them.
    for (const RenderPart& part : model.Parts())
        part.GetMaterial().SetReceivesGroundShadow(true);
}
}

class SabaDemoBehaviour final : public Behaviour
{
public:
    SabaDemoBehaviour(
        SabaMmdRuntimeModel& runtime,
        Camera& camera,
        DirectionalLight& light,
        LightTrack lightTrack,
        Window& window,
        Input& input,
        bool sceneMode,
        float cameraSpeed
    )
        : runtime(&runtime),
          camera(&camera),
          light(&light),
          lightTrack(std::move(lightTrack)),
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

        // Scene already advanced the engine-owned ModelInstance before
        // behaviours run. The demo only applies optional presentation tracks.
        if (this->cameraFollowEnabled && !this->runtime->IsMotionPaused())
        {
            const std::optional<CameraTrackSample> sample =
                this->runtime->SampleCameraMotion(
                    static_cast<float>(this->runtime->MotionFrame())
                );
            if (sample.has_value())
            {
                // Preserve host camera settings the sample does not carry
                // (NearClip/FarClip and any other fields).
                this->camera->SetParam(ToCameraParam(
                    *sample,
                    this->camera->GetParam()
                ));
            }
        }

        // Programmatic light track: rotates the key light direction around Y
        // so the CSM/ground shadow direction visibly changes over time. The
        // demo owns the track; the backend never writes the light directly.
        {
            LightKeyframe lightSample;
            if (this->lightTrack.Sample(
                    static_cast<float>(this->runtime->MotionFrame()),
                    lightSample
                ))
            {
                const DirectionalLightData fallback{
                    .Direction = this->light->Direction(),
                    .Color = this->light->Color(),
                    .Intensity = this->light->Intensity()
                };
                const DirectionalLightData lightData = ToLightData(
                    LightTrackSample{
                        lightSample.time,
                        lightSample.color,
                        lightSample.position
                    },
                    fallback
                );
                this->light->SetDirection(lightData.Direction);
                this->light->SetColor(lightData.Color);
                this->light->SetIntensity(lightData.Intensity);
            }
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
    SabaMmdRuntimeModel* runtime = nullptr;
    Camera* camera = nullptr;
    DirectionalLight* light = nullptr;
    LightTrack lightTrack;
    Window* window = nullptr;
    Input* input = nullptr;
    bool sceneMode = false;
    bool cameraFollowEnabled = true;
    float cameraSpeed = 2.5f;
    float titleElapsed = 0.0f;
    bool titleDirty = true;
    std::size_t diagnosticCounter = 0U;
};
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

void SetupGenericGltfDemoScene(
    Scene& scene,
    ResourceManager& resources,
    std::filesystem::path modelPath
)
{
    if (modelPath.empty() ||
        !std::filesystem::is_regular_file(modelPath))
    {
        throw std::invalid_argument(
            "glTF demo requires an existing model file"
        );
    }

    EnvironmentMap* existingEnvironment =
        resources.FindEnvironment("defaultSky");
    EnvironmentMap& environment = existingEnvironment != nullptr
        ? *existingEnvironment
        : resources.CreateEnvironment(
            "defaultSky",
            EnvironmentMapData::ProceduralSky()
        );
    scene.SetEnvironment(&environment);

    (void)ConfigureCharacterLighting(scene);
    AddDemoGround(scene, resources);

    const std::u8string fileName = modelPath.filename().u8string();
    const std::string resourceName(
        reinterpret_cast<const char*>(fileName.data()),
        fileName.size()
    );
    ModelAsset& model = resources.LoadModel(
        "gltf::" + resourceName,
        modelPath
    );
    Entity& entity = scene.InstantiateModel(
        model,
        Transform(
            glm::vec3(0.0f, 0.0f, 0.1f),
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        )
    );
    MarkModelAsGroundShadowReceiver(model);

    glm::vec3 boundsMinimum{0.0f};
    glm::vec3 boundsMaximum{0.0f};
    if (ComputeModelWorldBounds(model, boundsMinimum, boundsMaximum))
    {
        const glm::vec3 boundsCenter =
            (boundsMinimum + boundsMaximum) * 0.5f;
        const float boundsRadius =
            glm::length(boundsMaximum - boundsMinimum) * 0.5f;
        const float cameraDistance = std::max(1.0f, boundsRadius * 2.6f);
        scene.ActiveCamera().SetParam(CameraParam{
            .Position = boundsCenter +
                glm::vec3(0.0f, boundsRadius * 0.25f, cameraDistance),
            .Target = boundsCenter,
            .Up = {0.0f, 1.0f, 0.0f}
        });
    }
    else
    {
        scene.ActiveCamera().SetParam(CameraParam{
            .Position = {3.0f, 2.0f, 5.0f},
            .Target = {0.0f, 1.0f, 0.0f},
            .Up = {0.0f, 1.0f, 0.0f}
        });
    }

    std::size_t skinnedMeshCount = 0U;
    std::size_t maximumRequiredBones = 0U;
    std::set<const Material*> uniqueMaterials;
    std::size_t opaqueMaterials = 0U;
    std::size_t maskMaterials = 0U;
    std::size_t blendMaterials = 0U;
    std::size_t baseColorTexturedMaterials = 0U;
    std::size_t normalTexturedMaterials = 0U;
    std::size_t metallicRoughnessTexturedMaterials = 0U;
    std::size_t emissiveTexturedMaterials = 0U;
    std::size_t occlusionTexturedMaterials = 0U;
    std::size_t doubleSidedMaterials = 0U;
    for (const RenderPart& part : model.Parts())
    {
        const Mesh& mesh = part.GetMesh();
        if (mesh.IsSkinned())
        {
            ++skinnedMeshCount;
            maximumRequiredBones = std::max(
                maximumRequiredBones,
                mesh.RequiredBoneCount()
            );
        }
        const Material& material = part.GetMaterial();
        if (uniqueMaterials.emplace(&material).second)
        {
            switch (material.AlphaMode())
            {
            case MaterialAlphaMode::Opaque:
                ++opaqueMaterials;
                break;
            case MaterialAlphaMode::Mask:
                ++maskMaterials;
                break;
            case MaterialAlphaMode::Blend:
                ++blendMaterials;
                break;
            }
            baseColorTexturedMaterials += material.HasTexture(
                "baseColorTexture") ? 1U : 0U;
            normalTexturedMaterials += material.HasTexture(
                "normalTexture") ? 1U : 0U;
            metallicRoughnessTexturedMaterials += material.HasTexture(
                "metallicRoughnessTexture") ? 1U : 0U;
            emissiveTexturedMaterials += material.HasTexture(
                "emissiveTexture") ? 1U : 0U;
            occlusionTexturedMaterials += material.HasTexture(
                "occlusionTexture") ? 1U : 0U;
            doubleSidedMaterials += material.IsDoubleSided() ? 1U : 0U;
        }
    }

    const ModelInstance* instance = entity.TryGetModelInstance();
    const IModelRuntimeDriver* runtime =
        instance != nullptr ? instance->TryGetRuntime() : nullptr;
    std::cout << "[INFO] GLTF demo: meshes=" << model.Parts().size()
              << " backend="
              << (runtime != nullptr ? runtime->BackendName() : "static")
              << " bones="
              << (model.HasSkeleton()
                    ? model.GetSkeleton().BoneCount()
                    : 0U)
              << " skinnedMeshes=" << skinnedMeshCount
              << " maxBonesPerMesh=" << maximumRequiredBones
              << " morphs="
              << (model.HasMorphs()
                    ? model.GetMorphSet().MorphCount()
                    : 0U)
              << " clips=" << model.AnimationClipCount()
              << " materials=" << uniqueMaterials.size()
              << " alpha=[opaque=" << opaqueMaterials
              << ",mask=" << maskMaterials
              << ",blend=" << blendMaterials << "]"
              << " textures=[base=" << baseColorTexturedMaterials
              << ",normal=" << normalTexturedMaterials
              << ",metalRough=" << metallicRoughnessTexturedMaterials
              << ",emissive=" << emissiveTexturedMaterials
              << ",occlusion=" << occlusionTexturedMaterials << "]"
              << " doubleSided=" << doubleSidedMaterials
              << " model=" << ToNarrowUtf8(modelPath) << std::endl;
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

    const bool usedDefaultModelPath = modelPath.empty();
    if (modelPath.empty())
        modelPath = DemoModelPath(alternateModel);
    ValidateDemoModelFile(modelPath, usedDefaultModelPath);
    const std::u8string modelFileName = modelPath.filename().u8string();
    const std::string modelResourceName(
        reinterpret_cast<const char*>(modelFileName.data()),
        modelFileName.size()
    );
    const bool usedDefaultScenePath = scenePath.empty();
    if (scenePath.empty())
        scenePath = DemoScenePath(alternateModel);
    if (sceneMode)
        ValidateDemoModelFile(scenePath, usedDefaultScenePath);
    const std::u8string sceneFileName = scenePath.filename().u8string();
    const std::string sceneResourceName(
        reinterpret_cast<const char*>(sceneFileName.data()),
        sceneFileName.size()
    );
    glm::vec3 sceneMinimum{-20.0f, 0.0f, -20.0f};
    glm::vec3 sceneMaximum{20.0f, 40.0f, 20.0f};
    ModelAsset& model = resources.LoadModel(
        "saba::" + modelResourceName,
        modelPath
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
        ModelAsset& sceneModel = resources.LoadModel(
            "saba::" + sceneResourceName,
            scenePath
        );
        scene.InstantiateModel(
            sceneModel,
            Transform(
                glm::vec3(0.0f, 0.0f, 50.1f),
                glm::vec3(0.0f),
                glm::vec3(1.0f)
            )
        );
        MarkModelAsGroundShadowReceiver(sceneModel);
    }
    else
    {
        // Character demo: stand the character on a visible ground plane so
        // the MMD ground shadow has a surface to land on. No stage backdrop:
        // the composition is character + chessboard ground only.
        AddDemoGround(scene, resources);
    }
    const bool usedDefaultMotionPath = motionPath.empty();
    if (motionPath.empty() && !sceneMode)
        motionPath = DemoDreamWingMotionPath();
    if (!motionPath.empty() && !std::filesystem::is_regular_file(motionPath))
    {
        if (usedDefaultMotionPath)
        {
            std::cerr << "[WARN] Demo VMD not found, continuing without "
                         "motion: "
                      << ToNarrowUtf8(motionPath) << "\n"
                      << "       Prepare assets with "
                         "script\\setup_demo_assets.ps1 (docs/ASSETS.md).\n";
            motionPath.clear();
        }
        else
        {
            throw std::runtime_error(
                "Motion file not found: " + ToNarrowUtf8(motionPath)
            );
        }
    }
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
    ModelInstance* modelInstance = entity.TryGetModelInstance();
    if (modelInstance == nullptr)
        throw std::runtime_error("Demo entity has no ModelInstance");
    auto* runtime = dynamic_cast<SabaMmdRuntimeModel*>(
        modelInstance->TryGetRuntime()
    );
    if (runtime == nullptr)
        throw std::runtime_error("Demo PMX did not select Saba MMD backend");
    runtime->SetPhysicsSettings(physicsSettings);
    if (!motionPath.empty() && !runtime->LoadMotion(motionPath))
        throw std::runtime_error("Saba demo motion failed to load");
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

    DirectionalLight& keyLight = ConfigureCharacterLighting(scene);

    // Programmatic light track: rotates the key light direction around Y so
    // the CSM/ground shadow direction changes over the motion. The demo owns
    // the track; WISTERIA's application layer converts samples to host light
    // data (ToLightData) — the backend never writes the light directly.
    LightTrack demoLightTrack({
        LightKeyframe{
            0.0f,
            {1.0f, 0.96f, 0.92f},
            {0.9f, 1.0f, 1.1f},
            {}
        },
        LightKeyframe{
            300.0f,
            {1.0f, 0.96f, 0.92f},
            {-0.9f, 1.0f, -0.4f},
            {}
        },
        LightKeyframe{
            600.0f,
            {1.0f, 0.96f, 0.92f},
            {0.2f, 1.0f, -1.0f},
            {}
        }
    });

    entity.AddBehaviour<SabaDemoBehaviour>(
        *runtime,
        scene.ActiveCamera(),
        keyLight,
        std::move(demoLightTrack),
        window,
        window.GetInput(),
        sceneMode,
        sceneMode ? 12.0f : 2.5f
    );
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
