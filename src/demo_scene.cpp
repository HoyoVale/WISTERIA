#include "pch.hpp"
#include "demo_scene.hpp"
#include "behaviour.hpp"
#include "manager.hpp"
#include "scene.hpp"
#include <array>
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
