#include "pch.hpp"
#include "demo_scene.hpp"
#include "behaviour.hpp"
#include "manager.hpp"
#include "scene.hpp"
#include "window.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <string_view>

namespace
{
std::filesystem::path DemoModelPath1()
{
    return std::filesystem::current_path() / "assets" / "models" /
        "mmd" / u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
}

std::filesystem::path DemoModelPath2()
{
    return std::filesystem::current_path() / "assets" / "models" /
        "mmd" / u8"叶瞬光皮肤_pmx" / u8"叶瞬光.pmx";
}

BoneIndex FindDemoAnimationBone(const Skeleton& skeleton)
{
    // Prefer a small, unmistakable articulated movement. These are standard
    // MMD bone names; the root fallback keeps the demo useful for other rigs.
    constexpr std::array<std::string_view, 6> Candidates{
        "頭",
        "首",
        "上半身2",
        "上半身",
        "全ての親",
        "センター"
    };
    for (std::string_view name : Candidates)
    {
        if (const std::optional<BoneIndex> index = skeleton.FindBone(name))
            return *index;
    }

    if (skeleton.BoneCount() == 0)
        throw std::logic_error("Demo animation requires a non-empty Skeleton");
    return 0U;
}

class DemoAnimationParameterBehaviour final : public Behaviour
{
public:
    void Update(Entity& entity, float deltaTime) override
    {
        this->elapsed += deltaTime;
        if (this->elapsed < SwitchInterval)
            return;

        this->elapsed = std::fmod(this->elapsed, SwitchInterval);
        Animator& animator = entity.GetAnimator();
        animator.SetBool(
            "demoNod",
            !animator.GetBool("demoNod")
        );
    }

private:
    static constexpr float SwitchInterval = 3.0f;

    float elapsed = 0.0f;
};

class DemoBlinkBehaviour final : public Behaviour
{
public:
    explicit DemoBlinkBehaviour(MorphIndex morphIndex)
        : morphIndex(morphIndex)
    {
    }

