#include "wisteria/common/pch.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/assets/manager.hpp"
#include "wisteria/assets/saba_mmd_importer.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"
#include "wisteria/scene/scene.hpp"
#include "wisteria/platform/window.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>

#ifndef WISTERIA_BUILD_CONFIGURATION
#define WISTERIA_BUILD_CONFIGURATION "Unknown"
#endif

namespace
{
constexpr float FullBodyActionDuration = 8.0f;
constexpr std::string_view FullBodyActionName = "demoFullBodyAction";

std::filesystem::path DemoModelPath(bool alternate)
{
    return std::filesystem::current_path() / "assets" / "models" /
        "mmd" /
        (alternate ? u8"叶瞬光皮肤_pmx" : u8"叶瞬光_pmx") /
        u8"叶瞬光.pmx";
}

std::filesystem::path DemoMotionPath1()
{
    return std::filesystem::current_path() / "assets" / "motions" /
        u8"皮卡皮卡皮卡丘+" / u8"身体动作.vmd";
}

std::optional<BoneIndex> FindBone(
    const Skeleton& skeleton,
    std::initializer_list<std::string_view> names
)
{
    for (std::string_view name : names)
    {
        if (const std::optional<BoneIndex> index = skeleton.FindBone(name))
            return index;
    }
    return std::nullopt;
}

std::optional<MorphIndex> FindMorph(
    const MorphSet& morphs,
    std::initializer_list<std::string_view> names
)
{
    for (std::string_view name : names)
    {
        if (const std::optional<MorphIndex> index = morphs.FindMorph(name))
            return index;
    }
    return std::nullopt;
}

struct DemoPoseKey
{
    float time = 0.0f;
    glm::vec3 translationDelta{0.0f};
    glm::vec3 rotationDegrees{0.0f};
};

AnimationTrack MakeDemoTrack(
    const Skeleton& skeleton,
    BoneIndex boneIndex,
    std::initializer_list<DemoPoseKey> keys
)
{
    const BoneTransform bind = BoneTransform::FromMatrix(
        skeleton.BoneAt(boneIndex).bindLocalMatrix
    );
    std::vector<VectorKeyframe> translations;
    std::vector<QuaternionKeyframe> rotations;
    translations.reserve(keys.size());
    rotations.reserve(keys.size());
    glm::quat previous = bind.rotation;
    for (const DemoPoseKey& key : keys)
    {
        glm::quat rotation = glm::normalize(
            bind.rotation * glm::quat(glm::radians(key.rotationDegrees))
        );
        if (glm::dot(previous, rotation) < 0.0f)
            rotation = -rotation;
        previous = rotation;
        translations.push_back(VectorKeyframe{
            key.time,
            bind.translation + key.translationDelta
        });
        rotations.push_back(QuaternionKeyframe{key.time, rotation});
    }
    return AnimationTrack(
        boneIndex,
        std::move(translations),
        std::move(rotations)
    );
}

void AddDemoTrack(
    std::vector<AnimationTrack>& tracks,
    const Skeleton& skeleton,
    std::initializer_list<std::string_view> names,
    std::initializer_list<DemoPoseKey> keys
)
{
    if (const std::optional<BoneIndex> bone = FindBone(skeleton, names))
        tracks.push_back(MakeDemoTrack(skeleton, *bone, keys));
}

AnimationClip BuildFullBodyAction(ModelAsset& model)
{
    if (!model.HasSkeleton())
        throw std::logic_error("Full-body MMD demo requires a Skeleton");

    const Skeleton& skeleton = model.GetSkeleton();
    std::vector<AnimationTrack> tracks;
    tracks.reserve(16U);

    AddDemoTrack(tracks, skeleton, {"全ての親"}, {
        {0.0f},
        {2.0f, {}, {0.0f, 5.0f, 0.0f}},
        {4.0f},
        {6.0f, {}, {0.0f, -5.0f, 0.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"センター"}, {
        {0.0f},
        {1.0f, {-0.18f, 0.08f, 0.0f}},
        {2.0f, {-0.34f, 0.16f, 0.04f}},
        {3.0f, {0.0f, 0.03f, 0.0f}},
        {4.0f, {0.30f, 0.14f, -0.04f}},
        {5.0f, {0.12f, 0.07f, 0.0f}},
        {6.0f, {-0.12f, 0.18f, 0.05f}},
        {7.0f, {0.08f, 0.05f, 0.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"グルーブ"}, {
        {0.0f},
        {1.0f, {0.0f, 0.08f, 0.0f}},
        {2.0f, {0.0f, -0.04f, 0.0f}},
        {3.0f, {0.0f, 0.12f, 0.0f}},
        {4.0f, {0.0f, -0.02f, 0.0f}},
        {5.0f, {0.0f, 0.10f, 0.0f}},
        {6.0f, {0.0f, -0.03f, 0.0f}},
        {7.0f, {0.0f, 0.08f, 0.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"下半身"}, {
        {0.0f},
        {2.0f, {}, {0.0f, -10.0f, 2.0f}},
        {4.0f, {}, {0.0f, 12.0f, -2.0f}},
        {6.0f, {}, {0.0f, -8.0f, 1.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"上半身"}, {
        {0.0f},
        {1.0f, {}, {-4.0f, 7.0f, 4.0f}},
        {2.0f, {}, {3.0f, 14.0f, 7.0f}},
        {3.0f, {}, {-3.0f, 2.0f, -3.0f}},
        {4.0f, {}, {2.0f, -15.0f, -7.0f}},
        {5.0f, {}, {-4.0f, -7.0f, 4.0f}},
        {6.0f, {}, {3.0f, 11.0f, 6.0f}},
        {7.0f, {}, {-2.0f, -3.0f, -3.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"上半身2", "上半身1"}, {
        {0.0f},
        {2.0f, {}, {-5.0f, 9.0f, 4.0f}},
        {4.0f, {}, {4.0f, -10.0f, -4.0f}},
        {6.0f, {}, {-3.0f, 8.0f, 3.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"首"}, {
        {0.0f},
        {2.0f, {}, {4.0f, -8.0f, -3.0f}},
        {4.0f, {}, {-5.0f, 10.0f, 3.0f}},
        {6.0f, {}, {3.0f, -6.0f, -2.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"頭"}, {
        {0.0f},
        {1.0f, {}, {-5.0f, -10.0f, 0.0f}},
        {2.0f, {}, {8.0f, -18.0f, -3.0f}},
        {3.0f, {}, {-3.0f, 0.0f, 0.0f}},
        {4.0f, {}, {6.0f, 20.0f, 3.0f}},
        {5.0f, {}, {-7.0f, 8.0f, 0.0f}},
        {6.0f, {}, {5.0f, -15.0f, -2.0f}},
        {7.0f, {}, {-3.0f, 3.0f, 0.0f}},
        {8.0f}
    });

    AddDemoTrack(tracks, skeleton, {"左肩"}, {
        {0.0f}, {2.0f, {}, {4.0f, 0.0f, -8.0f}},
        {4.0f, {}, {-3.0f, 0.0f, 6.0f}},
        {6.0f, {}, {4.0f, 0.0f, -7.0f}}, {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"右肩"}, {
        {0.0f}, {2.0f, {}, {-4.0f, 0.0f, 8.0f}},
        {4.0f, {}, {3.0f, 0.0f, -6.0f}},
        {6.0f, {}, {-4.0f, 0.0f, 7.0f}}, {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"左腕"}, {
        {0.0f},
        {1.0f, {}, {-18.0f, -5.0f, -18.0f}},
        {2.0f, {}, {-38.0f, -12.0f, -32.0f}},
        {3.0f, {}, {-8.0f, 8.0f, -14.0f}},
        {4.0f, {}, {22.0f, 12.0f, -5.0f}},
        {5.0f, {}, {-30.0f, -6.0f, -28.0f}},
        {6.0f, {}, {-8.0f, 10.0f, -12.0f}},
        {7.0f, {}, {15.0f, 4.0f, -8.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"右腕"}, {
        {0.0f},
        {1.0f, {}, {18.0f, 5.0f, 18.0f}},
        {2.0f, {}, {8.0f, -8.0f, 14.0f}},
        {3.0f, {}, {36.0f, 12.0f, 30.0f}},
        {4.0f, {}, {-22.0f, -12.0f, 5.0f}},
        {5.0f, {}, {8.0f, -10.0f, 12.0f}},
        {6.0f, {}, {32.0f, 6.0f, 28.0f}},
        {7.0f, {}, {-15.0f, -4.0f, 8.0f}},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"左ひじ"}, {
        {0.0f}, {2.0f, {}, {0.0f, 0.0f, -38.0f}},
        {4.0f, {}, {0.0f, 0.0f, -12.0f}},
        {6.0f, {}, {0.0f, 0.0f, -48.0f}}, {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"右ひじ"}, {
        {0.0f}, {2.0f, {}, {0.0f, 0.0f, 14.0f}},
        {4.0f, {}, {0.0f, 0.0f, 42.0f}},
        {6.0f, {}, {0.0f, 0.0f, 18.0f}}, {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"左手首"}, {
        {0.0f}, {2.0f, {}, {8.0f, -12.0f, -10.0f}},
        {4.0f, {}, {-4.0f, 8.0f, 8.0f}},
        {6.0f, {}, {8.0f, -10.0f, -8.0f}}, {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"右手首"}, {
        {0.0f}, {2.0f, {}, {-4.0f, -8.0f, -8.0f}},
        {4.0f, {}, {8.0f, 12.0f, 10.0f}},
        {6.0f, {}, {-5.0f, -7.0f, -7.0f}}, {8.0f}
    });

    AddDemoTrack(tracks, skeleton, {"左足ＩＫ", "左足IK"}, {
        {0.0f},
        {1.0f, {-0.06f, 0.05f, 0.05f}},
        {2.0f, {-0.18f, 0.12f, -0.22f}, {0.0f, -4.0f, 0.0f}},
        {3.0f},
        {4.0f, {0.05f, 0.02f, 0.04f}},
        {5.0f},
        {6.0f, {-0.12f, 0.10f, -0.16f}},
        {7.0f},
        {8.0f}
    });
    AddDemoTrack(tracks, skeleton, {"右足ＩＫ", "右足IK"}, {
        {0.0f},
        {1.0f},
        {2.0f, {-0.04f, 0.02f, 0.03f}},
        {3.0f, {0.16f, 0.11f, -0.20f}, {0.0f, 4.0f, 0.0f}},
        {4.0f},
        {5.0f, {0.11f, 0.09f, -0.15f}},
        {6.0f},
        {7.0f, {0.06f, 0.04f, 0.04f}},
        {8.0f}
    });

    std::vector<MorphWeightTrack> morphTracks;
    if (model.HasMorphs())
    {
        const MorphSet& morphs = model.GetMorphSet();
        if (const std::optional<MorphIndex> blink = FindMorph(
                morphs,
                {"まばたき", "瞬き", "blink", "Blink"}
            ))
        {
            morphTracks.emplace_back(*blink, std::vector<FloatKeyframe>{
                {0.0f, 0.0f},
                {1.35f, 0.0f}, {1.45f, 1.0f}, {1.56f, 0.0f},
                {3.80f, 0.0f}, {3.90f, 1.0f}, {4.02f, 0.0f},
                {6.65f, 0.0f}, {6.75f, 1.0f}, {6.86f, 0.0f},
                {8.0f, 0.0f}
            });
        }
        if (const std::optional<MorphIndex> smile = FindMorph(
                morphs,
                {"笑い", "にこり", "smile", "Smile"}
            ))
        {
            morphTracks.emplace_back(*smile, std::vector<FloatKeyframe>{
                {0.0f, 0.0f}, {1.5f, 0.18f}, {3.0f, 0.48f},
                {5.0f, 0.68f}, {6.8f, 0.25f}, {8.0f, 0.0f}
            });
        }
    }

    if (tracks.size() < 8U)
    {
        throw std::runtime_error(
            "MMD demo rig does not contain enough standard full-body bones"
        );
    }
    return AnimationClip(
        std::string(FullBodyActionName),
        FullBodyActionDuration,
        std::move(tracks),
        {},
        std::move(morphTracks)
    );
}

void ConfigureCharacterLighting(Scene& scene)
{
    scene.CreateDirectionalLight(DirectionalLightData{
        .Direction = {-0.35f, -0.75f, -0.45f},
        .Color = {1.0f, 0.96f, 0.92f},
        .Intensity = 0.75f
    });
    scene.CreatePointLight(PointLightData{
        .Position = {5.0f, 13.0f, 9.0f},
        .Color = {1.0f, 0.88f, 0.78f},
        .Intensity = 2.4f,
        .Range = 35.0f,
        .Linear = 0.035f,
        .Quadratic = 0.006f
    });
    scene.CreatePointLight(PointLightData{
        .Position = {-6.0f, 9.0f, 5.0f},
        .Color = {0.62f, 0.72f, 1.0f},
        .Intensity = 1.3f,
        .Range = 30.0f,
        .Linear = 0.045f,
        .Quadratic = 0.008f
    });
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
    case MorphLabStage::Impulse: return "PMX 2.1 Impulse Morph";
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
        1U,
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
    MorphLabBehaviour(Scene& scene, Window& window, Input& input)
        : scene(scene), window(window), input(input)
    {
    }

    void Update(Entity& entity, float deltaTime) override
    {
        if (this->input.WasKeyPressed(InputKey::Space))
        {
            this->paused = !this->paused;
            this->titleDirty = true;
        }
        if (this->input.WasKeyPressed(InputKey::P))
        {
            const bool enabled = !this->scene.Physics().DebugDrawEnabled();
            this->scene.Physics().SetDebugDrawEnabled(enabled);
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
        title += " | Left/Right: switch | Space: pause | P: debug ";
        title += this->scene.Physics().DebugDrawEnabled() ? "ON" : "OFF";
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

    Scene& scene;
    Window& window;
    Input& input;
    MorphLabStage stage = MorphLabStage::Vertex;
    float elapsed = 0.0f;
    bool paused = false;
    bool titleDirty = true;
};

class SabaMeshDemoBehaviour final : public Behaviour
{
public:
    SabaMeshDemoBehaviour(
        std::shared_ptr<SabaMmdRuntimeModel> runtime,
        std::vector<Mesh*> meshes
    )
        : runtime(std::move(runtime)),
          meshes(std::move(meshes))
    {
    }

    void Update(Entity&, float deltaTime) override
    {
        this->runtime->Update(deltaTime);
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
    std::size_t diagnosticCounter = 0U;
};

ModelAsset& CreateSabaMeshModel(
    ResourceManager& resources,
    const std::string& name,
    const std::filesystem::path& modelPath
)
{
    SabaMmdImporter importer;
    ImportedModelData imported = importer.Import(modelPath);
    return resources.CreateModel(name, std::move(imported));
}

}

const AnimationClip& CreateMmdFullBodyDemoAnimation(ModelAsset& model)
{
    if (const AnimationClip* existing = model.FindAnimationClip(
            std::string(FullBodyActionName)
        ))
    {
        return *existing;
    }
    return model.AddAnimationClip(BuildFullBodyAction(model));
}

void SetupSabaMmdDemoScene(
    Scene& scene,
    ResourceManager& resources,
    Window& window,
    bool alternateModel
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

    const std::filesystem::path modelPath = DemoModelPath(alternateModel);
    ModelAsset& model = CreateSabaMeshModel(
        resources,
        alternateModel ? "sabaMeshModelAlt" : "sabaMeshModel",
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

    const std::filesystem::path motionPath = DemoMotionPath1();
    const std::filesystem::path vmdPath =
        std::filesystem::is_regular_file(motionPath)
        ? motionPath
        : std::filesystem::path{};
    SabaPhysicsSettings physicsSettings;
    if (const char* fpsValue =
            std::getenv("WISTERIA_SABA_PHYSICS_FPS"))
    {
        const float fps = static_cast<float>(std::atof(fpsValue));
        if (fps > 0.0f)
            physicsSettings.fixedTimeStep = 1.0f / fps;
    }
    if (const char* stepsValue =
            std::getenv("WISTERIA_SABA_PHYSICS_MAXSTEPS"))
    {
        const int steps = std::atoi(stepsValue);
        if (steps > 0)
            physicsSettings.maxSubSteps = steps;
    }
    auto runtime = std::make_shared<SabaMmdRuntimeModel>(
        modelPath,
        vmdPath,
        physicsSettings
    );
    if (!runtime->Initialize())
    {
        throw std::runtime_error(
            "Saba mesh demo runtime failed to initialize"
        );
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
    entity.AddBehaviour<SabaMeshDemoBehaviour>(
        runtime,
        std::move(sabaMeshes)
    );

    scene.ActiveCamera().SetParam(CameraParam{
        .Position = {0.0f, 9.0f, 27.0f},
        .Target = {0.0f, 9.0f, 0.3f},
        .Up = {0.0f, 1.0f, 0.0f}
    });
    ConfigureCharacterLighting(scene);
    std::cout << "[INFO] MMD SABA MESH demo: meshes="
              << model.Parts().size()
              << " physicsFps="
              << (1.0f / physicsSettings.fixedTimeStep)
              << " maxSubSteps=" << physicsSettings.maxSubSteps
              << std::endl;
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
    MmdRigidBodyDefinition anchorBody;
    anchorBody.name = "morphLabAnchor";
    anchorBody.bone = 0U;
    anchorBody.shape = MmdRigidBodyShape::Sphere;
    anchorBody.size = glm::vec3(0.20f);
    anchorBody.modelBindTransform = glm::mat4(1.0f);
    anchorBody.boneToBody = glm::mat4(1.0f);
    anchorBody.bodyToBone = glm::mat4(1.0f);

    MmdRigidBodyDefinition dynamicBody;
    dynamicBody.name = "morphLabDynamicTip";
    dynamicBody.bone = 1U;
    dynamicBody.shape = MmdRigidBodyShape::Sphere;
    dynamicBody.size = glm::vec3(0.34f);
    dynamicBody.position = glm::vec3(0.42f, 0.62f, 0.0f);
    dynamicBody.modelBindTransform = glm::translate(
        glm::mat4(1.0f),
        dynamicBody.position
    );
    dynamicBody.boneToBody = dynamicBody.modelBindTransform;
    dynamicBody.bodyToBone = glm::inverse(dynamicBody.modelBindTransform);
    dynamicBody.mass = 0.65f;
    dynamicBody.linearDamping = 0.08f;
    dynamicBody.angularDamping = 0.12f;
    dynamicBody.friction = 0.5f;
    dynamicBody.mode = MmdRigidBodyMode::Physics;

    MmdJointDefinition joint;
    joint.name = "morphLabPendulum";
    joint.bodyA = 0U;
    joint.bodyB = 1U;
    joint.modelBindTransform = glm::mat4(1.0f);
    joint.linearLower = glm::vec3(0.0f);
    joint.linearUpper = glm::vec3(0.0f);
    joint.angularLower = glm::vec3(0.0f, 0.0f, -0.85f);
    joint.angularUpper = glm::vec3(0.0f, 0.0f, 0.85f);
    joint.angularSpring = glm::vec3(0.0f, 0.0f, 3.0f);
    model.SetMmdPhysics(MmdPhysicsAsset(
        {std::move(anchorBody), std::move(dynamicBody)},
        {std::move(joint)}
    ));
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
    scene.InstantiateModel(model,
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
    active.AddBehaviour<MorphLabBehaviour>(
        scene,
        window,
        window.GetInput()
    );

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