    void Update(Entity& entity, float deltaTime) override
    {
        this->elapsed = std::fmod(this->elapsed + deltaTime, 4.0f);
        float weight = 0.0f;
        if (this->elapsed < 0.12f)
            weight = this->elapsed / 0.12f;
        else if (this->elapsed < 0.24f)
            weight = 1.0f - (this->elapsed - 0.12f) / 0.12f;
        entity.GetMorphState().SetWeight(this->morphIndex, weight);
    }

private:
    MorphIndex morphIndex = InvalidMorphIndex;
    float elapsed = 0.0f;
};

void EnableDemoBlink(Entity& entity, const ModelAsset& model)
{
    if (!entity.HasMorphState() || !model.HasMorphs())
        return;
    constexpr std::array<std::string_view, 5> Candidates{
        "まばたき",
        "瞬き",
        "眨眼",
        "blink",
        "Blink"
    };
    for (std::string_view name : Candidates)
    {
        if (const std::optional<MorphIndex> index =
                model.GetMorphSet().FindMorph(name))
        {
            entity.AddBehaviour<DemoBlinkBehaviour>(*index);
            std::cout << "[INFO] Demo blink uses morph: "
                      << model.GetMorphSet().DefinitionAt(*index).name
                      << std::endl;
            return;
        }
    }
}

void EnsureDemoAnimation(ModelAsset& model)
{
    if (!model.HasSkeleton() || model.AnimationClipCount() != 0)
        return;

    const Skeleton& skeleton = model.GetSkeleton();
    const BoneIndex boneIndex = FindDemoAnimationBone(skeleton);
    const BoneTransform bindTransform = BoneTransform::FromMatrix(
        skeleton.BoneAt(boneIndex).bindLocalMatrix
    );
    const auto rotated = [&bindTransform](
        float degrees,
        const glm::vec3& axis
    )
    {
        return glm::normalize(
            bindTransform.rotation * glm::angleAxis(
                glm::radians(degrees),
                axis
            )
        );
    };

    model.AddAnimationClip(AnimationClip(
        "demoHeadTurn",
        4.0f,
        {AnimationTrack(
            boneIndex,
            {},
            {
                QuaternionKeyframe{0.0f, bindTransform.rotation},
                QuaternionKeyframe{
                    1.0f,
                    rotated(25.0f, glm::vec3(0.0f, 1.0f, 0.0f))
                },
                QuaternionKeyframe{2.0f, bindTransform.rotation},
                QuaternionKeyframe{
                    3.0f,
                    rotated(-25.0f, glm::vec3(0.0f, 1.0f, 0.0f))
                },
                QuaternionKeyframe{4.0f, bindTransform.rotation}
            }
        )}
    ));
    model.AddAnimationClip(AnimationClip(
        "demoHeadNod",
        4.0f,
        {AnimationTrack(
            boneIndex,
            {},
            {
                QuaternionKeyframe{0.0f, bindTransform.rotation},
                QuaternionKeyframe{
                    1.0f,
                    rotated(16.0f, glm::vec3(1.0f, 0.0f, 0.0f))
                },
                QuaternionKeyframe{2.0f, bindTransform.rotation},
                QuaternionKeyframe{
                    3.0f,
                    rotated(-12.0f, glm::vec3(1.0f, 0.0f, 0.0f))
                },
                QuaternionKeyframe{4.0f, bindTransform.rotation}
            }
        )}
    ));

    std::cout << "[INFO] Demo animation uses bone: "
              << skeleton.BoneAt(boneIndex).name << std::endl;
}

void EnableDemoStateMachine(Entity& entity, const ModelAsset& model)
{
    const AnimationClip* headTurn =
        model.FindAnimationClip("demoHeadTurn");
    const AnimationClip* headNod =
        model.FindAnimationClip("demoHeadNod");
    if (headTurn != nullptr && headNod != nullptr)
    {
        Animator& animator = entity.GetAnimator();
        animator.SetBool("demoNod", false);

        AnimationStateMachine& stateMachine = animator.GetStateMachine();
        stateMachine.AddState(AnimationState{
            "HeadTurn",
            headTurn,
            1.0f,
            true
        });
        stateMachine.AddState(AnimationState{
            "HeadNod",
            headNod,
            1.0f,
            true
        });
        stateMachine.AddTransition(AnimationTransitionRule{
            "HeadTurn",
            "HeadNod",
            1.0f,
            [](const Animator& currentAnimator)
            {
                return currentAnimator.GetBool("demoNod");
            }
        });
        stateMachine.AddTransition(AnimationTransitionRule{
            "HeadNod",
            "HeadTurn",
            1.0f,
            [](const Animator& currentAnimator)
            {
                return !currentAnimator.GetBool("demoNod");
            }
        });
        stateMachine.SetState("HeadTurn");
        entity.AddBehaviour<DemoAnimationParameterBehaviour>();
    }
}


enum class MorphLabStage : std::size_t
{
    Vertex,
    Bone,
    Uv,
    Material,
    Group,
    Flip,
    Impulse,
    Count
};

constexpr MorphIndex MorphLabVertex = 0U;
constexpr MorphIndex MorphLabBone = 1U;
constexpr MorphIndex MorphLabUv = 2U;
constexpr MorphIndex MorphLabMaterial = 3U;
constexpr MorphIndex MorphLabGroup = 4U;
constexpr MorphIndex MorphLabFlip = 5U;
constexpr MorphIndex MorphLabImpulse = 6U;
constexpr float MorphLabStageDuration = 4.5f;

const char* MorphLabStageName(MorphLabStage stage) noexcept
{
    switch (stage)
    {
    case MorphLabStage::Vertex: return "Vertex Morph";
    case MorphLabStage::Bone: return "Bone Morph";
    case MorphLabStage::Uv: return "UV Morph";
    case MorphLabStage::Material: return "Material Morph";
    case MorphLabStage::Group: return "Group Morph";
    case MorphLabStage::Flip: return "PMX 2.1 Flip Morph";
    case MorphLabStage::Impulse: return "PMX 2.1 Impulse Morph (physics pending)";
    case MorphLabStage::Count: break;
    }
    return "Unknown Morph";
}

float SmoothStep(float value) noexcept
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

float MorphLabPulse(float elapsed) noexcept
{
    constexpr float FadeDuration = 0.65f;
    constexpr float FadeOutStart = MorphLabStageDuration - FadeDuration;
    if (elapsed < FadeDuration)
        return SmoothStep(elapsed / FadeDuration);
    if (elapsed > FadeOutStart)
    {
        return 1.0f - SmoothStep(
            (elapsed - FadeOutStart) / FadeDuration
        );
    }
    return 1.0f;
}

std::vector<std::uint8_t> CreateMorphLabCheckerPixels()
{
    constexpr int Width = 64;
    constexpr int Height = 64;
    constexpr int Cell = 8;
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(Width * Height * 4),
        255U
    );
    for (int y = 0; y < Height; ++y)
    {
        for (int x = 0; x < Width; ++x)
        {
            const bool alternate = ((x / Cell) + (y / Cell)) % 2 != 0;
            const std::array<std::uint8_t, 4> color = alternate
                ? std::array<std::uint8_t, 4>{55U, 35U, 85U, 255U}
                : std::array<std::uint8_t, 4>{220U, 190U, 255U, 255U};
            const std::size_t offset = static_cast<std::size_t>(
                (y * Width + x) * 4
            );
            std::copy(color.begin(), color.end(), pixels.begin() + offset);
        }
    }
    return pixels;
}

DefaultModelData CreateMorphLabMeshData()
{
    DefaultModelData data;
    data.layout = {
        {"position", 3, FLOAT, false, false, 0U},
        {"color", 3, FLOAT, false, false, 1U},
        {"texCoord", 2, FLOAT, false, false, 2U},
        {"normal", 3, FLOAT, false, false, 3U},
        {"tangent", 4, FLOAT, false, false, 4U},
        {"additionalTexCoord", 2, FLOAT, false, false, 5U},
        {"edgeScale", 1, FLOAT, false, false, 6U},
        {"boneIndices", 4, FLOAT, false, false, 7U},
        {"boneWeights", 4, FLOAT, false, false, 8U}
    };

    constexpr std::array<float, 3> X{-0.8f, 0.0f, 0.8f};
    constexpr std::array<float, 3> Y{-1.2f, 0.0f, 1.2f};
    data.vertices.reserve(9U * 26U);
    for (std::size_t row = 0; row < Y.size(); ++row)
    {
        for (std::size_t column = 0; column < X.size(); ++column)
        {
            const float u = static_cast<float>(column);
            const float v = static_cast<float>(row) * 1.5f;
            const float tipWeight = static_cast<float>(row) * 0.5f;
            const float rootWeight = 1.0f - tipWeight;
            const std::array<float, 26> vertex{
                X[column], Y[row], 0.0f,
                1.0f, 1.0f, 1.0f,
                u, v,
                0.0f, 0.0f, 1.0f,
                1.0f, 0.0f, 0.0f, 1.0f,
                u, v,
                1.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                rootWeight, tipWeight, 0.0f, 0.0f
            };
            data.vertices.insert(
                data.vertices.end(),
                vertex.begin(),
                vertex.end()
            );
        }
    }

    for (unsigned int row = 0U; row < 2U; ++row)
    {
        for (unsigned int column = 0U; column < 2U; ++column)
        {
            const unsigned int lowerLeft = row * 3U + column;
            const unsigned int lowerRight = lowerLeft + 1U;
            const unsigned int upperLeft = lowerLeft + 3U;
            const unsigned int upperRight = upperLeft + 1U;
            data.indices.insert(
                data.indices.end(),
                {
                    lowerLeft, lowerRight, upperRight,
                    upperRight, upperLeft, lowerLeft
                }
            );
        }
    }
    return data;
}

std::vector<MeshMorphTarget> CreateMorphLabMeshTargets()
{
    MeshMorphTarget vertex;
    vertex.morphIndex = MorphLabVertex;
    vertex.offsets = {
        VertexMorphOffset{3U, {-0.18f, 0.0f, 0.10f}},
        VertexMorphOffset{4U, {0.0f, 0.0f, 0.32f}},
        VertexMorphOffset{5U, {0.18f, 0.0f, 0.10f}},
        VertexMorphOffset{6U, {-0.38f, 0.12f, 0.0f}},
        VertexMorphOffset{7U, {0.0f, 0.48f, 0.15f}},
        VertexMorphOffset{8U, {0.38f, 0.12f, 0.0f}}
    };

    MeshMorphTarget uv;
    uv.morphIndex = MorphLabUv;
    for (std::uint32_t vertexIndex = 0U; vertexIndex < 9U; ++vertexIndex)
    {
        uv.uvOffsets.push_back(UvMorphOffset{
            vertexIndex,
            0U,
            glm::vec4(0.85f, 0.35f, 0.0f, 0.0f)
        });
    }
    return {std::move(vertex), std::move(uv)};
}

Skeleton CreateMorphLabSkeleton()
{
    Bone root;
    root.name = "MorphLabRoot";
    root.parentIndex = InvalidBoneIndex;
    root.bindLocalMatrix = glm::mat4(1.0f);
    root.inverseBindMatrix = glm::mat4(1.0f);

    Bone tip;
    tip.name = "MorphLabTip";
    tip.parentIndex = 0U;
    tip.bindLocalMatrix = glm::mat4(1.0f);
    tip.inverseBindMatrix = glm::mat4(1.0f);
    return Skeleton({std::move(root), std::move(tip)});
}

std::vector<MorphDefinition> CreateMorphLabDefinitions()
{
    MorphDefinition vertex;
    vertex.name = "Lab Vertex";
    vertex.kind = MorphKind::Vertex;

    MorphDefinition bone;
    bone.name = "Lab Bone";
    bone.kind = MorphKind::Bone;
    bone.boneOffsets.push_back(BoneMorphOffset{
        1U,
        glm::vec3(0.28f, 0.12f, 0.0f),
        glm::angleAxis(
            glm::radians(-28.0f),
            glm::vec3(0.0f, 0.0f, 1.0f)
        )
    });

    MorphDefinition uv;
    uv.name = "Lab UV";
    uv.kind = MorphKind::Uv;

    MorphDefinition material;
    material.name = "Lab Material";
    material.kind = MorphKind::Material;
    MaterialMorphOffset materialOffset;
    materialOffset.materialIndex = 0U;
    materialOffset.operation = MaterialMorphOperation::Add;
    materialOffset.diffuse = glm::vec4(0.48f, -0.38f, -0.18f, -0.30f);
    materialOffset.ambient = glm::vec3(0.18f, 0.02f, 0.12f);
    materialOffset.edgeColor = glm::vec4(0.65f, 0.12f, 0.55f, 0.0f);
    materialOffset.edgeSize = 1.8f;
    materialOffset.textureFactor = glm::vec4(0.15f, -0.25f, -0.10f, 0.0f);
    material.materialOffsets.push_back(materialOffset);

    MorphDefinition group;
    group.name = "Lab Group";
    group.kind = MorphKind::Group;
    group.groupMembers = {
        GroupMorphMember{MorphLabVertex, 0.65f},
        GroupMorphMember{MorphLabBone, 0.75f},
        GroupMorphMember{MorphLabUv, 0.55f},
        GroupMorphMember{MorphLabMaterial, 0.70f}
    };

    MorphDefinition flip;
    flip.name = "Lab Flip";
    flip.kind = MorphKind::Flip;
    flip.flipMembers = {
        FlipMorphMember{MorphLabVertex, 1.0f},
        FlipMorphMember{MorphLabMaterial, 1.0f},
        FlipMorphMember{MorphLabGroup, 1.0f}
    };

    MorphDefinition impulse;
    impulse.name = "Lab Impulse";
    impulse.kind = MorphKind::Impulse;
    impulse.impulseOffsets.push_back(ImpulseMorphOffset{
        0U,
        false,
        glm::vec3(1.5f, 2.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 0.8f)
    });

    return {
        std::move(vertex),
        std::move(bone),
        std::move(uv),
        std::move(material),
        std::move(group),
        std::move(flip),
        std::move(impulse)
    };
}

class MorphLabBehaviour final : public Behaviour
{
public:
    MorphLabBehaviour(Window& window, Input& input)
        : window(window), input(input)
    {
    }

    void Update(Entity& entity, float deltaTime) override
    {
        if (this->input.WasKeyPressed(InputKey::Space))
        {
            this->paused = !this->paused;
            this->titleDirty = true;
        }
        if (this->input.WasKeyPressed(InputKey::Right))
            this->Advance(1);
        if (this->input.WasKeyPressed(InputKey::Left))
            this->Advance(-1);

        if (!this->paused)
        {
            this->elapsed += deltaTime;
            while (this->elapsed >= MorphLabStageDuration)
            {
                this->elapsed -= MorphLabStageDuration;
                this->Advance(1);
            }
        }

        MorphState& morphState = entity.GetMorphState();
        std::vector<float> weights(morphState.MorphCount(), 0.0f);
        const float pulse = MorphLabPulse(this->elapsed);
        switch (this->stage)
        {
        case MorphLabStage::Vertex:
            weights[MorphLabVertex] = pulse;
            break;
        case MorphLabStage::Bone:
            weights[MorphLabBone] = pulse;
            break;
        case MorphLabStage::Uv:
            weights[MorphLabUv] = pulse;
            break;
        case MorphLabStage::Material:
            weights[MorphLabMaterial] = pulse;
            break;
        case MorphLabStage::Group:
            weights[MorphLabGroup] = pulse;
            break;
        case MorphLabStage::Flip:
            weights[MorphLabFlip] = this->FlipControlWeight();
            break;
        case MorphLabStage::Impulse:
            weights[MorphLabImpulse] = pulse;
            break;
        case MorphLabStage::Count:
            break;
        }
        morphState.SetWeights(weights);

        if (this->titleDirty)
        {
            this->UpdateTitle(morphState);
            this->titleDirty = false;
        }
    }

private:
    void Advance(int direction)
    {
        const int count = static_cast<int>(MorphLabStage::Count);
        int index = static_cast<int>(this->stage) + direction;
        index %= count;
        if (index < 0)
            index += count;
        this->stage = static_cast<MorphLabStage>(index);
        this->elapsed = 0.0f;
        this->titleDirty = true;
    }

    float FlipControlWeight() const noexcept
    {
        if (this->elapsed < 0.45f || this->elapsed >= 4.1f)
            return 0.10f;
        if (this->elapsed < 1.55f)
            return 0.30f;
        if (this->elapsed < 2.65f)
            return 0.55f;
        return 0.80f;
    }

    void UpdateTitle(const MorphState& morphState)
    {
        std::string title = "FLORAL WISTERIA - MORPH LAB - ";
        title += MorphLabStageName(this->stage);
        title += this->paused ? " [PAUSED]" : "";
        title += " | Left/Right: switch | Space: pause";
        this->window.SetTitle(std::move(title));

        std::cout << "[MORPH LAB] " << MorphLabStageName(this->stage);
        if (this->stage == MorphLabStage::Impulse)
        {
            std::vector<MmdRigidBodyImpulse> impulses;
            std::vector<float> weights(morphState.MorphCount(), 0.0f);
            weights[MorphLabImpulse] = 1.0f;
            morphState.GetMorphSet().EvaluateImpulseMorphs(
                weights,
                impulses
            );
            if (!impulses.empty())
            {
                const MmdRigidBodyImpulse& command = impulses.front();
                std::cout << " -> rigidBody=" << command.rigidBodyIndex
                          << " globalLinear=("
                          << command.globalLinearImpulse.x << ", "
                          << command.globalLinearImpulse.y << ", "
                          << command.globalLinearImpulse.z << ")";
            }
        }
        std::cout << std::endl;
    }

    Window& window;
    Input& input;
    MorphLabStage stage = MorphLabStage::Vertex;
    float elapsed = 0.0f;
    bool paused = false;
    bool titleDirty = true;
};

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
    EnsureDemoAnimation(Model);
    Entity& Entity = scene.InstantiateModel(
        Model,
        Transform(
            glm::vec3(0.0f, 0.0f, 0.1f),
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        )
    );

    EnableDemoStateMachine(Entity, Model);
    EnableDemoBlink(Entity, Model);
    Entity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f));

    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {0.0f, 16.1f, 10.5f},
        .Target = {0.0f, 16.1f, 0.25f},
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
    EnsureDemoAnimation(Model);
    Entity& Entity = scene.InstantiateModel(
        Model,
        Transform(
            glm::vec3(0.0f, 0.0f, 0.1f),
            glm::vec3(0.0f),
            glm::vec3(1.0f)
        )
    );

    EnableDemoStateMachine(Entity, Model);
    EnableDemoBlink(Entity, Model);
    Entity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f));

    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {0.0f, 16.1f, 10.5f},
        .Target = {0.0f, 16.1f, 0.25f},
        .Up = {0.0f, 1.0f, 0.0f}
    });
    scene.CreatePointLight(PointLightData{
        .Position = {2.5f, 1.5f, 2.5f},
        .Color = {1.0f, 1.0f, 1.0f},
        .Intensity = 1.6f,
        .Range = 8.0f
    });
}


ModelAsset& CreateMorphLabModel(ResourceManager& resources)
{
    if (ModelAsset* existing = resources.FindModel("morphLab"))
        return *existing;

    Mesh& mesh = resources.CreateMesh(
        "morphLab::mesh",
        CreateMorphLabMeshData(),
        2U,
        CreateMorphLabMeshTargets()
    );

    MaterialData materialData;
    const std::filesystem::path shaderDirectory =
        std::filesystem::current_path() / "assets" / "shaders";
    materialData.shaderFilePath.VertexPath =
        (shaderDirectory / "mmd.vert").string();
    materialData.shaderFilePath.FragmentPath =
        (shaderDirectory / "mmd.frag").string();
    materialData.shadingModel = MaterialShadingModel::MmdToon;
    materialData.shaderInterface.imageBasedLightingEnabled = false;
    materialData.textureSources = {
        {
            "texture",
            TextureData::FromRgba8(
                64,
                64,
                CreateMorphLabCheckerPixels()
            )
        }
    };
    materialData.baseColorFactor = glm::vec4(0.42f, 0.70f, 0.95f, 1.0f);
    materialData.specularColor = glm::vec3(0.45f);
    materialData.shininess = 20.0f;
    materialData.ambientColor = glm::vec3(0.22f, 0.18f, 0.28f);
    materialData.edgeEnabled = true;
    materialData.edgeColor = glm::vec4(0.08f, 0.03f, 0.12f, 1.0f);
    materialData.edgeSize = 1.1f;
    materialData.doubleSided = true;
    Material& material = resources.CreateMaterial(
        "morphLab::material",
        materialData
    );

    ModelAsset& model = resources.CreateModel("morphLab");
    model.SetSkeleton(CreateMorphLabSkeleton());
    model.SetMorphs(CreateMorphLabDefinitions());
    model.SetMmdRigidBodyCount(1U);
    model.AddPart(mesh, material, glm::mat4(1.0f), 0U);
    return model;
}

void SetupMorphDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window
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

    ModelAsset& model = CreateMorphLabModel(resources);
    scene.InstantiateModel(
        model,
        Transform(
            glm::vec3(-1.25f, 0.0f, 0.0f),
            glm::vec3(0.0f),
            glm::vec3(0.92f)
        )
    );
    Entity& active = scene.InstantiateModel(
        model,
        Transform(
            glm::vec3(1.25f, 0.0f, 0.0f),
            glm::vec3(0.0f),
            glm::vec3(0.92f)
        )
    );
    active.AddBehaviour<MorphLabBehaviour>(window, window.GetInput());

    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {0.0f, 0.0f, 6.2f},
        .Target = {0.0f, 0.0f, 0.0f},
        .Up = {0.0f, 1.0f, 0.0f},
        .VerticalFovDegrees = 42.0f
    });
    scene.CreatePointLight(PointLightData{
        .Position = {0.0f, 2.5f, 4.0f},
        .Color = {1.0f, 0.94f, 1.0f},
        .Intensity = 4.0f,
        .Range = 12.0f
    });
    scene.CreateDirectionalLight(DirectionalLightData{
        .Direction = {-0.4f, -0.7f, -1.0f},
        .Color = {0.45f, 0.50f, 0.75f},
        .Intensity = 0.8f
    });
}
