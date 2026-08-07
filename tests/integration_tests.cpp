#include "test_support.hpp"
#include "test_fixtures.hpp"
#include "wisteria/mmd/mmd_determinism.hpp"
#include "wisteria/mmd/physics/mmd_physics_audit.hpp"
#include "wisteria/mmd/physics/mmd_physics_configuration.hpp"
#include "wisteria/mmd/physics/mmd_physics_trace.hpp"
#include "wisteria/runtime/checkpoint_serialization.hpp"
#include "wisteria/runtime/model_backend.hpp"
#include <Saba/Model/MMD/MMDCamera.h>
#include <Saba/Model/MMD/PMXFile.h>
#include "trace_jsonl.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>

namespace
{

void TestGlmMultiplySanity()
{
    const glm::mat4 identity(1.0f);
    const glm::vec4 input(-1.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 output = identity * input;
    Require(
        output.x == -1.0f && output.y == 0.0f &&
            output.z == 0.0f && output.w == 1.0f,
        "glm::mat4 * vec4 failed for an identity matrix"
    );
}

void TestAnimatedModelImporter()
{
    const std::filesystem::path modelPath =
        FixturePath("animated-triangle-gltf");
    RequireCoreAsset("animated-triangle-gltf");
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.skeleton.has_value(), "Animated glTF lost its Skeleton");
    Require(imported.animations.size() == 1, "Animated glTF lost its clip");
    Require(
        imported.animations[0].Name() == "moveRoot" &&
        NearlyEqual(imported.animations[0].Duration(), 1.0f) &&
        imported.animations[0].TrackCount() == 1,
        "Assimp animation metadata was imported incorrectly"
    );
    const std::optional<BoneIndex> rootBone =
        imported.skeleton->FindBone("rootBone");
    Require(rootBone.has_value(), "Animated glTF root bone was not imported");
    const AnimationTrack* track =
        imported.animations[0].FindTrack(*rootBone);
    Require(track != nullptr, "Animation channel was not mapped to its bone");
    Require(
        NearlyEqual(
            track->Sample(0.5f, BoneTransform{}).translation.x,
            1.0f
        ),
        "Imported animation keys were not converted to seconds"
    );

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("animatedTriangle", modelPath);
    Require(
        model.AnimationClipCount() == 1 &&
        model.FindAnimationClip("moveRoot") != nullptr,
        "ResourceManager lost imported animation data"
    );
    Scene scene;
    Entity& entity = scene.InstantiateModel(model);
    scene.Update(0.25f);
    Require(
        NearlyEqual(entity.GetPose().LocalMatrix(*rootBone)[3].x, 0.5f),
        "Imported animation did not drive the instantiated Pose"
    );
}

void TestExtendedPmxMorphImporter()
{
    const std::filesystem::path modelPath =
        FixturePath("extended-morph-pmx");
    RequireCoreAsset("extended-morph-pmx");
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.skeleton.has_value() &&
        imported.skeleton->BoneCount() == 2U,
        "Extended PMX fixture lost its imported Skeleton"
    );
    Require(
        imported.meshes.size() == 1U &&
        imported.materials.size() == 1U &&
        imported.morphs.size() == 5U,
        "Extended PMX fixture changed its resource counts"
    );

    const MorphSet morphSet(imported.morphs);
    const std::optional<MorphIndex> vertexMorph =
        morphSet.FindMorph("vertex");
    const std::optional<MorphIndex> boneMorphIndex =
        morphSet.FindMorph("bone");
    const std::optional<MorphIndex> uvMorph = morphSet.FindMorph("uv");
    const std::optional<MorphIndex> materialMorphIndex =
        morphSet.FindMorph("materialMorph");
    const std::optional<MorphIndex> groupMorph =
        morphSet.FindMorph("group");
    Require(
        vertexMorph.has_value() && boneMorphIndex.has_value() &&
        uvMorph.has_value() && materialMorphIndex.has_value() &&
        groupMorph.has_value(),
        "Extended PMX fixture lost one or more named morphs"
    );
    const MorphIndex vertexIndex = *vertexMorph;
    const MorphIndex boneIndex = *boneMorphIndex;
    const MorphIndex uvIndex = *uvMorph;
    const MorphIndex materialIndex = *materialMorphIndex;
    const MorphIndex groupIndex = *groupMorph;
    Require(
        morphSet.DefinitionAt(vertexIndex).kind == MorphKind::Vertex &&
        morphSet.DefinitionAt(boneIndex).kind == MorphKind::Bone &&
        morphSet.DefinitionAt(uvIndex).kind == MorphKind::Uv &&
        morphSet.DefinitionAt(materialIndex).kind == MorphKind::Material &&
        morphSet.DefinitionAt(groupIndex).kind == MorphKind::Group,
        "PMX morph types 1, 2, 3, 8, or 0 were mapped incorrectly"
    );

    const MorphDefinition& boneMorph = morphSet.DefinitionAt(boneIndex);
    Require(
        boneMorph.boneOffsets.size() == 1U &&
        imported.skeleton->BoneAt(
            boneMorph.boneOffsets[0U].boneIndex
        ).name == "root",
        "PMX Bone Morph source index was not remapped to the Skeleton"
    );
    const MorphDefinition& materialMorph =
        morphSet.DefinitionAt(materialIndex);
    Require(
        materialMorph.materialOffsets.size() == 1U &&
        materialMorph.materialOffsets[0U].materialIndex == 0U &&
        materialMorph.materialOffsets[0U].operation ==
            MaterialMorphOperation::Add,
        "PMX Material Morph metadata was imported incorrectly"
    );

    MorphState state(morphSet);
    state.SetWeight(groupIndex, 1.0f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[boneIndex], 0.5f) &&
        NearlyEqual(effective[uvIndex], 2.0f) &&
        NearlyEqual(effective[materialIndex], 1.0f) &&
        NearlyEqual(effective[groupIndex], 0.0f),
        "Imported PMX Group Morph did not drive extended morph kinds"
    );

    PoseBuffer pose(*imported.skeleton);
    morphSet.ApplyBoneMorphs(effective, pose);
    const BoneTransform& transformed = pose.TransformAt(
        boneMorph.boneOffsets[0U].boneIndex
    );
    Require(
        NearlyEqual(
            transformed.translation,
            glm::vec3(0.5f, 1.0f, -1.5f)
        ) &&
        NearlySameRotation(
            transformed.rotation,
            glm::angleAxis(
                glm::radians(-45.0f),
                glm::vec3(1.0f, 0.0f, 0.0f)
            )
        ),
        "Imported PMX Bone Morph coordinate conversion is incorrect"
    );

    const ImportedMeshData& importedMeshData = imported.meshes[0U];
    Mesh importedMesh(
        DefaultModelData{
            importedMeshData.data.vertices,
            importedMeshData.data.indices,
            importedMeshData.data.layout
        },
        importedMeshData.requiredBoneCount,
        importedMeshData.morphTargets
    );
    std::vector<MorphVertexDelta> deltas;
    Require(
        importedMeshData.morphMaterialIndex ==
            std::optional<std::uint32_t>(0U) &&
        importedMeshData.morphTargets.size() == 2U &&
        importedMesh.CalculateMorphDeltas(effective, deltas),
        "Imported PMX UV Morph did not create a runtime mesh target"
    );
    bool foundUvDelta = false;
    for (const MorphVertexDelta& delta : deltas)
    {
        if (NearlyEqual(
                delta.uv[0U],
                glm::vec4(0.5f, 1.0f, 0.0f, 0.0f)
            ))
        {
            foundUvDelta = true;
            break;
        }
    }
    Require(foundUvDelta, "Imported PMX UV Morph delta changed");

    MaterialMorphValues values;
    values.edgeSize = 1.0f;
    morphSet.ApplyMaterialMorphs(0U, effective, values);
    Require(
        NearlyEqual(values.diffuse.x, 1.2f) &&
        NearlyEqual(values.edgeSize, 1.5f) &&
        NearlyEqual(values.textureFactor.x, 1.1f),
        "Imported PMX Material Morph values changed"
    );
}

void TestVmdImporter()
{
    std::vector<std::uint8_t> bytes;
    const auto appendValue = [&bytes]<typename T>(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const std::size_t begin = bytes.size();
        bytes.resize(begin + sizeof(T));
        std::memcpy(bytes.data() + begin, &value, sizeof(T));
    };
    const auto appendFixed = [&bytes](std::string_view value, std::size_t size)
    {
        const std::size_t begin = bytes.size();
        bytes.resize(begin + size, 0U);
        const std::size_t copySize = std::min(value.size(), size);
        std::memcpy(bytes.data() + begin, value.data(), copySize);
    };
    const auto linearInterpolation = []
    {
        std::array<std::uint8_t, 64> result{};
        for (std::size_t offset : {0U, 16U, 32U, 48U})
        {
            result[offset] = 20U;
            result[offset + 4U] = 20U;
            result[offset + 8U] = 107U;
            result[offset + 12U] = 107U;
        }
        return result;
    };
    const auto appendBoneFrame = [
        &appendValue,
        &appendFixed,
        &bytes
    ](
        std::string_view shiftJisName,
        std::uint32_t frame,
        const glm::vec3& translation,
        const glm::quat& rotation,
        const std::array<std::uint8_t, 64>& interpolation
    )
    {
        appendFixed(shiftJisName, 15U);
        appendValue(frame);
        appendValue(translation.x);
        appendValue(translation.y);
        appendValue(translation.z);
        appendValue(rotation.x);
        appendValue(rotation.y);
        appendValue(rotation.z);
        appendValue(rotation.w);
        bytes.insert(
            bytes.end(),
            interpolation.begin(),
            interpolation.end()
        );
    };

    appendFixed("Vocaloid Motion Data 0002", 30U);
    appendFixed("testModel", 20U);
    appendValue(std::uint32_t{3U});

    const std::string headShiftJis("\x93\xAA", 2U);
    const std::string unknownShiftJis("\x96\xA2\x92\x6D", 4U);
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    appendBoneFrame(
        headShiftJis,
        0U,
        glm::vec3(0.0f),
        identity,
        linearInterpolation()
    );

    std::array<std::uint8_t, 64> curved = linearInterpolation();
    curved[0U] = 64U;
    curved[4U] = 0U;
    curved[8U] = 127U;
    curved[12U] = 64U;
    appendBoneFrame(
        headShiftJis,
        30U,
        glm::vec3(2.0f, 0.0f, 4.0f),
        glm::angleAxis(
            glm::radians(90.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ),
        curved
    );
    appendBoneFrame(
        unknownShiftJis,
        60U,
        glm::vec3(8.0f),
        identity,
        linearInterpolation()
    );

    // Morph frames are followed by empty camera/light/self-shadow sections
    // and VMD model frames containing per-controller IK switches.
    appendValue(std::uint32_t{3U});
    const auto appendMorphFrame = [
        &appendValue,
        &appendFixed
    ](std::string_view name, std::uint32_t frame, float weight)
    {
        appendFixed(name, 15U);
        appendValue(frame);
        appendValue(weight);
    };
    appendMorphFrame("smile", 0U, 0.0f);
    appendMorphFrame("smile", 30U, 1.0f);
    appendMorphFrame("unknownMorph", 15U, 0.5f);
    appendValue(std::uint32_t{0U});
    appendValue(std::uint32_t{0U});
    appendValue(std::uint32_t{0U});
    appendValue(std::uint32_t{3U});
    const auto appendIkModelFrame = [
        &appendValue,
        &appendFixed
    ](
        std::uint32_t frame,
        bool enabled,
        bool includeUnknown
    )
    {
        appendValue(frame);
        appendValue(std::uint8_t{1U});
        appendValue(std::uint32_t{includeUnknown ? 2U : 1U});
        appendFixed("ikController", 20U);
        appendValue(static_cast<std::uint8_t>(enabled));
        if (includeUnknown)
        {
            appendFixed("unknownIk", 20U);
            appendValue(std::uint8_t{0U});
        }
    };
    appendIkModelFrame(0U, true, false);
    appendIkModelFrame(15U, false, true);
    appendIkModelFrame(30U, true, false);

    const glm::mat4 headBind = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 2.0f, 3.0f)
    );
    Bone head{
        "\xE9\xA0\xAD",
        InvalidBoneIndex,
        headBind,
        glm::inverse(headBind)
    };
    Bone ikLink{"ikLink"};
    Bone ikEffector{"ikEffector"};
    ikEffector.parentIndex = 1U;
    ikEffector.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    ikEffector.inverseBindMatrix = glm::inverse(
        ikEffector.bindLocalMatrix
    );
    Bone ikController{"ikController"};
    ikController.bindLocalMatrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    ikController.inverseBindMatrix = glm::inverse(
        ikController.bindLocalMatrix
    );
    ikController.ikConstraint = MmdIkConstraint{
        2U,
        8U,
        glm::radians(45.0f),
        {MmdIkLink{1U}}
    };
    Skeleton skeleton({
        std::move(head),
        std::move(ikLink),
        std::move(ikEffector),
        std::move(ikController)
    });
    MorphSet morphSet({MorphDefinition{"smile", MorphCategory::Mouth}});
    const ImportedVmdAnimationData imported = VmdImporter().Import(
        bytes,
        skeleton,
        "memoryMotion",
        VmdImportOptions{.clipName = "walk"},
        &morphSet
    );

    Require(imported.modelName == "testModel", "VMD model name was not decoded");
    Require(
        imported.sourceBoneTrackCount == 2U &&
        imported.unmatchedBoneNames.size() == 1U,
        "VMD unmatched-bone reporting is incorrect"
    );
    Require(
        imported.sourceMorphTrackCount == 2U &&
        imported.unmatchedMorphNames.size() == 1U &&
        imported.clip.MorphWeightTrackCount() == 1U,
        "VMD morph reporting or name mapping is incorrect"
    );
    Require(
        imported.sourceIkStateTrackCount == 2U &&
        imported.unmatchedIkNames.size() == 1U &&
        imported.clip.MmdIkStateTrackCount() == 1U,
        "VMD IK-state reporting or controller mapping is incorrect"
    );
    Require(
        imported.clip.Name() == "walk" &&
        NearlyEqual(imported.clip.Duration(), 1.0f) &&
        imported.clip.TrackCount() == 1U,
        "VMD clip metadata is incorrect"
    );

    const AnimationTrack* track = imported.clip.FindTrack(0U);
    Require(track != nullptr, "VMD bone name was not mapped from Shift-JIS");
    Require(
        track->TranslationKeys()[1].interpolation[0].mode ==
            AnimationInterpolation::CubicBezier &&
        track->TranslationKeys()[1].interpolation[0].Evaluate(0.5f) < 0.5f,
        "VMD Bezier interpolation was not imported"
    );
    const BoneTransform endPose = track->Sample(1.0f, BoneTransform{});
    Require(
        NearlyEqual(endPose.translation.x, 3.0f) &&
        NearlyEqual(endPose.translation.y, 2.0f) &&
        NearlyEqual(endPose.translation.z, -1.0f),
        "VMD translation or handedness conversion is incorrect"
    );
    Require(
        endPose.rotation.y < -0.7f,
        "VMD quaternion handedness conversion is incorrect"
    );

    const MmdIkStateTrack* ikTrack =
        imported.clip.FindMmdIkStateTrack(3U);
    Require(
        ikTrack != nullptr && ikTrack->Sample(0.0f) &&
        !ikTrack->Sample(0.5f) && ikTrack->Sample(1.0f),
        "VMD IK switches were not imported as discrete keyframes"
    );
    const MorphWeightTrack* morphTrack =
        imported.clip.FindMorphWeightTrack(0U);
    Require(
        morphTrack != nullptr &&
        NearlyEqual(morphTrack->Sample(0.5f), 0.5f) &&
        NearlyEqual(morphTrack->Sample(1.0f), 1.0f),
        "VMD morph weights were not imported with linear interpolation"
    );

    Pose pose(skeleton);
    MorphState morphState(morphSet);
    Animator animator(pose, &morphState);
    animator.Play(imported.clip);
    animator.SetTime(0.5f);
    Require(
        !animator.IsMmdIkEnabled(3U) &&
        NearlyEqual(morphState.Weight(0U), 0.5f) &&
        glm::length(
            glm::vec3(pose.GlobalMatrix(2U)[3]) -
            glm::vec3(1.0f, 0.0f, 0.0f)
        ) < 0.001f,
        "Animator did not apply a disabled VMD IK state"
    );
    animator.SetTime(1.0f);
    Require(
        animator.IsMmdIkEnabled(3U) &&
        NearlyEqual(morphState.Weight("smile"), 1.0f) &&
        glm::length(
            glm::vec3(pose.GlobalMatrix(2U)[3]) -
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) < 0.001f,
        "Animator did not re-enable IK at the next VMD keyframe"
    );
    animator.SetMmdIkEnabled(3U, false);
    Require(
        !animator.IsMmdIkEnabled(3U),
        "Manual IK override did not take priority over VMD"
    );
    animator.ClearMmdIkOverride(3U);
    Require(
        animator.IsMmdIkEnabled(3U),
        "Clearing a manual IK override did not restore VMD control"
    );

    AnimationClip alwaysEnabled(
        "alwaysEnabled",
        1.0f,
        {},
        {MmdIkStateTrack(3U, {BoolKeyframe{0.0f, true}})}
    );
    AnimationClip alwaysDisabled(
        "alwaysDisabled",
        1.0f,
        {},
        {MmdIkStateTrack(3U, {BoolKeyframe{0.0f, false}})}
    );
    animator.Play(alwaysEnabled);
    animator.CrossFade(alwaysDisabled, 1.0f);
    animator.Update(0.49f);
    Require(
        animator.IsMmdIkEnabled(3U),
        "Cross Fade switched its discrete IK state too early"
    );
    animator.Update(0.02f);
    Require(
        !animator.IsMmdIkEnabled(3U),
        "Cross Fade did not switch to the destination IK state"
    );

    AnimationClip neutralExpression(
        "neutralExpression",
        1.0f,
        {},
        {},
        {MorphWeightTrack(0U, {FloatKeyframe{0.0f, 0.0f}})}
    );
    AnimationClip smileExpression(
        "smileExpression",
        1.0f,
        {},
        {},
        {MorphWeightTrack(0U, {FloatKeyframe{0.0f, 1.0f}})}
    );
    animator.Play(neutralExpression);
    animator.CrossFade(smileExpression, 1.0f);
    animator.Update(0.5f);
    Require(
        NearlyEqual(morphState.Weight("smile"), 0.5f),
        "Cross Fade did not blend source and destination morph weights"
    );

    bool invalidSignatureRejected = false;
    try
    {
        std::vector<std::uint8_t> invalid = bytes;
        invalid[0] = static_cast<std::uint8_t>('X');
        static_cast<void>(VmdImporter().Import(
            invalid,
            skeleton,
            "invalid"
        ));
    }
    catch (const std::runtime_error&)
    {
        invalidSignatureRejected = true;
    }
    Require(invalidSignatureRejected, "VMD importer accepted an invalid signature");
}

void TestVmdAssetWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-couqie");
    const std::filesystem::path motionPath =
        FixturePath("production-vmd-penguin");
    RequireFullAsset("production-pmx-couqie");
    RequireFullAsset("production-vmd-penguin");

    const ImportedModelData model = ModelImporter().Import(modelPath);
    Require(model.skeleton.has_value(), "VMD test PMX has no Skeleton");
    std::optional<MorphSet> morphSet;
    if (!model.morphs.empty())
        morphSet.emplace(model.morphs);
    const ImportedVmdAnimationData motion = VmdImporter().Import(
        motionPath,
        *model.skeleton,
        {},
        morphSet.has_value() ? &*morphSet : nullptr
    );
    Require(
        motion.modelName == "Adelie Tomori_arm" &&
        motion.sourceBoneTrackCount == 44U,
        "Real VMD header or bone-track count changed"
    );
    Require(
        motion.clip.TrackCount() >= 20U &&
        NearlyEqual(motion.clip.Duration(), 0.6f),
        "Real VMD motion did not bind to the PMX Skeleton"
    );
    Require(
        motion.clip.MorphWeightTrackCount() +
            motion.unmatchedMorphNames.size() ==
            motion.sourceMorphTrackCount,
        "Real VMD morph tracks were not completely mapped or reported"
    );
}

void TestModelAssetSkeleton()
{
    ModelAsset model("skeletonModel");
    Require(!model.HasSkeleton(), "Static ModelAsset unexpectedly has a skeleton");
    Require(model.TryGetSkeleton() == nullptr, "Missing skeleton returned a pointer");

    bool missingRejected = false;
    try
    {
        static_cast<void>(model.GetSkeleton());
    }
    catch (const std::logic_error&)
    {
        missingRejected = true;
    }
    Require(missingRejected, "ModelAsset returned a missing skeleton");

    model.SetSkeleton(Skeleton({Bone{"root"}}));
    Require(model.HasSkeleton(), "ModelAsset did not store its skeleton");
    Require(
        model.TryGetSkeleton() == &model.GetSkeleton() &&
        model.GetSkeleton().BoneCount() == 1,
        "ModelAsset skeleton access is inconsistent"
    );

    bool replacementRejected = false;
    try
    {
        model.SetSkeleton(Skeleton({Bone{"replacement"}}));
    }
    catch (const std::logic_error&)
    {
        replacementRejected = true;
    }
    Require(replacementRejected, "ModelAsset allowed its skeleton to be replaced");
}

void TestPmx21FlipImpulseMorphRuntime()
{
    MorphDefinition first;
    first.name = "first";
    first.kind = MorphKind::Vertex;

    MorphDefinition second;
    second.name = "second";
    second.kind = MorphKind::Material;

    MorphDefinition flip;
    flip.name = "flip";
    flip.kind = MorphKind::Flip;
    flip.flipMembers = {
        FlipMorphMember{0U, 0.25f},
        FlipMorphMember{1U, 0.75f}
    };

    MorphDefinition impulse;
    impulse.name = "impulse";
    impulse.kind = MorphKind::Impulse;
    impulse.impulseOffsets = {
        ImpulseMorphOffset{
            2U,
            false,
            glm::vec3(1.0f, 2.0f, 3.0f),
            glm::vec3(4.0f, 5.0f, 6.0f)
        },
        ImpulseMorphOffset{
            2U,
            true,
            glm::vec3(-1.0f, 0.0f, 2.0f),
            glm::vec3(0.5f, -0.5f, 1.0f)
        },
        ImpulseMorphOffset{
            2U,
            false,
            glm::vec3(0.0f),
            glm::vec3(0.0f)
        }
    };

    MorphDefinition nested;
    nested.name = "nested";
    nested.kind = MorphKind::Group;
    nested.groupMembers = {
        GroupMorphMember{2U, 1.0f},
        GroupMorphMember{3U, 2.0f}
    };

    MorphDefinition outer;
    outer.name = "outer";
    outer.kind = MorphKind::Group;
    outer.groupMembers = {GroupMorphMember{4U, 1.0f}};

    MorphSet morphSet({
        std::move(first),
        std::move(second),
        std::move(flip),
        std::move(impulse),
        std::move(nested),
        std::move(outer)
    });
    Require(
        morphSet.HasKind(MorphKind::Flip) &&
        morphSet.HasKind(MorphKind::Impulse),
        "MorphSet did not retain PMX 2.1 morph kinds"
    );

    MorphState state(morphSet);
    state.SetWeight("first", 0.9f);
    state.SetWeight("flip", 0.2f);
    std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.9f) &&
        NearlyEqual(effective[1U], 0.0f),
        "Flip Morph selected a target inside its initial empty interval"
    );

    state.SetWeight("flip", 0.34f);
    effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.25f) &&
        NearlyEqual(effective[1U], 0.0f) &&
        NearlyEqual(effective[2U], 0.0f),
        "Flip Morph did not overwrite the first selected target"
    );

    state.Reset();
    state.SetWeight("second", 0.2f);
    state.SetWeight("flip", 0.67f);
    effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.0f) &&
        NearlyEqual(effective[1U], 0.75f),
        "Flip Morph did not overwrite the second selected target"
    );
    state.SetWeight("flip", 2.0f);
    Require(
        NearlyEqual(state.EffectiveWeights()[1U], 0.75f),
        "Flip Morph did not clamp an above-range control to its last target"
    );

    state.Reset();
    state.SetWeight("outer", 0.67f);
    effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[1U], 0.75f) &&
        NearlyEqual(effective[3U], 1.34f) &&
        NearlyEqual(effective[4U], 0.0f) &&
        NearlyEqual(effective[5U], 0.0f),
        "Nested Group Morph did not drive Flip and Impulse Morphs"
    );

    std::vector<MmdRigidBodyImpulse> impulses;
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.size() == 1U &&
        impulses[0U].rigidBodyIndex == 2U &&
        impulses[0U].reset &&
        NearlyEqual(
            impulses[0U].globalLinearImpulse,
            glm::vec3(1.34f, 2.68f, 4.02f)
        ) &&
        NearlyEqual(
            impulses[0U].globalTorqueImpulse,
            glm::vec3(5.36f, 6.7f, 8.04f)
        ) &&
        NearlyEqual(
            impulses[0U].localLinearImpulse,
            glm::vec3(-1.34f, 0.0f, 2.68f)
        ) &&
        NearlyEqual(
            impulses[0U].localTorqueImpulse,
            glm::vec3(0.67f, -0.67f, 1.34f)
        ),
        "Impulse Morph aggregation did not preserve local/global channels"
    );

    state.Reset();
    state.SetWeight("impulse", -0.5f);
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.size() == 1U && impulses[0U].reset &&
        NearlyEqual(
            impulses[0U].globalLinearImpulse,
            glm::vec3(-0.5f, -1.0f, -1.5f)
        ),
        "Impulse Morph incorrectly clamped a negative effective weight"
    );
    state.Reset();
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.empty(),
        "Zero-weight Impulse Morph produced a physics request"
    );

    MorphDefinition blockedLeaf;
    blockedLeaf.name = "blockedLeaf";
    MorphDefinition earlyFlip;
    earlyFlip.name = "earlyFlip";
    earlyFlip.kind = MorphKind::Flip;
    earlyFlip.flipMembers = {FlipMorphMember{0U, 0.4f}};
    MorphDefinition lateController;
    lateController.name = "lateController";
    lateController.kind = MorphKind::Flip;
    lateController.flipMembers = {FlipMorphMember{1U, 1.0f}};
    MorphSet blockedOrderSet({
        std::move(blockedLeaf),
        std::move(earlyFlip),
        std::move(lateController)
    });
    MorphState blockedOrderState(blockedOrderSet);
    blockedOrderState.SetWeight(2U, 1.0f);
    Require(
        NearlyEqual(blockedOrderState.EffectiveWeights()[0U], 0.0f),
        "A later Flip Morph incorrectly re-evaluated an earlier Flip Morph"
    );

    MorphDefinition forwardedLeaf;
    forwardedLeaf.name = "forwardedLeaf";
    MorphDefinition earlyController;
    earlyController.name = "earlyController";
    earlyController.kind = MorphKind::Flip;
    earlyController.flipMembers = {FlipMorphMember{2U, 1.0f}};
    MorphDefinition lateFlip;
    lateFlip.name = "lateFlip";
    lateFlip.kind = MorphKind::Flip;
    lateFlip.flipMembers = {FlipMorphMember{0U, 0.4f}};
    MorphSet forwardedOrderSet({
        std::move(forwardedLeaf),
        std::move(earlyController),
        std::move(lateFlip)
    });
    MorphState forwardedOrderState(forwardedOrderSet);
    forwardedOrderState.SetWeight(1U, 1.0f);
    Require(
        NearlyEqual(forwardedOrderState.EffectiveWeights()[0U], 0.4f),
        "An earlier Flip Morph did not feed a later Flip Morph in index order"
    );

    MorphDefinition selfFlip;
    selfFlip.name = "selfFlip";
    selfFlip.kind = MorphKind::Flip;
    selfFlip.flipMembers = {FlipMorphMember{0U, 0.5f}};
    MorphSet selfFlipSet({std::move(selfFlip)});
    MorphState selfFlipState(selfFlipSet);
    selfFlipState.SetWeight(0U, 1.0f);
    Require(
        NearlyEqual(selfFlipState.EffectiveWeights()[0U], 0.0f),
        "Self-referencing Flip Morph was not handled as a control-only morph"
    );
}

void TestPmx21FlipImpulseImporter()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx21-flip-impulse");
    RequireCoreAsset("pmx21-flip-impulse");
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.mmdPhysics.has_value() &&
        imported.mmdPhysics->RigidBodyCount() == 1U &&
        imported.morphs.size() == 7U,
        "PMX 2.1 fixture changed its rigid-body or morph count"
    );

    const MorphSet morphSet(imported.morphs);
    const std::optional<MorphIndex> vertex = morphSet.FindMorph("vertex");
    const std::optional<MorphIndex> material =
        morphSet.FindMorph("materialMorph");
    const std::optional<MorphIndex> group = morphSet.FindMorph("group");
    const std::optional<MorphIndex> flip = morphSet.FindMorph("flip");
    const std::optional<MorphIndex> impulse = morphSet.FindMorph("impulse");
    Require(
        vertex.has_value() && material.has_value() && group.has_value() &&
        flip.has_value() && impulse.has_value(),
        "PMX 2.1 fixture lost a named morph"
    );
    Require(
        morphSet.DefinitionAt(*flip).kind == MorphKind::Flip &&
        morphSet.DefinitionAt(*impulse).kind == MorphKind::Impulse &&
        morphSet.DefinitionAt(*flip).flipMembers.size() == 2U &&
        morphSet.DefinitionAt(*impulse).impulseOffsets.size() == 3U,
        "PMX 2.1 Flip/Impulse metadata was imported incorrectly"
    );

    const ImpulseMorphOffset& global =
        morphSet.DefinitionAt(*impulse).impulseOffsets[0U];
    const ImpulseMorphOffset& local =
        morphSet.DefinitionAt(*impulse).impulseOffsets[1U];
    Require(
        !global.local && local.local &&
        global.rigidBodyIndex == 0U && local.rigidBodyIndex == 0U &&
        NearlyEqual(global.velocity, glm::vec3(1.0f, 2.0f, -3.0f)) &&
        NearlyEqual(global.torque, glm::vec3(-4.0f, -5.0f, 6.0f)) &&
        NearlyEqual(local.velocity, glm::vec3(2.0f, 0.0f, 2.0f)) &&
        NearlyEqual(local.torque, glm::vec3(-1.0f, 1.0f, 3.0f)),
        "PMX Impulse Morph coordinate conversion is incorrect"
    );

    MorphState state(morphSet);
    state.SetWeight(*group, 0.67f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[*material], 1.42f) &&
        NearlyEqual(effective[*impulse], 0.335f) &&
        NearlyEqual(effective[*flip], 0.0f) &&
        NearlyEqual(effective[*group], 0.0f),
        "Imported Group Morph did not compose PMX 2.1 controls correctly"
    );

    std::vector<MmdRigidBodyImpulse> impulses;
    state.EvaluateImpulseMorphs(impulses);
    Require(
        impulses.size() == 1U && impulses[0U].reset &&
        NearlyEqual(
            impulses[0U].globalLinearImpulse,
            glm::vec3(0.335f, 0.67f, -1.005f)
        ) &&
        NearlyEqual(
            impulses[0U].localLinearImpulse,
            glm::vec3(0.67f, 0.0f, 0.67f)
        ),
        "Imported PMX Impulse Morph did not produce runtime impulses"
    );
}

void TestPmxPhysicsImporter()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.skeleton.has_value() && imported.mmdPhysics.has_value(),
        "PMX Physics 1 fixture lost its Skeleton or physics metadata"
    );

    const MmdPhysicsAsset& physics = *imported.mmdPhysics;
    Require(
        physics.RigidBodyCount() == 3U && physics.JointCount() == 6U,
        "PMX Physics 1 fixture changed its rigid-body or joint count"
    );
    const auto sphereIndex = physics.FindRigidBody("followSphere");
    const auto boxIndex = physics.FindRigidBody("dynamicBox");
    const auto capsuleIndex = physics.FindRigidBody("mergeCapsule");
    Require(
        sphereIndex == std::optional<RigidBodyIndex>(0U) &&
        boxIndex == std::optional<RigidBodyIndex>(1U) &&
        capsuleIndex == std::optional<RigidBodyIndex>(2U),
        "PMX rigid-body name lookup changed"
    );

    const MmdRigidBodyDefinition& sphere = physics.RigidBodyAt(*sphereIndex);
    const MmdRigidBodyDefinition& box = physics.RigidBodyAt(*boxIndex);
    const MmdRigidBodyDefinition& capsule = physics.RigidBodyAt(*capsuleIndex);
    const std::optional<BoneIndex> root = imported.skeleton->FindBone("root");
    Require(
        root.has_value() &&
        imported.skeleton->BoneAt(*root).deformAfterPhysics,
        "PMX deform-after-physics bone flag was not preserved"
    );
    Require(
        root.has_value() && sphere.bone == *root &&
        capsule.bone == *root && box.bone == InvalidBoneIndex,
        "PMX rigid-body bone indices were not mapped to the runtime Skeleton"
    );
    Require(
        sphere.shape == MmdRigidBodyShape::Sphere &&
        box.shape == MmdRigidBodyShape::Box &&
        capsule.shape == MmdRigidBodyShape::Capsule &&
        sphere.mode == MmdRigidBodyMode::FollowBone &&
        box.mode == MmdRigidBodyMode::Physics &&
        capsule.mode == MmdRigidBodyMode::PhysicsWithBone,
        "PMX rigid-body shape or mode mapping changed"
    );
    Require(
        sphere.collisionGroup == 0U && sphere.nonCollisionMask == 0x0002U &&
        box.collisionGroup == 1U && box.nonCollisionMask == 0x0001U &&
        capsule.collisionGroup == 15U &&
        capsule.nonCollisionMask == 0x00F0U,
        "PMX rigid-body collision filters changed"
    );
    Require(
        NearlyEqual(sphere.size, glm::vec3(1.0f, 2.0f, 3.0f)) &&
        NearlyEqual(sphere.position, glm::vec3(1.0f, 2.0f, -3.0f)) &&
        NearlyEqual(box.position, glm::vec3(-1.0f, 4.0f, 2.0f)) &&
        NearlyEqual(capsule.position, glm::vec3(0.0f, 3.0f, -1.0f)),
        "PMX rigid-body size or coordinate conversion changed"
    );
    Require(
        NearlySameRotation(
            sphere.rotation,
            glm::angleAxis(-0.25f, glm::vec3(1.0f, 0.0f, 0.0f))
        ) &&
        NearlySameRotation(
            box.rotation,
            glm::angleAxis(-0.5f, glm::vec3(0.0f, 1.0f, 0.0f))
        ) &&
        NearlySameRotation(
            capsule.rotation,
            glm::angleAxis(0.75f, glm::vec3(0.0f, 0.0f, 1.0f))
        ),
        "PMX rigid-body rotation conversion changed"
    );
    Require(
        NearlyEqual(sphere.modelBindTransform[3],
            glm::vec4(1.0f, 2.0f, -3.0f, 1.0f)) &&
        MatrixIdentityDeviation(sphere.boneToBody * sphere.bodyToBone) <
            Epsilon,
        "PMX rigid-body bind offsets are inconsistent"
    );
    Require(
        NearlyEqual(box.mass, 2.0f) &&
        NearlyEqual(box.linearDamping, 0.25f) &&
        NearlyEqual(box.angularDamping, 0.35f) &&
        NearlyEqual(box.restitution, 0.45f) &&
        NearlyEqual(box.friction, 0.55f),
        "PMX rigid-body physical parameters changed"
    );

    for (std::size_t index = 0; index < physics.JointCount(); ++index)
    {
        Require(
            physics.JointAt(index).type ==
                static_cast<MmdJointType>(index),
            "PMX 2.1 joint type mapping changed"
        );
    }
    const MmdJointDefinition& joint = physics.JointAt(0U);
    Require(
        joint.bodyA == 0U && joint.bodyB == 1U &&
        NearlyEqual(joint.position, glm::vec3(1.0f, 2.0f, 3.0f)) &&
        NearlyEqual(joint.linearLower, glm::vec3(-1.0f, -2.0f, -6.0f)) &&
        NearlyEqual(joint.linearUpper, glm::vec3(4.0f, 5.0f, 3.0f)) &&
        NearlyEqual(joint.angularLower, glm::vec3(-0.4f, -0.5f, -0.3f)) &&
        NearlyEqual(joint.angularUpper, glm::vec3(0.1f, 0.2f, 0.6f)) &&
        NearlyEqual(joint.linearSpring, glm::vec3(1.0f, 2.0f, 3.0f)) &&
        NearlyEqual(joint.angularSpring, glm::vec3(4.0f, 5.0f, 6.0f)),
        "PMX joint limits, springs, or coordinate conversion changed"
    );
}

void TestPmxPhysicsImporterValidation()
{
    const auto rejected = [](const std::filesystem::path& path)
    {
        try
        {
            (void)ModelImporter().Import(path);
            return false;
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
    };

    Require(
        rejected(FixturePath("pmx-physics-invalid-group")),
        "PMX importer accepted collision group 16"
    );
    Require(
        rejected(FixturePath("pmx-physics-invalid-joint")),
        "PMX importer accepted an out-of-range joint rigid body"
    );
    Require(
        rejected(FixturePath("pmx-physics-softbody")),
        "PMX importer silently accepted unsupported Soft Body data"
    );
}

void TestMmdPhysicsAssetValidation()
{
    MmdRigidBodyDefinition bodyA;
    bodyA.name = "bodyA";
    MmdRigidBodyDefinition bodyB;
    bodyB.name = "bodyB";
    MmdJointDefinition joint;
    joint.name = "joint";
    joint.bodyA = 0U;
    joint.bodyB = 1U;
    MmdPhysicsAsset physics({bodyA, bodyB}, {joint});
    Require(
        physics.RigidBodyCount() == 2U && physics.JointCount() == 1U &&
        physics.FindRigidBody("bodyB") ==
            std::optional<RigidBodyIndex>(1U),
        "MmdPhysicsAsset did not preserve valid definitions"
    );

    bool invalidGroupRejected = false;
    try
    {
        MmdRigidBodyDefinition invalid;
        invalid.name = "invalid";
        invalid.collisionGroup = 16U;
        MmdPhysicsAsset rejected({invalid}, {});
        (void)rejected;
    }
    catch (const std::invalid_argument&)
    {
        invalidGroupRejected = true;
    }
    Require(
        invalidGroupRejected,
        "MmdPhysicsAsset accepted an invalid collision group"
    );

    bool invalidJointRejected = false;
    try
    {
        MmdJointDefinition invalid;
        invalid.name = "invalidJoint";
        invalid.bodyA = InvalidRigidBodyIndex;
        invalid.bodyB = InvalidRigidBodyIndex;
        MmdPhysicsAsset rejected({bodyA}, {invalid});
        (void)rejected;
    }
    catch (const std::invalid_argument&)
    {
        invalidJointRejected = true;
    }
    Require(
        invalidJointRejected,
        "MmdPhysicsAsset accepted a joint without a rigid-body endpoint"
    );
}

void TestExtendedMmdMorphRuntime()
{
    Skeleton skeleton({Bone{"root"}});

    MorphDefinition boneMorph;
    boneMorph.name = "bone";
    boneMorph.kind = MorphKind::Bone;
    boneMorph.boneOffsets.push_back(BoneMorphOffset{
        0U,
        glm::vec3(2.0f, 0.0f, 0.0f),
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))
    });

    MorphDefinition uvMorph;
    uvMorph.name = "uv";
    uvMorph.kind = MorphKind::Uv;

    MaterialMorphOffset multiplyOffset;
    multiplyOffset.materialIndex = AllMaterialMorphTargets;
    multiplyOffset.operation = MaterialMorphOperation::Multiply;
    multiplyOffset.diffuse = glm::vec4(0.5f, 1.0f, 1.0f, 1.0f);
    multiplyOffset.specular = glm::vec3(1.0f);
    multiplyOffset.shininess = 1.0f;
    multiplyOffset.ambient = glm::vec3(1.0f);
    multiplyOffset.edgeColor = glm::vec4(1.0f);
    multiplyOffset.edgeSize = 2.0f;
    multiplyOffset.textureFactor = glm::vec4(0.5f, 1.0f, 1.0f, 1.0f);
    multiplyOffset.sphereTextureFactor = glm::vec4(1.0f);
    multiplyOffset.toonTextureFactor = glm::vec4(1.0f);
    MorphDefinition multiplyMaterialMorph;
    multiplyMaterialMorph.name = "materialMultiply";
    multiplyMaterialMorph.kind = MorphKind::Material;
    multiplyMaterialMorph.materialOffsets.push_back(multiplyOffset);

    MaterialMorphOffset addOffset;
    addOffset.materialIndex = 3U;
    addOffset.operation = MaterialMorphOperation::Add;
    addOffset.diffuse = glm::vec4(0.2f, 0.0f, 0.0f, 0.0f);
    addOffset.edgeSize = 0.4f;
    addOffset.textureFactor = glm::vec4(0.2f, 0.0f, 0.0f, 0.0f);
    MorphDefinition addMaterialMorph;
    addMaterialMorph.name = "materialAdd";
    addMaterialMorph.kind = MorphKind::Material;
    addMaterialMorph.materialOffsets.push_back(addOffset);

    MorphDefinition combinedMorph;
    combinedMorph.name = "combined";
    combinedMorph.kind = MorphKind::Group;
    combinedMorph.groupMembers = {
        GroupMorphMember{0U, 0.5f},
        GroupMorphMember{1U, 2.0f},
        GroupMorphMember{2U, 1.0f},
        GroupMorphMember{3U, 0.5f}
    };

    MorphSet morphSet({
        std::move(boneMorph),
        std::move(uvMorph),
        std::move(multiplyMaterialMorph),
        std::move(addMaterialMorph),
        std::move(combinedMorph)
    });
    Require(
        morphSet.HasKind(MorphKind::Bone) &&
        morphSet.HasKind(MorphKind::Uv) &&
        morphSet.HasKind(MorphKind::Material),
        "MorphSet did not retain extended MMD morph kinds"
    );

    MorphState state(morphSet);
    state.SetWeight("combined", 1.0f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[0U], 0.5f) &&
        NearlyEqual(effective[1U], 2.0f) &&
        NearlyEqual(effective[2U], 1.0f) &&
        NearlyEqual(effective[3U], 0.5f) &&
        NearlyEqual(effective[4U], 0.0f),
        "Group Morph did not expand Bone, UV, and Material leaves"
    );

    PoseBuffer poseBuffer(skeleton);
    morphSet.ApplyBoneMorphs(effective, poseBuffer);
    const BoneTransform& boneTransform = poseBuffer.TransformAt(0U);
    Require(
        NearlyEqual(boneTransform.translation, glm::vec3(1.0f, 0.0f, 0.0f)) &&
        NearlySameRotation(
            boneTransform.rotation,
            glm::angleAxis(
                glm::radians(45.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            )
        ),
        "Bone Morph did not apply weighted translation and rotation"
    );

    DefaultModelData data{
        {
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f
        },
        {0U, 1U, 2U},
        {{"position", 3, FLOAT}}
    };
    Mesh mesh(
        std::move(data),
        0U,
        {
            MeshMorphTarget{
                1U,
                {},
                {
                    UvMorphOffset{
                        1U,
                        0U,
                        glm::vec4(0.25f, 0.5f, 0.0f, 0.0f)
                    },
                    UvMorphOffset{
                        2U,
                        1U,
                        glm::vec4(0.1f, 0.2f, 0.3f, 0.4f)
                    }
                }
            }
        }
    );
    std::vector<MorphVertexDelta> deltas;
    Require(
        mesh.CalculateMorphDeltas(effective, deltas) &&
        deltas.size() == 3U &&
        NearlyEqual(
            deltas[1U].uv[0U],
            glm::vec4(0.5f, 1.0f, 0.0f, 0.0f)
        ) &&
        NearlyEqual(
            deltas[2U].uv[1U],
            glm::vec4(0.2f, 0.4f, 0.6f, 0.8f)
        ),
        "UV Morph did not accumulate the correct channel deltas"
    );

    MaterialMorphValues targetValues;
    targetValues.edgeSize = 1.0f;
    morphSet.ApplyMaterialMorphs(3U, effective, targetValues);
    Require(
        NearlyEqual(targetValues.diffuse.x, 0.6f) &&
        NearlyEqual(targetValues.edgeSize, 2.2f) &&
        NearlyEqual(targetValues.textureFactor.x, 0.6f),
        "Material Morph multiply/add composition is incorrect"
    );

    MaterialMorphValues otherValues;
    otherValues.edgeSize = 1.0f;
    morphSet.ApplyMaterialMorphs(4U, effective, otherValues);
    Require(
        NearlyEqual(otherValues.diffuse.x, 0.5f) &&
        NearlyEqual(otherValues.edgeSize, 2.0f) &&
        NearlyEqual(otherValues.textureFactor.x, 0.5f),
        "All-material Morph did not apply independently of targeted offsets"
    );

    Pose animatedPose(skeleton);
    MorphState animatedState(morphSet);
    Animator animator(animatedPose, &animatedState);
    animatedState.SetWeight("combined", 1.0f);
    animator.Update(0.0f);
    const BoneTransform animatedTransform = BoneTransform::FromMatrix(
        animatedPose.LocalMatrix(0U)
    );
    Require(
        NearlyEqual(
            animatedTransform.translation,
            glm::vec3(1.0f, 0.0f, 0.0f)
        ) &&
        NearlySameRotation(
            animatedTransform.rotation,
            glm::angleAxis(
                glm::radians(45.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            )
        ),
        "Animator did not re-evaluate a manually changed Bone Morph"
    );

    const AnimationClip heldClip(
        "held",
        1.0f,
        {
            AnimationTrack(
                0U,
                {VectorKeyframe{0.0f, glm::vec3(3.0f, 0.0f, 0.0f)}}
            )
        }
    );
    animatedState.Reset();
    animator.Play(heldClip);
    animator.Stop(false);
    animatedState.SetWeight("combined", 1.0f);
    animator.Update(0.0f);
    Require(
        NearlyEqual(
            BoneTransform::FromMatrix(animatedPose.LocalMatrix(0U))
                .translation,
            glm::vec3(4.0f, 0.0f, 0.0f)
        ),
        "Manual Bone Morph discarded a stopped animation pose"
    );

    animator.Stop(true);
    animatedState.SetWeight("combined", 1.0f);
    animator.Update(0.0f);
    Require(
        NearlyEqual(
            BoneTransform::FromMatrix(animatedPose.LocalMatrix(0U))
                .translation,
            glm::vec3(1.0f, 0.0f, 0.0f)
        ),
        "Animator Stop(true) retained a stale animation pose baseline"
    );
}

void TestSabaMmdImporterWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    RequireFullAsset("production-pmx-yeshiguang");

    SabaMmdImporter sabaImporter;
    ImportedModelData saba;
    try
    {
        saba = sabaImporter.Import(modelPath);
    }
    catch (const std::exception& error)
    {
        std::cout << "[SABA IMPORT FAIL] " << error.what() << std::endl;
        throw;
    }
    Require(
        saba.skeleton.has_value() &&
            saba.mmdPhysics.has_value() &&
            !saba.morphs.empty() &&
            !saba.meshes.empty() &&
            !saba.materials.empty(),
        "Saba importer produced an incomplete PMX model"
    );

    ImportedModelData assimp;
    try
    {
        assimp = ModelImporter().Import(modelPath);
    }
    catch (const std::exception& error)
    {
        std::cout << "[ASSIMP IMPORT FAIL] " << error.what() << std::endl;
        throw;
    }
    Require(
        assimp.skeleton.has_value() &&
            assimp.mmdPhysics.has_value() &&
            !assimp.morphs.empty() &&
            !assimp.meshes.empty() &&
            !assimp.materials.empty(),
        "Assimp importer produced an incomplete PMX model"
    );

    const std::size_t sabaBones = saba.skeleton->BoneCount();
    const std::size_t assimpBones = assimp.skeleton->BoneCount();
    const std::size_t sabaBodies = saba.mmdPhysics->RigidBodyCount();
    const std::size_t assimpBodies = assimp.mmdPhysics->RigidBodyCount();
    const std::size_t sabaJoints = saba.mmdPhysics->JointCount();
    const std::size_t assimpJoints = assimp.mmdPhysics->JointCount();

    std::unordered_set<std::string> sabaBoneNames;
    std::unordered_set<std::string> assimpBoneNames;
    for (BoneIndex index = 0U; index < sabaBones; ++index)
        sabaBoneNames.insert(saba.skeleton->BoneAt(index).name);
    for (BoneIndex index = 0U; index < assimpBones; ++index)
        assimpBoneNames.insert(assimp.skeleton->BoneAt(index).name);
    for (const std::string& name : assimpBoneNames)
    {
        if (sabaBoneNames.find(name) == sabaBoneNames.end())
            std::cout << "[SABA IMPORTER] only-assimp-bone: " << name
                      << std::endl;
    }
    for (const std::string& name : sabaBoneNames)
    {
        if (assimpBoneNames.find(name) == assimpBoneNames.end())
            std::cout << "[SABA IMPORTER] only-saba-bone: " << name
                      << std::endl;
    }

    std::cout << "[SABA IMPORTER] saba bones=" << sabaBones
              << " rigidBodies=" << sabaBodies
              << " joints=" << sabaJoints
              << " materials=" << saba.materials.size()
              << " morphs=" << saba.morphs.size()
              << " meshes=" << saba.meshes.size()
              << " | assimp bones=" << assimpBones
              << " rigidBodies=" << assimpBodies
              << " joints=" << assimpJoints
              << " materials=" << assimp.materials.size()
              << " morphs=" << assimp.morphs.size()
              << " meshes=" << assimp.meshes.size()
              << std::endl;

    Require(
        (sabaBones + 1U == assimpBones ||
            sabaBones == assimpBones ||
            sabaBones == assimpBones + 1U) &&
            sabaBodies == assimpBodies &&
            sabaJoints == assimpJoints &&
            saba.materials.size() == assimp.materials.size() &&
            saba.morphs.size() == assimp.morphs.size(),
        "Saba and Assimp PMX import disagree beyond one extra skeleton bone"
    );

    const std::filesystem::path smallPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    ImportedModelData small = sabaImporter.Import(smallPath);
    Require(
        small.mmdPhysics.has_value() &&
            small.mmdPhysics->RigidBodyCount() == 3U &&
            small.mmdPhysics->JointCount() == 6U,
        "Saba importer mismatched the PMX Physics 1 fixture"
    );
}

void TestSabaSkinningWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    const std::filesystem::path motionPath =
        FixturePath("production-vmd-body");
    RequireFullAsset("production-pmx-yeshiguang");
    RequireFullAsset("production-vmd-body");

    SabaMmdImporter importer;
    ImportedModelData imported = importer.Import(modelPath);
    Require(!imported.meshes.empty(), "Saba import produced no meshes");

    std::vector<glm::vec3> importedBindPositions;
    std::vector<unsigned int> importedIndices =
        imported.meshes[0U].data.indices;
    std::vector<std::uint32_t> mesh0SourceIndices =
        imported.meshes[0U].sourceVertexIndices;
    std::vector<std::vector<unsigned int>> allImportedIndices;
    allImportedIndices.reserve(imported.meshes.size());
    for (const ImportedMeshData& meshData : imported.meshes)
        allImportedIndices.push_back(meshData.data.indices);
    std::vector<std::vector<std::uint32_t>> allSourceIndices;
    allSourceIndices.reserve(imported.meshes.size());
    for (const ImportedMeshData& meshData : imported.meshes)
        allSourceIndices.push_back(meshData.sourceVertexIndices);
    {
        const std::vector<float>& vertices =
            imported.meshes[0U].data.vertices;
        constexpr std::size_t VertexStride = 26U;
        importedBindPositions.reserve(vertices.size() / VertexStride);
        for (std::size_t index = 0U;
             index + VertexStride <= vertices.size();
             index += VertexStride)
        {
            importedBindPositions.emplace_back(
                vertices[index],
                vertices[index + 1U],
                vertices[index + 2U]
            );
        }
    }

    Mesh mesh(
        std::move(imported.meshes[0U].data),
        imported.meshes[0U].requiredBoneCount,
        std::move(imported.meshes[0U].morphTargets),
        std::move(imported.meshes[0U].sourceVertexIndices)
    );

    SabaMmdRuntimeModel runtime(modelPath, motionPath);
    Require(runtime.Initialize(), "Saba runtime failed to initialize");
    Require(
        runtime.NeedsDynamicVertexUpload(),
        "Saba runtime must request dynamic vertex uploads"
    );

    const std::span<const glm::vec3> bindPositions =
        runtime.BindPositions();
    Require(
        importedBindPositions.size() == mesh0SourceIndices.size(),
        "Saba runtime and importer disagree on bind vertex count"
    );
    std::size_t bindMismatches = 0U;
    float maximumBindDifference = 0.0f;
    for (std::size_t index = 0U; index < importedBindPositions.size(); ++index)
    {
        const std::uint32_t globalIndex = mesh0SourceIndices[index];
        const float difference = glm::distance(
            bindPositions[globalIndex],
            importedBindPositions[index]
        );
        maximumBindDifference = std::max(
            maximumBindDifference,
            difference
        );
        if (difference > 0.01f)
            ++bindMismatches;
    }
    std::cout << "[SABA BIND] vertices=" << importedBindPositions.size()
              << " mismatches=" << bindMismatches
              << " maxDifference=" << maximumBindDifference
              << std::endl;
    Require(
        bindMismatches < bindPositions.size() / 100U,
        "Saba runtime and importer bind vertices are out of order or offset"
    );

    const std::vector<std::uint32_t> runtimeIndices =
        runtime.Indices();
    Require(
        !runtimeIndices.empty() &&
            importedIndices.size() <= runtimeIndices.size(),
        "Saba runtime indices are unavailable or shorter than the importer mesh"
    );
    std::size_t indexMismatches = 0U;
    for (std::size_t index = 0U; index < importedIndices.size(); ++index)
    {
        if (mesh0SourceIndices.size() <= importedIndices[index] ||
            runtimeIndices[index] !=
                mesh0SourceIndices[importedIndices[index]])
            ++indexMismatches;
    }
    std::cout << "[SABA INDEX] mesh0=" << importedIndices.size()
              << " runtime=" << runtimeIndices.size()
              << " mismatches=" << indexMismatches
              << std::endl;
    Require(
        indexMismatches == 0U,
        "Saba importer indices do not match the Saba runtime face order"
    );

    std::size_t runtimeIndexCursor = 0U;
    for (std::size_t meshIndex = 0U;
         meshIndex < allImportedIndices.size();
         ++meshIndex)
    {
        const std::vector<unsigned int>& meshIndices =
            allImportedIndices[meshIndex];
        const std::vector<std::uint32_t>& sourceIndices =
            allSourceIndices[meshIndex];
        std::size_t mismatches = 0U;
        for (std::size_t index = 0U; index < meshIndices.size(); ++index)
        {
            const bool validLocal =
                meshIndices[index] < sourceIndices.size();
            const std::uint32_t globalIndex =
                validLocal ? sourceIndices[meshIndices[index]] : 0U;
            if (runtimeIndexCursor + index >= runtimeIndices.size() ||
                !validLocal ||
                runtimeIndices[runtimeIndexCursor + index] != globalIndex)
            {
                ++mismatches;
            }
        }
        std::cout << "[SABA INDEX] mesh=" << meshIndex
                  << " indices=" << meshIndices.size()
                  << " mismatches=" << mismatches
                  << std::endl;
        Require(
            mismatches == 0U,
            "Saba importer sub-mesh indices are out of order"
        );
        runtimeIndexCursor += meshIndices.size();
    }

    constexpr int LongRunFrames = 600;
    for (int frame = 0; frame < LongRunFrames; ++frame)
    {
        runtime.Update(1.0f / 60.0f);
        if (frame == 59 || frame == 239 || frame == 479 ||
            frame == LongRunFrames - 1)
        {
            const SabaMmdRuntimeModel::VertexDiagnostics diagnostics =
                runtime.DiagnoseVertices();
            std::cout << "[SABA SKIN] frame=" << frame
                      << " finite="
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
        }
    }
    // R1.1E: dynamic geometry upload is a WISTERIA responsibility and flows
    // exclusively through ModelInstance. Reproduce the unified upload here to
    // verify the Saba vertex frame remains uploadable.
    const ModelVertexFrame vertexFrame = runtime.VertexFrame();
    Require(
        !vertexFrame.positions.empty() &&
        vertexFrame.positions.size() == vertexFrame.normals.size(),
        "Saba runtime produced an inconsistent vertex frame"
    );
    const std::span<const std::uint32_t> sourceIndices =
        mesh.SourceVertexIndices();
    if (sourceIndices.empty())
    {
        mesh.UploadDynamicVertices(
            vertexFrame.positions,
            vertexFrame.normals
        );
    }
    else
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        positions.reserve(sourceIndices.size());
        normals.reserve(sourceIndices.size());
        for (const std::uint32_t globalIndex : sourceIndices)
        {
            Require(
                globalIndex < vertexFrame.positions.size(),
                "Mesh source vertex index exceeds runtime vertex frame"
            );
            positions.push_back(vertexFrame.positions[globalIndex]);
            normals.push_back(vertexFrame.normals[globalIndex]);
        }
        mesh.UploadDynamicVertices(positions, normals);
    }
    Require(
        mesh.HasDynamicVertexSource(),
        "Saba runtime did not upload skinned vertices"
    );

    const SabaMmdRuntimeModel::VertexDiagnostics finalDiagnostics =
        runtime.DiagnoseVertices();
    Require(
        finalDiagnostics.finite,
        "Saba skinning produced non-finite vertices"
    );
    Require(
        finalDiagnostics.maximumDisplacementFromBind < 500.0f,
        "Saba skinning displaced vertices far beyond the bind pose"
    );

    const std::span<const glm::vec3> updatePositions =
        runtime.UpdatePositions();
    const std::span<const glm::vec3> bindPositions2 =
        runtime.BindPositions();
    std::size_t abnormalMeshCount = 0U;
    for (std::size_t meshIndex = 0U;
         meshIndex < allImportedIndices.size();
         ++meshIndex)
    {
        const std::vector<unsigned int>& meshIndices =
            allImportedIndices[meshIndex];
        const std::vector<std::uint32_t>& sourceIndices =
            allSourceIndices[meshIndex];
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(-std::numeric_limits<float>::max());
        float maximumDisplacement = 0.0f;
        bool finite = true;
        for (const unsigned int index : meshIndices)
        {
            if (index >= sourceIndices.size() ||
                sourceIndices[index] >= updatePositions.size() ||
                sourceIndices[index] >= bindPositions2.size())
            {
                finite = false;
                break;
            }
            const std::uint32_t globalIndex = sourceIndices[index];
            const glm::vec3& position = updatePositions[globalIndex];
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z))
            {
                finite = false;
                break;
            }
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
            maximumDisplacement = std::max(
                maximumDisplacement,
                glm::distance(position, bindPositions2[globalIndex])
            );
        }
        const bool abnormal =
            !finite ||
            maximumDisplacement > 30.0f ||
            std::abs(minimum.x) > 40.0f ||
            std::abs(maximum.x) > 40.0f ||
            minimum.y < -10.0f ||
            maximum.y > 40.0f ||
            std::abs(minimum.z) > 40.0f ||
            std::abs(maximum.z) > 40.0f;
        if (abnormal)
            ++abnormalMeshCount;
        std::cout << "[SABA MESH] mesh=" << meshIndex
                  << " name=\""
                  << imported.materials[meshIndex].name
                  << "\" alpha="
                  << static_cast<int>(
                        imported.materials[meshIndex].alphaMode
                    )
                  << " doubleSided="
                  << (imported.materials[meshIndex].doubleSided
                        ? "1" : "0")
                  << " indices=" << meshIndices.size()
                  << " finite=" << (finite ? "true" : "false")
                  << " maxDisp=" << maximumDisplacement
                  << " min=(" << minimum.x << ", "
                  << minimum.y << ", " << minimum.z << ")"
                  << " max=(" << maximum.x << ", "
                  << maximum.y << ", " << maximum.z << ")"
                  << " abnormal=" << (abnormal ? "true" : "false")
                  << std::endl;
    }
    Require(
        abnormalMeshCount == 0U,
        "At least one Saba sub-mesh has abnormal skinned vertices"
    );
}

void TestSabaImporterAcrossModelsWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path mmdDirectory =
        FixturePath("production-mmd-directory");
    RequireFullAssetDirectory("production-mmd-directory");

    std::vector<std::filesystem::path> candidates;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(mmdDirectory))
    {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".pmx")
        {
            candidates.push_back(entry.path());
        }
    }
    std::sort(candidates.begin(), candidates.end());

    std::size_t testedModels = 0U;
    for (const std::filesystem::path& modelPath : candidates)
    {
        const std::u8string u8Name = modelPath.filename().u8string();
        const std::string modelName(
            reinterpret_cast<const char*>(u8Name.data()),
            u8Name.size()
        );

        ImportedModelData saba;
        SabaMmdImporter sabaImporter;
        try
        {
            saba = sabaImporter.Import(modelPath);
        }
        catch (const std::exception& error)
        {
            std::cout << "[SABA MODEL] saba-import-fail: "
                      << modelName << ": " << error.what() << std::endl;
            Require(
                false,
                "Saba importer rejected a model: " + modelName
            );
        }
        Require(
            saba.skeleton.has_value() && !saba.meshes.empty(),
            "Saba cross-model import is incomplete: " + modelName
        );

        ImportedModelData assimp;
        bool assimpAvailable = true;
        try
        {
            assimp = ModelImporter().Import(modelPath);
        }
        catch (const std::exception& error)
        {
            assimpAvailable = false;
            std::cout << "[SABA MODEL] assimp-skip: "
                      << modelName << ": " << error.what() << std::endl;
        }

        const std::size_t sabaBones = saba.skeleton->BoneCount();
        std::cout << "[SABA MODEL] " << modelName
                  << " bones=" << sabaBones
                  << " bodies="
                  << (saba.mmdPhysics.has_value()
                        ? saba.mmdPhysics->RigidBodyCount()
                        : 0U)
                  << " joints="
                  << (saba.mmdPhysics.has_value()
                        ? saba.mmdPhysics->JointCount()
                        : 0U)
                  << " materials=" << saba.materials.size()
                  << " morphs=" << saba.morphs.size()
                  << " assimp=" << (assimpAvailable ? "ok" : "skip")
                  << std::endl;

        if (assimpAvailable)
        {
            Require(
                assimp.skeleton.has_value(),
                "Assimp cross-model comparison lost skeleton or physics"
            );
            const std::size_t assimpBones = assimp.skeleton->BoneCount();
            Require(
                (sabaBones + 1U == assimpBones ||
                    sabaBones == assimpBones ||
                    sabaBones == assimpBones + 1U) &&
                    saba.materials.size() == assimp.materials.size() &&
                    saba.morphs.size() == assimp.morphs.size(),
                "Saba/Assimp metadata mismatch on model: " + modelName
            );
            if (saba.mmdPhysics.has_value() &&
                assimp.mmdPhysics.has_value())
            {
                Require(
                    saba.mmdPhysics->RigidBodyCount() ==
                        assimp.mmdPhysics->RigidBodyCount() &&
                        saba.mmdPhysics->JointCount() ==
                            assimp.mmdPhysics->JointCount(),
                    "Saba/Assimp physics metadata mismatch on model: " +
                        modelName
                );
            }
        }
        ++testedModels;
    }
    Require(
        testedModels >= 3U,
        "Saba cross-model comparison did not cover enough models"
    );
}

void TestSabaMmdPhysicsLongRunWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    const std::filesystem::path motionPath =
        FixturePath("production-vmd-body");
    RequireFullAsset("production-pmx-yeshiguang");
    RequireFullAsset("production-vmd-body");

    SabaMmdRuntimeModel runtime(
        modelPath,
        motionPath,
        SabaPhysicsSettings{}
    );
    Require(runtime.Initialize(), "Saba physics runtime failed to initialize");
    PhysicsInstance* physics = runtime.TryGetPhysicsInstance();
    Require(
        physics != nullptr && physics->OwnsSimulationStep(),
        "Saba physics instance must own its simulation step"
    );

    for (int frame = 0; frame < 720; ++frame)
        runtime.Update(1.0f / 60.0f);

    const SabaMmdRuntimeModel::VertexDiagnostics diagnostics =
        runtime.DiagnoseVertices();
    Require(
        diagnostics.finite &&
            diagnostics.maximumDisplacementFromBind < 500.0f,
        "Saba physics long-run produced non-finite or runaway vertices"
    );

    // 120 Hz is the Saba default; the configurable interface must also accept
    // a 60 Hz / fewer-substep profile before Initialize().
    SabaMmdRuntimeModel configuredRuntime(
        modelPath,
        motionPath,
        SabaPhysicsSettings{}
    );
    SabaPhysicsSettings sixtyHz;
    sixtyHz.fixedTimeStep = 1.0f / 60.0f;
    sixtyHz.maxSubSteps = 4;
    configuredRuntime.SetPhysicsSettings(sixtyHz);
    Require(
        configuredRuntime.Initialize(),
        "Saba runtime with overridden physics settings failed to initialize"
    );
    // Live re-apply must also work after Initialize().
    configuredRuntime.SetPhysicsSettings(sixtyHz);
    for (int frame = 0; frame < 120; ++frame)
        configuredRuntime.Update(1.0f / 60.0f);
    const SabaMmdRuntimeModel::VertexDiagnostics configuredDiagnostics =
        configuredRuntime.DiagnoseVertices();
    Require(
        configuredDiagnostics.finite &&
            configuredDiagnostics.maximumDisplacementFromBind < 500.0f,
        "Saba physics settings override produced non-finite or runaway vertices"
    );
}

void TestSabaMmdPhysicsCompatBaselineWhenAvailable()
{
    RequireFullAssetsTier();
    // Phase 0/1 of the community physics adoption plan: a reproducible,
    // physics-only baseline on a frozen fixture. No motion, fixed 120 Hz
    // step, tight runaway bound; the trace export (WISTERIA_PHYSICS_TRACE)
    // feeds the cross-implementation comparison once reference traces from
    // babylon-mmd / libmmd / nanoem are available.
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    RequireFullAsset("production-pmx-yeshiguang");

    // Physics-only baseline: no VMD motion, so the measured displacement is
    // driven purely by the rigid-body world settling under gravity.
    SabaMmdRuntimeModel runtime(modelPath);
    Require(
        runtime.Initialize(),
        "Saba compat baseline runtime failed to initialize"
    );
    SabaPhysicsSettings settings;
    settings.fixedTimeStep = 1.0f / 120.0f;
    settings.maxSubSteps = 10;
    runtime.SetPhysicsSettings(settings);

    std::ofstream trace;
    const char* tracePath = std::getenv("WISTERIA_PHYSICS_TRACE");
    if (tracePath != nullptr && tracePath[0] != '\0')
    {
        trace.open(tracePath);
        Require(trace.is_open(), "Cannot open physics trace file");
        trace << "frame,min_x,min_y,min_z,max_x,max_y,max_z,"
              << "max_displacement\n";
    }

    constexpr int TotalFrames = 300;
    constexpr int SampleInterval = 10;
    float displacementAtFrame200 = 0.0f;
    for (int frame = 0; frame < TotalFrames; ++frame)
    {
        runtime.Update(1.0f / 120.0f);
        if ((frame + 1) % SampleInterval != 0)
            continue;

        const SabaMmdRuntimeModel::VertexDiagnostics diagnostics =
            runtime.DiagnoseVertices();
        Require(
            diagnostics.finite,
            "Compat baseline produced non-finite vertices"
        );
        Require(
            diagnostics.maximumDisplacementFromBind < 5.0f,
            "Compat baseline physics runaway"
        );
        if ((frame + 1) == 200)
        {
            displacementAtFrame200 =
                diagnostics.maximumDisplacementFromBind;
        }
        if ((frame + 1) == TotalFrames)
        {
            Require(
                std::abs(
                    diagnostics.maximumDisplacementFromBind -
                    displacementAtFrame200
                ) < 0.01f,
                "Compat baseline did not converge by the final sample"
            );
        }

        const Pose& pose = runtime.GetPose();
        for (std::size_t bone = 0U; bone < pose.BoneCount(); ++bone)
        {
            const glm::mat4& local = pose.LocalMatrix(
                static_cast<BoneIndex>(bone)
            );
            const bool finite = std::isfinite(local[0][0]) &&
                std::isfinite(local[0][1]) &&
                std::isfinite(local[0][2]) &&
                std::isfinite(local[0][3]) &&
                std::isfinite(local[1][0]) &&
                std::isfinite(local[1][1]) &&
                std::isfinite(local[1][2]) &&
                std::isfinite(local[1][3]) &&
                std::isfinite(local[2][0]) &&
                std::isfinite(local[2][1]) &&
                std::isfinite(local[2][2]) &&
                std::isfinite(local[2][3]) &&
                std::isfinite(local[3][0]) &&
                std::isfinite(local[3][1]) &&
                std::isfinite(local[3][2]) &&
                std::isfinite(local[3][3]);
            Require(
                finite,
                "Compat baseline pose contains a non-finite bone matrix"
            );
        }

        if (trace.is_open())
        {
            trace << (frame + 1) << ","
                  << diagnostics.minimumPosition.x << ","
                  << diagnostics.minimumPosition.y << ","
                  << diagnostics.minimumPosition.z << ","
                  << diagnostics.maximumPosition.x << ","
                  << diagnostics.maximumPosition.y << ","
                  << diagnostics.maximumPosition.z << ","
                  << diagnostics.maximumDisplacementFromBind << "\n";
        }
    }

    // Physics-disabled preset: saba's activation switch keeps the bodies
    // inactive, so the mesh must stay at the animation pose.
    SabaPhysicsSettings disabledSettings;
    disabledSettings.fixedTimeStep = 1.0f / 120.0f;
    disabledSettings.maxSubSteps = 10;
    disabledSettings.enabled = false;
    SabaMmdRuntimeModel disabledRuntime(modelPath);
    Require(
        disabledRuntime.Initialize(),
        "Saba disabled-physics runtime failed to initialize"
    );
    disabledRuntime.SetPhysicsSettings(disabledSettings);
    for (int frame = 0; frame < 60; ++frame)
        disabledRuntime.Update(1.0f / 120.0f);
    const SabaMmdRuntimeModel::VertexDiagnostics disabledDiagnostics =
        disabledRuntime.DiagnoseVertices();
    Require(
        disabledDiagnostics.finite &&
            disabledDiagnostics.maximumDisplacementFromBind < 1.0f,
        "Physics-disabled preset moved the mesh"
    );
}

void TestSabaMotionCameraLightInterfaceWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    const std::filesystem::path motionPath =
        FixturePath("production-vmd-body");
    const std::filesystem::path cameraPath =
        FixturePath("production-vmd-camera-gu");
    RequireFullAsset("production-pmx-yeshiguang");
    RequireFullAsset("production-vmd-body");
    RequireFullAsset("production-vmd-camera-gu");

    SabaMmdRuntimeModel runtime(modelPath, motionPath);
    Require(runtime.Initialize(), "Saba interface runtime failed to initialize");
    Require(runtime.HasMotion(), "Saba interface runtime has no motion");
    const double maxFrame = runtime.MotionMaxFrame();
    Require(maxFrame > 0.0, "Saba motion reports no key frames");

    // Non-looping advances past the last key and holds the end pose.
    runtime.SetMotionLooping(false);
    Require(!runtime.IsMotionLooping(), "Motion looping flag did not change");
    runtime.SetMotionFrame(maxFrame - 1.0);
    for (int frame = 0; frame < 5; ++frame)
        runtime.Update(1.0f / 30.0f);
    Require(
        runtime.MotionFrame() > maxFrame,
        "Non-looping motion did not advance past its last key"
    );

    // Looping wraps back into the clip.
    runtime.SetMotionLooping(true);
    runtime.SetMotionFrame(maxFrame - 1.0);
    runtime.Update(1.0f / 30.0f);
    Require(
        runtime.MotionFrame() < maxFrame,
        "Looping motion did not wrap at the last key"
    );

    // Pause freezes the frame, resume advances, restart rewinds.
    runtime.PauseMotion();
    Require(runtime.IsMotionPaused(), "Motion pause flag did not change");
    const double pausedFrame = runtime.MotionFrame();
    runtime.Update(1.0f / 30.0f);
    Require(
        NearlyEqual(runtime.MotionFrame(), pausedFrame),
        "Paused motion advanced its frame"
    );
    runtime.ResumeMotion();
    Require(!runtime.IsMotionPaused(), "Motion resume flag did not change");
    runtime.Update(1.0f / 30.0f);
    Require(
        runtime.MotionFrame() > pausedFrame,
        "Resumed motion did not advance its frame"
    );
    runtime.RestartMotion(true);
    Require(
        NearlyEqual(runtime.MotionFrame(), 0.0),
        "Restart did not rewind the motion frame"
    );

    // The interface can replace the loaded motion.
    Require(
        runtime.LoadMotion(motionPath) && runtime.HasMotion() &&
            NearlyEqual(runtime.MotionFrame(), 0.0),
        "LoadMotion did not replace the current motion"
    );

    // Camera interface: the camera VMD is a required FULL_ASSETS fixture.
    Require(
        runtime.LoadCameraMotion(cameraPath),
        "LoadCameraMotion rejected a camera VMD"
    );
    Camera camera;
    const std::optional<CameraTrackSample> cameraSample =
        runtime.SampleCameraMotion(10.0f);
    Require(
        cameraSample.has_value(),
        "SampleCameraMotion returned no sample for a loaded camera VMD"
    );
    camera.SetParam(ToCameraParam(*cameraSample, camera.GetParam()));
    const CameraParam& param = camera.GetParam();
    Require(
        std::isfinite(param.Position.x) &&
            std::isfinite(param.Position.y) &&
            std::isfinite(param.Position.z) &&
            std::isfinite(param.Target.x) &&
            std::isfinite(param.Target.y) &&
            std::isfinite(param.Target.z) &&
            param.VerticalFovDegrees > 0.0f &&
            param.VerticalFovDegrees < 180.0f,
        "VMD camera produced non-finite or invalid CameraParam"
    );

    // Light interface: VMDs may not carry light frames; both outcomes must be
    // safe, and the programmatic LightTrack path must always apply.
    DirectionalLight light;
    if (runtime.LoadLightMotion(motionPath))
    {
        const std::optional<LightTrackSample> sample =
            runtime.SampleLightMotion(0.0f);
        Require(
            sample.has_value(),
            "SampleLightMotion returned no sample for a loaded light track"
        );
        const DirectionalLightData emptyFallback{};
        light = DirectionalLight(ToLightData(*sample, emptyFallback));
        Require(
            glm::length(light.Direction()) > 0.0f,
            "VMD light motion produced a zero direction"
        );
    }
    LightTrack lightTrack({
        LightKeyframe{
            0.0f,
            {1.0f, 0.5f, 0.25f},
            {1.0f, 2.0f, 3.0f},
            {}
        },
        LightKeyframe{
            30.0f,
            {0.0f, 0.5f, 1.0f},
            {0.0f, 2.0f, 1.0f},
            {}
        }
    });
    LightKeyframe programmaticSample;
    Require(
        lightTrack.Sample(15.0f, programmaticSample),
        "Programmatic LightTrack did not sample"
    );
    light = DirectionalLight(ToLightData(LightTrackSample{
        15.0f,
        programmaticSample.color,
        programmaticSample.position
    }, DirectionalLightData{}));
    Require(
        NearlyEqual(light.Color(), glm::vec3(0.5f, 0.5f, 0.625f)) &&
            glm::length(light.Direction()) > 0.0f,
        "LightTrack did not apply to the directional light"
    );

    // R1.1 Fixup: applying a light sample must preserve the host light's
    // Intensity (the old ApplyLightMotion only changed Color/Direction).
    DirectionalLight preservedLight(DirectionalLightData{
        .Direction = {-0.35f, -0.75f, -0.45f},
        .Color = {1.0f, 0.96f, 0.92f},
        .Intensity = 0.75f
    });
    const DirectionalLightData fallback{
        .Direction = preservedLight.Direction(),
        .Color = preservedLight.Color(),
        .Intensity = preservedLight.Intensity()
    };
    preservedLight = DirectionalLight(ToLightData(
        LightTrackSample{0.0f, glm::vec3(1.0f, 0.5f, 0.25f), glm::vec3(1.0f, 2.0f, 3.0f)},
        fallback
    ));
    Require(
        NearlyEqual(preservedLight.Intensity(), 0.75f),
        "ToLightData changed the host light intensity"
    );
}

// Builds a minimal VMD containing two light keyframes and loads it through
// SabaMmdRuntimeModel::LoadLightMotion, verifying the real Saba light VMD
// sampling path (not just the programmatic LightTrack path).
void TestSabaLightVmdSampling()
{
    const auto writeLightVmd = [](const std::filesystem::path& path)
    {
        std::vector<std::uint8_t> bytes;
        const auto appendValue = [&bytes]<typename T>(const T& value)
        {
            const std::size_t offset = bytes.size();
            bytes.resize(offset + sizeof(T));
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
        };
        const auto appendFixed = [&bytes](
            std::string_view value,
            std::size_t size
        )
        {
            const std::size_t begin = bytes.size();
            bytes.resize(begin + size, 0U);
            const std::size_t copySize = std::min(value.size(), size);
            std::memcpy(bytes.data() + begin, value.data(), copySize);
        };

        appendFixed("Vocaloid Motion Data 0002", 30U);
        appendFixed("testModel", 20U);
        appendValue(std::uint32_t{0U});  // bone frames
        appendValue(std::uint32_t{0U});  // morph frames
        appendValue(std::uint32_t{0U});  // camera frames
        appendValue(std::uint32_t{2U});  // light frames
        const auto appendLightFrame = [&appendValue](
            std::uint32_t frame,
            const glm::vec3& color,
            const glm::vec3& position
        )
        {
            appendValue(frame);
            appendValue(color.x);
            appendValue(color.y);
            appendValue(color.z);
            appendValue(position.x);
            appendValue(position.y);
            appendValue(position.z);
        };
        appendLightFrame(
            0U,
            glm::vec3(1.0f, 0.9f, 0.8f),
            glm::vec3(1.0f, 1.0f, 0.5f)
        );
        appendLightFrame(
            30U,
            glm::vec3(0.4f, 0.6f, 1.0f),
            glm::vec3(-1.0f, 1.0f, -0.5f)
        );
        std::ofstream out(path, std::ios::binary);
        Require(out.is_open(), "Cannot write light VMD fixture");
        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        out.close();
    };

    const std::filesystem::path fixturePath =
        std::filesystem::temp_directory_path() /
        "wisteria_light_sampling_test.vmd";
    writeLightVmd(fixturePath);

    SabaMmdRuntimeModel runtime({});
    Require(
        runtime.LoadLightMotion(fixturePath),
        "Saba rejected a light-bearing VMD"
    );
    const std::optional<LightTrackSample> first =
        runtime.SampleLightMotion(0.0f);
    const std::optional<LightTrackSample> second =
        runtime.SampleLightMotion(30.0f);
    Require(
        first.has_value() && second.has_value(),
        "Saba light VMD sampling returned no sample"
    );
    Require(
        NearlyEqual(first->color, glm::vec3(1.0f, 0.9f, 0.8f)) &&
            NearlyEqual(first->position, glm::vec3(1.0f, 1.0f, -0.5f)),
        "Saba light VMD frame 0 sample is wrong"
    );
    Require(
        NearlyEqual(second->color, glm::vec3(0.4f, 0.6f, 1.0f)) &&
            NearlyEqual(second->position, glm::vec3(-1.0f, 1.0f, 0.5f)),
        "Saba light VMD frame 30 sample is wrong"
    );
    // Interpolated midpoint must be between the two keyframes.
    const std::optional<LightTrackSample> middle =
        runtime.SampleLightMotion(15.0f);
    Require(
        middle.has_value() &&
            middle->color.x > 0.4f && middle->color.x < 1.0f,
        "Saba light VMD interpolation did not blend keyframes"
    );

    std::error_code ignored;
    std::filesystem::remove(fixturePath, ignored);
}

// R1.1 Fixup: a VMD morph track writes weights through saba's internal
// MMDMorph objects, bypassing SetMorphWeight. MorphRevision must still
// advance during Update, and the persisted MorphSnapshot content must
// actually change between frames (not just the counter).
void TestMorphRevisionAdvancesOnVmdUpdate()
{
    const std::filesystem::path modelPath =
        FixturePath("extended-morph-pmx");
    RequireCoreAsset("extended-morph-pmx");

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("morphRevision", modelPath);
    Scene scene;
    Entity& entity = scene.InstantiateModel(model);
    Require(
        entity.HasModelInstance(),
        "Morph revision test entity has no ModelInstance"
    );
    ModelInstance& instance = entity.GetModelInstance();
    auto* runtime = dynamic_cast<MmdRuntimeModel*>(
        instance.TryGetRuntime()
    );
    Require(
        runtime != nullptr,
        "Morph revision test PMX did not resolve through Saba backend"
    );
    const std::uint64_t baseline = runtime->MorphRevision();

    // Build a VMD with two morph keyframes (frame 0 weight 0.0, frame 10
    // weight 1.0) and load it.
    const std::filesystem::path vmdPath =
        std::filesystem::temp_directory_path() /
        "wisteria_morph_revision_test.vmd";
    {
        std::vector<std::uint8_t> bytes;
        const auto appendValue = [&bytes]<typename T>(const T& value)
        {
            const std::size_t offset = bytes.size();
            bytes.resize(offset + sizeof(T));
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
        };
        const auto appendFixed = [&bytes](
            std::string_view value,
            std::size_t size
        )
        {
            const std::size_t begin = bytes.size();
            bytes.resize(begin + size, 0U);
            const std::size_t copySize = std::min(value.size(), size);
            std::memcpy(bytes.data() + begin, value.data(), copySize);
        };
        appendFixed("Vocaloid Motion Data 0002", 30U);
        appendFixed("testModel", 20U);
        appendValue(std::uint32_t{0U});  // bone frames
        appendValue(std::uint32_t{2U});  // morph frames
        const auto appendMorphFrame = [&appendFixed, &appendValue](
            std::string_view name,
            std::uint32_t frame,
            float weight
        )
        {
            appendFixed(name, 15U);
            appendValue(frame);
            appendValue(weight);
        };
        appendMorphFrame("vertex", 0U, 0.0f);
        appendMorphFrame("vertex", 10U, 1.0f);
        appendValue(std::uint32_t{0U});  // camera frames
        appendValue(std::uint32_t{0U});  // light frames
        std::ofstream out(vmdPath, std::ios::binary);
        Require(out.is_open(), "Cannot write morph revision VMD fixture");
        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        out.close();
    }

    Require(
        runtime->LoadMotion(vmdPath),
        "Morph revision VMD failed to load"
    );
    // Non-looping: frame advances must not wrap back into the clip.
    runtime->SetMotionLooping(false);
    const std::uint64_t afterLoad = runtime->MorphRevision();
    Require(
        afterLoad > baseline,
        "LoadMotion did not advance MorphRevision"
    );

    runtime->SetMotionFrame(0.0);
    scene.Update(1.0f / 60.0f);
    const std::uint64_t afterUpdate = runtime->MorphRevision();
    Require(
        afterUpdate > afterLoad,
        "VMD Update did not advance MorphRevision (snapshot would freeze)"
    );

    // The snapshot content at frame 0 must reflect the VMD weight 0.0.
    const ModelFrameSnapshot& frame0Snapshot =
        instance.CaptureSnapshot(CaptureMask::Morphs);
    float frame0Weight = -1.0f;
    for (const MorphEntrySnapshot& entry :
         frame0Snapshot.morphs.entries)
    {
        if (entry.name == "vertex")
            frame0Weight = entry.rawWeight;
    }
    Require(
        frame0Weight > -0.5f && frame0Weight < 0.5f,
        "MorphSnapshot frame 0 weight did not reflect the VMD"
    );

    // Advance to frame 10 and capture again: the weight must change to 1.0.
    runtime->SetMotionFrame(10.0);
    scene.Update(1.0f / 60.0f);
    const ModelFrameSnapshot& frame10Snapshot =
        instance.CaptureSnapshot(CaptureMask::Morphs);
    float frame10Weight = -1.0f;
    for (const MorphEntrySnapshot& entry :
         frame10Snapshot.morphs.entries)
    {
        if (entry.name == "vertex")
            frame10Weight = entry.rawWeight;
    }
    Require(
        frame10Weight > 0.5f,
        "MorphSnapshot frame 10 weight did not advance with the VMD"
    );
    Require(
        std::abs(frame10Weight - frame0Weight) > 0.5f,
        "MorphSnapshot content did not change between VMD frames"
    );

    std::error_code ignored;
    std::filesystem::remove(vmdPath, ignored);
}

// Golden regression: the WISTERIA MMD camera conversion must reproduce
// saba::MMDLookAtCamera's look-at output for the same MMD camera inputs.
void TestMmdCameraConversionMatchesSaba()
{
    const struct
    {
        glm::vec3 interest;
        glm::vec3 rotationDegrees;
        float distance;
        float fovDegrees;
    } cases[] = {
        {glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f), 50.0f, 30.0f},
        {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.0f, -10.0f, 2.0f), 30.0f, 45.0f},
        {glm::vec3(-3.0f, 5.0f, 7.0f), glm::vec3(-20.0f, 180.0f, 15.0f), 80.0f, 12.0f},
        {glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), -15.0f, 60.0f},
        {glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(-89.0f, 45.0f, 0.0f), 25.0f, 90.0f}
    };

    for (const auto& testCase : cases)
    {
        saba::MMDCamera mmdCamera;
        mmdCamera.m_interest = testCase.interest;
        mmdCamera.m_rotate = glm::radians(testCase.rotationDegrees);
        mmdCamera.m_distance = testCase.distance;
        mmdCamera.m_fov = glm::radians(testCase.fovDegrees);
        const saba::MMDLookAtCamera sabaLook(mmdCamera);

        CameraTrackSample sample;
        sample.interest = testCase.interest;
        sample.rotation = testCase.rotationDegrees;
        sample.distance = testCase.distance;
        sample.viewAngle = testCase.fovDegrees;
        const CameraParam ours = ToCameraParam(sample, CameraParam{});

        Require(
            NearlyEqual(ours.Position, sabaLook.m_eye) &&
                NearlyEqual(ours.Target, sabaLook.m_center) &&
                NearlyEqual(ours.Up, sabaLook.m_up),
            "WISTERIA MMD camera conversion diverged from saba MMDLookAtCamera"
        );
        Require(
            NearlyEqual(ours.VerticalFovDegrees, testCase.fovDegrees),
            "WISTERIA MMD camera conversion changed FOV"
        );
    }
}

#if defined(WISTERIA_TEST_NATIVE_ABI)

void TestNativeAbiLifecycle()
{
    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK && context != 0U,
        "wisteria_create_context failed"
    );
    Require(
        wisteria_version_major() == WISTERIA_NATIVE_VERSION_MAJOR &&
            wisteria_version_minor() == WISTERIA_NATIVE_VERSION_MINOR,
        "wisteria_native version mismatch"
    );

    char errorBuffer[256] = {};
    Require(
        wisteria_last_error_message(
            context,
            errorBuffer,
            sizeof(errorBuffer)
        ) == WISTERIA_OK,
        "wisteria_last_error_message query failed"
    );

    WisteriaModel model = 0U;
    Require(
        wisteria_load_model(context, nullptr, &model) ==
            WISTERIA_ERROR_INVALID_ARGUMENT,
        "null model path was accepted"
    );
    Require(
        wisteria_load_model(context, "", &model) ==
            WISTERIA_ERROR_INVALID_ARGUMENT,
        "empty model path was accepted"
    );
    Require(
        wisteria_load_model(context, "no/such/file.pmx", &model) ==
            WISTERIA_ERROR_IO,
        "missing model file did not report IO"
    );
    Require(
        wisteria_update(context, 1234U, 1.0f / 60.0f) ==
            WISTERIA_ERROR_NOT_FOUND,
        "invalid model handle was accepted"
    );
    Require(
        wisteria_destroy_context(999999U) == WISTERIA_ERROR_NOT_FOUND,
        "invalid context handle was accepted"
    );
    Require(
        wisteria_destroy_context(context) == WISTERIA_OK,
        "wisteria_destroy_context failed"
    );
}

// R1.S2: handle boundary semantics. Opaque, globally-unique handles mean a
// stale handle, a double destroy and a cross-context handle must all be
// rejected without crashing or touching another object.
void TestNativeAbiHandleBoundaries()
{
    const std::filesystem::path modelPath =
        FixturePath("extended-morph-pmx");
    RequireCoreAsset("extended-morph-pmx");
    const std::u8string modelPathU8 = modelPath.u8string();
    const std::string modelPathUtf8(
        reinterpret_cast<const char*>(modelPathU8.data()),
        modelPathU8.size()
    );

    WisteriaContext firstContext = 0U;
    WisteriaContext secondContext = 0U;
    Require(
        wisteria_create_context(&firstContext) == WISTERIA_OK &&
            wisteria_create_context(&secondContext) == WISTERIA_OK,
        "ABI handle boundary context creation failed"
    );

    // Create a real model in each context. With globally-unique opaque
    // handles the two model handles must differ.
    WisteriaModel firstModel = 0U;
    WisteriaModel secondModel = 0U;
    Require(
        wisteria_load_model(
            firstContext,
            modelPathUtf8.c_str(),
            &firstModel
        ) == WISTERIA_OK &&
            firstModel != 0U,
        "ABI first model load failed"
    );
    Require(
        wisteria_load_model(
            secondContext,
            modelPathUtf8.c_str(),
            &secondModel
        ) == WISTERIA_OK &&
            secondModel != 0U,
        "ABI second model load failed"
    );
    {
        // Cross-context: the first context's handle must not be accepted by
        // the second context (opaque handles are globally unique).
        const WisteriaStatus crossStatus = wisteria_update(
            secondContext,
            firstModel,
            1.0f / 60.0f
        );
        Require(
            crossStatus == WISTERIA_ERROR_NOT_FOUND,
            "Cross-context model handle did not report NOT_FOUND"
        );
    }

    // A context handle must never be accepted as a model handle.
    Require(
        wisteria_update(
            firstContext,
            firstContext,
            1.0f / 60.0f
        ) == WISTERIA_ERROR_NOT_FOUND,
        "Context handle was accepted as a model handle"
    );

    // The two handles must be distinct (globally unique allocation).
    Require(
        firstModel != secondModel,
        "Opaque handle allocator reused a value across contexts"
    );

    // Destroy the first context, then destroy it again: the second call must
    // be NOT_FOUND (double destroy is safe).
    Require(
        wisteria_destroy_context(firstContext) == WISTERIA_OK,
        "ABI first context destroy failed"
    );
    Require(
        wisteria_destroy_context(firstContext) == WISTERIA_ERROR_NOT_FOUND,
        "ABI double context destroy was accepted"
    );
    Require(
        wisteria_destroy_context(secondContext) == WISTERIA_OK,
        "ABI second context destroy failed"
    );

    // Context recreated after destroy must not reuse the destroyed context's
    // handle value, and the stale model handle must not hit the new context.
    WisteriaContext recreatedContext = 0U;
    Require(
        wisteria_create_context(&recreatedContext) == WISTERIA_OK,
        "ABI recreated context creation failed"
    );
    Require(
        recreatedContext != firstContext &&
            recreatedContext != secondContext,
        "Opaque context handle was reused after destroy"
    );
    WisteriaModel recreatedModel = 0U;
    Require(
        wisteria_load_model(
            recreatedContext,
            modelPathUtf8.c_str(),
            &recreatedModel
        ) == WISTERIA_OK,
        "ABI recreated context model load failed"
    );
    Require(
        recreatedModel != firstModel && recreatedModel != secondModel,
        "Opaque model handle was reused after context destroy"
    );
    Require(
        wisteria_update(
            recreatedContext,
            firstModel,
            1.0f / 60.0f
        ) == WISTERIA_ERROR_NOT_FOUND,
        "Stale model handle hit a recreated context"
    );
    Require(
        wisteria_destroy_context(recreatedContext) == WISTERIA_OK,
        "ABI recreated context destroy failed"
    );

    // Destroying a context invalidates its child handles: any later use of a
    // handle that belonged to it is NOT_FOUND, never a crash.
    WisteriaContext temporaryContext = 0U;
    Require(
        wisteria_create_context(&temporaryContext) == WISTERIA_OK,
        "ABI temporary context creation failed"
    );
    WisteriaWindow temporaryWindow = 0U;
    const WisteriaStatus windowStatus = wisteria_window_create(
        temporaryContext,
        160,
        120,
        "WISTERIA handle boundary",
        &temporaryWindow
    );
    if (windowStatus == WISTERIA_OK)
    {
        Require(
            wisteria_window_should_close(
                temporaryContext,
                temporaryWindow,
                nullptr
            ) == WISTERIA_ERROR_INVALID_ARGUMENT,
            "ABI null out handle was accepted"
        );
        int32_t closed = -1;
        Require(
            wisteria_window_should_close(
                temporaryContext,
                temporaryWindow,
                &closed
            ) == WISTERIA_OK,
            "ABI valid window query failed"
        );
        Require(
            wisteria_destroy_context(temporaryContext) == WISTERIA_OK,
            "ABI context with a live window failed to destroy"
        );
        Require(
            wisteria_window_should_close(
                temporaryContext,
                temporaryWindow,
                &closed
            ) == WISTERIA_ERROR_NOT_FOUND,
            "ABI child handle survived its context destroy"
        );
    }
    else
    {
        // No display backend available; the context itself must still be
        // destroyable and its handle must not survive.
        Require(
            wisteria_destroy_context(temporaryContext) == WISTERIA_OK,
            "ABI context without display failed to destroy"
        );
    }
}

// R1.S3: parent-first destroy cascade. Destroying a Window must invalidate
// every Scene bound to it (they reference the destroyed Window*), and a
// Scene handle must not survive its Window.
void TestNativeAbiWindowSceneCascade()
{
    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK,
        "ABI cascade context creation failed"
    );

    WisteriaWindow window = 0U;
    const WisteriaStatus createStatus = wisteria_window_create(
        context,
        320,
        240,
        "WISTERIA cascade test",
        &window
    );
    if (createStatus != WISTERIA_OK)
    {
        wisteria_destroy_context(context);
        SkipTest("window backend is unavailable in this environment");
    }

    WisteriaScene scene = 0U;
    Require(
        wisteria_scene_create(context, window, &scene) == WISTERIA_OK &&
            scene != 0U,
        "ABI cascade scene create failed"
    );

    // Destroy the window first: the bound scene references the Window* and
    // must be invalidated as part of the cascade.
    Require(
        wisteria_window_destroy(context, window) == WISTERIA_OK,
        "ABI cascade window destroy failed"
    );
    Require(
        wisteria_scene_destroy(context, scene) == WISTERIA_ERROR_NOT_FOUND,
        "ABI scene handle survived its window destroy"
    );

    // Recreating a window must allocate a fresh handle, never reuse the
    // destroyed one; the old scene handle stays invalid.
    WisteriaWindow secondWindow = 0U;
    if (wisteria_window_create(
            context,
            160,
            120,
            "WISTERIA cascade second",
            &secondWindow
        ) == WISTERIA_OK)
    {
        Require(
            secondWindow != window,
            "ABI window handle was reused after destroy"
        );
        wisteria_window_destroy(context, secondWindow);
    }

    Require(
        wisteria_destroy_context(context) == WISTERIA_OK,
        "ABI cascade context destroy failed"
    );
}

// R1.S1 fix: a pathological filesystem input (overlong path) must never let a
// C++ exception cross the extern "C" boundary. The call must return a status
// and leave the out handle untouched.
void TestNativeAbiExceptionBoundary()
{
    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK,
        "ABI exception boundary context creation failed"
    );

    // Overlong path: PathFromUtf8 and filesystem calls may throw; InvokeAbi
    // must convert that into a status code instead of propagating.
    std::string overlongPath(20000U, 'a');
    overlongPath += ".pmx";
    WisteriaModel model = 0U;
    const WisteriaStatus status = wisteria_load_model(
        context,
        overlongPath.c_str(),
        &model
    );
    Require(
        status != WISTERIA_OK,
        "Overlong path was accepted as a valid model"
    );
    Require(
        model == 0U,
        "Overlong path load wrote an out handle"
    );

    // Null and empty paths remain INVALID_ARGUMENT (parameter validation is
    // outside InvokeAbi but still inside the extern "C" boundary).
    Require(
        wisteria_load_model(context, nullptr, &model) ==
            WISTERIA_ERROR_INVALID_ARGUMENT,
        "Null model path was accepted"
    );
    Require(
        wisteria_load_model(context, "", &model) ==
            WISTERIA_ERROR_INVALID_ARGUMENT,
        "Empty model path was accepted"
    );

    Require(
        wisteria_destroy_context(context) == WISTERIA_OK,
        "ABI exception boundary context destroy failed"
    );
}

void TestNativeAbiWindowWhenAvailable()
{
    RequireFullAssetsTier();
    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK,
        "ABI window context creation failed"
    );

    WisteriaWindow window = 0U;
    const WisteriaStatus createStatus = wisteria_window_create(
        context,
        320,
        240,
        "WISTERIA ABI window test",
        &window
    );
    if (createStatus != WISTERIA_OK)
    {
        // No display available (e.g. headless CI): the window layer cannot
        // run, so skip the rest of this test.
        wisteria_destroy_context(context);
        SkipTest("window backend is unavailable in this environment");
    }

    // The renderer resolves shaders/assets relative to the current working
    // directory. CTest runs from build/, so temporarily switch to the
    // project root and restore it on every exit path.
    const std::filesystem::path previousWorkingDirectory =
        std::filesystem::current_path();
    try
    {
        std::filesystem::current_path(ProjectAssetDirectory.parent_path());

    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    const std::u8string modelPathU8 = modelPath.u8string();
    const std::string modelPathUtf8(
        reinterpret_cast<const char*>(modelPathU8.data()),
        modelPathU8.size()
    );
    const std::filesystem::path motionPath =
        FixturePath("production-vmd-motion");
    const std::u8string motionPathU8 = motionPath.u8string();
    const std::string motionPathUtf8(
        reinterpret_cast<const char*>(motionPathU8.data()),
        motionPathU8.size()
    );
    const std::filesystem::path scenePath =
        FixturePath("production-pmx-suibian");
    const std::u8string scenePathU8 = scenePath.u8string();
    const std::string scenePathUtf8(
        reinterpret_cast<const char*>(scenePathU8.data()),
        scenePathU8.size()
    );
    RequireFullAsset("production-pmx-yeshiguang");
    RequireFullAsset("production-vmd-motion");
    RequireFullAsset("production-pmx-suibian");

    Require(
        wisteria_window_load_demo(
            context,
            window,
            modelPathUtf8.c_str(),
            motionPathUtf8.c_str(),
            scenePathUtf8.c_str(),
            0.0f,
            0
        ) == WISTERIA_OK,
        "ABI window demo load failed"
    );
    for (int frame = 0; frame < 30; ++frame)
    {
        Require(
            wisteria_poll_and_render(
                context,
                1.0f / 60.0f
            ) == WISTERIA_OK,
            "ABI window render failed"
        );
    }

    int32_t closed = 1;
    Require(
        wisteria_window_should_close(context, window, &closed) ==
                WISTERIA_OK &&
            closed == 0,
        "ABI window reported close before it was requested"
    );

    float pose[9] = {};
    Require(
        wisteria_window_camera_pose(
            context,
            window,
            pose,
            pose + 3,
            pose + 6
        ) == WISTERIA_OK &&
            std::isfinite(pose[0]) &&
            std::isfinite(pose[3]) &&
            std::isfinite(pose[6]),
        "ABI window camera pose is invalid"
    );
    Require(
        wisteria_window_set_camera_speed(context, window, 5.0f) ==
            WISTERIA_OK,
        "ABI window camera speed failed"
    );

    struct WisteriaRenderSettings renderSettings = {};
    renderSettings.shadow_map_size = 2048;
    renderSettings.shadow_pcf_radius = 2;
    renderSettings.shadows_enabled = 1;
    renderSettings.ground_shadow_enabled = 1;
    renderSettings.shadow_bias = 0.003f;
    Require(
        wisteria_window_set_render_settings(
            context,
            window,
            &renderSettings
        ) == WISTERIA_OK,
        "ABI window render settings failed"
    );
    renderSettings.shadow_map_size = 64;
    Require(
        wisteria_window_set_render_settings(
            context,
            window,
            &renderSettings
        ) == WISTERIA_ERROR_INVALID_ARGUMENT,
        "ABI window accepted an invalid shadow map size"
    );
    Require(
        wisteria_window_set_render_settings(
            context,
            window,
            nullptr
        ) == WISTERIA_ERROR_INVALID_ARGUMENT,
        "ABI window accepted null render settings"
    );

    int32_t keyDown = 0;
    Require(
        wisteria_window_is_key_down(
            context,
            window,
            WISTERIA_KEY_SPACE,
            &keyDown
        ) == WISTERIA_OK,
        "ABI window key query failed"
    );
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    Require(
        wisteria_window_cursor_delta(
            context,
            window,
            &cursorX,
            &cursorY
        ) == WISTERIA_OK &&
            std::isfinite(cursorX) &&
            std::isfinite(cursorY),
        "ABI window cursor delta failed"
    );

    Require(
        wisteria_window_destroy(context, window) == WISTERIA_OK,
        "ABI window destroy failed"
    );
    }
    catch (...)
    {
        std::filesystem::current_path(previousWorkingDirectory);
        throw;
    }
    std::filesystem::current_path(previousWorkingDirectory);
    Require(
        wisteria_destroy_context(context) == WISTERIA_OK,
        "ABI window context destroy failed"
    );
}

void TestNativeAbiSabaWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    const std::filesystem::path motionPath =
        FixturePath("production-vmd-motion");
    RequireFullAsset("production-pmx-yeshiguang");
    RequireFullAsset("production-vmd-motion");

    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK,
        "ABI context creation failed"
    );
    WisteriaModel model = 0U;
    // The C ABI contract is UTF-8 paths; .string() would use the ANSI code
    // page on Windows and bypass the UTF-8 conversion in the wrapper.
    const std::u8string modelPathU8 = modelPath.u8string();
    const std::string modelPathUtf8(
        reinterpret_cast<const char*>(modelPathU8.data()),
        modelPathU8.size()
    );
    Require(
        wisteria_load_model(
            context,
            modelPathUtf8.c_str(),
            &model
        ) == WISTERIA_OK,
        "ABI model load failed"
    );
    WisteriaMotion motion = 0U;
    const std::u8string motionPathU8 = motionPath.u8string();
    const std::string motionPathUtf8(
        reinterpret_cast<const char*>(motionPathU8.data()),
        motionPathU8.size()
    );
    Require(
        wisteria_load_motion(
            context,
            model,
            motionPathUtf8.c_str(),
            &motion
        ) == WISTERIA_OK,
        "ABI motion load failed"
    );

    double maxFrame = 0.0;
    Require(
        wisteria_motion_max_frame(context, model, &maxFrame) == WISTERIA_OK &&
            maxFrame > 0.0,
        "ABI motion max frame is invalid"
    );
    Require(
        wisteria_play_motion(context, model, motion) == WISTERIA_OK,
        "ABI play motion failed"
    );
    for (int frame = 0; frame < 120; ++frame)
    {
        Require(
            wisteria_update(context, model, 1.0f / 60.0f) == WISTERIA_OK,
            "ABI update failed"
        );
    }

    double frame = 0.0;
    Require(
        wisteria_motion_frame(context, model, &frame) == WISTERIA_OK &&
            frame > 0.0,
        "ABI motion frame did not advance"
    );
    WisteriaVertexBounds bounds{};
    Require(
        wisteria_vertex_bounds(context, model, &bounds) == WISTERIA_OK &&
            bounds.finite == 1 &&
            bounds.vertexCount > 0U &&
            std::isfinite(bounds.maximumDisplacementFromBind),
        "ABI vertex bounds are invalid"
    );

    Require(
        wisteria_pause_motion(context, model) == WISTERIA_OK,
        "ABI pause motion failed"
    );
    const double pausedFrame = frame;
    Require(
        wisteria_update(context, model, 1.0f / 60.0f) == WISTERIA_OK,
        "ABI update while paused failed"
    );
    Require(
        wisteria_motion_frame(context, model, &frame) == WISTERIA_OK &&
            NearlyEqual(frame, pausedFrame),
        "Paused ABI motion advanced its frame"
    );
    Require(
        wisteria_resume_motion(context, model) == WISTERIA_OK,
        "ABI resume motion failed"
    );
    Require(
        wisteria_update(context, model, 1.0f / 60.0f) == WISTERIA_OK,
        "ABI update after resume failed"
    );
    Require(
        wisteria_motion_frame(context, model, &frame) == WISTERIA_OK &&
            frame > pausedFrame,
        "Resumed ABI motion did not advance"
    );

    Require(
        wisteria_set_motion_frame(context, model, 5.0) == WISTERIA_OK &&
            wisteria_motion_frame(context, model, &frame) == WISTERIA_OK &&
            NearlyEqual(frame, 5.0),
        "ABI set motion frame failed"
    );
    Require(
        wisteria_set_motion_looping(context, model, 1) == WISTERIA_OK &&
            wisteria_set_physics_settings(
                context,
                model,
                1.0f / 60.0f,
                4,
                0.0f,
                -98.0f,
                0.0f
            ) == WISTERIA_OK,
        "ABI looping or physics settings failed"
    );
    Require(
        wisteria_update(context, model, 1.0f / 60.0f) == WISTERIA_OK,
        "ABI update after settings failed"
    );

    Require(
        wisteria_unload_motion(context, model, motion) == WISTERIA_OK &&
            wisteria_unload_motion(context, model, motion) ==
                WISTERIA_ERROR_NOT_FOUND,
        "ABI motion unload did not invalidate the handle"
    );
    Require(
        wisteria_unload_model(context, model) == WISTERIA_OK &&
            wisteria_destroy_context(context) == WISTERIA_OK,
        "ABI model/context teardown failed"
    );
}

void TestNativeAbiMmdControl()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    const std::filesystem::path cameraPath =
        FixturePath("production-vmd-camera");
    RequireFullAsset("production-pmx-yeshiguang");
    RequireFullAsset("production-vmd-camera");

    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK,
        "ABI MMD control context creation failed"
    );
    const std::u8string modelPathU8 = modelPath.u8string();
    const std::string modelPathUtf8(
        reinterpret_cast<const char*>(modelPathU8.data()),
        modelPathU8.size()
    );
    WisteriaModel model = 0U;
    Require(
        wisteria_load_model(
            context,
            modelPathUtf8.c_str(),
            &model
        ) == WISTERIA_OK,
        "ABI MMD control model load failed"
    );

    uint32_t capabilities = 0U;
    Require(
        wisteria_physics_capabilities(context, model, &capabilities) ==
                WISTERIA_OK &&
            (capabilities & WISTERIA_PHYSICS_CAP_FIXED_STEP) != 0U &&
            (capabilities & WISTERIA_PHYSICS_CAP_GRAVITY) != 0U &&
            (capabilities & WISTERIA_PHYSICS_CAP_ENABLED) != 0U,
        "ABI physics capabilities did not advertise engine-backed knobs"
    );

    struct WisteriaPhysicsPreset preset = {};
    preset.fixed_time_step = 1.0f / 120.0f;
    preset.max_sub_steps = 10;
    preset.gravity[0] = 0.0f;
    preset.gravity[1] = -98.0f;
    preset.gravity[2] = 0.0f;
    preset.physics_enabled = 1;
    Require(
        wisteria_set_physics_preset(context, model, &preset) == WISTERIA_OK,
        "ABI physics preset failed"
    );
    preset.physics_enabled = 2;
    Require(
        wisteria_set_physics_preset(context, model, &preset) ==
            WISTERIA_ERROR_INVALID_ARGUMENT,
        "ABI physics preset accepted an invalid enabled value"
    );
    preset.physics_enabled = 0;
    Require(
        wisteria_set_physics_preset(context, model, &preset) == WISTERIA_OK &&
            wisteria_physics_reset(context, model) == WISTERIA_OK,
        "ABI physics disable or reset failed"
    );

    uint32_t boneIndex = 0U;
    Require(
        wisteria_find_bone_index(context, model, nullptr, &boneIndex) ==
            WISTERIA_ERROR_INVALID_ARGUMENT,
        "ABI bone lookup accepted a null name"
    );
    Require(
        wisteria_find_bone_index(
            context,
            model,
            "no_such_bone_for_abi_test",
            &boneIndex
        ) == WISTERIA_ERROR_NOT_FOUND,
        "ABI bone lookup accepted an unknown bone"
    );
    Require(
        wisteria_set_mmd_ik_enabled(context, model, 0U, 1) == WISTERIA_OK,
        "ABI IK switch failed"
    );
    Require(
        wisteria_set_mmd_ik_enabled(context, 1234U, 0U, 1) ==
            WISTERIA_ERROR_NOT_FOUND,
        "ABI IK switch accepted an invalid model"
    );

    const std::u8string cameraPathU8 = cameraPath.u8string();
    const std::string cameraPathUtf8(
        reinterpret_cast<const char*>(cameraPathU8.data()),
        cameraPathU8.size()
    );
    Require(
        wisteria_load_camera_motion(
            context,
            model,
            cameraPathUtf8.c_str()
        ) == WISTERIA_OK,
        "ABI camera motion load failed"
    );
    Require(
        wisteria_load_camera_motion(
            context,
            model,
            "no/such/camera.vmd"
        ) == WISTERIA_ERROR_IO,
        "ABI camera motion accepted a missing file"
    );

    Require(
        wisteria_unload_model(context, model) == WISTERIA_OK &&
            wisteria_destroy_context(context) == WISTERIA_OK,
        "ABI MMD control teardown failed"
    );
}

void TestNativeAbiSceneWhenAvailable()
{
    RequireFullAssetsTier();
    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK,
        "ABI scene context creation failed"
    );
    WisteriaWindow window = 0U;
    if (wisteria_window_create(
            context,
            320,
            240,
            "WISTERIA ABI scene test",
            &window
        ) != WISTERIA_OK)
    {
        wisteria_destroy_context(context);
        SkipTest("window backend is unavailable in this environment");
    }

    const std::filesystem::path previousWorkingDirectory =
        std::filesystem::current_path();
    try
    {
        const std::filesystem::path projectRoot =
            std::filesystem::path(WISTERIA_PROJECT_ASSET_DIR).parent_path();
        std::filesystem::current_path(projectRoot);

        const std::filesystem::path modelPath =
            FixturePath("production-pmx-yeshiguang");
        RequireFullAsset("production-pmx-yeshiguang");
        const std::u8string modelPathU8 = modelPath.u8string();
        const std::string modelPathUtf8(
            reinterpret_cast<const char*>(modelPathU8.data()),
            modelPathU8.size()
        );
        const std::string morphModelPath =
            FixturePath("extended-morph-pmx").string();

        WisteriaScene scene = 0U;
        Require(
            wisteria_scene_create(context, window, &scene) == WISTERIA_OK &&
                scene != 0U,
            "ABI scene create failed"
        );
        Require(
            wisteria_scene_load_model(
                context,
                scene,
                modelPathUtf8.c_str(),
                nullptr
            ) == WISTERIA_ERROR_INVALID_ARGUMENT,
            "ABI scene accepted a null model out-handle"
        );
        WisteriaSceneModel model = 0U;
        Require(
            wisteria_scene_load_model(
                context,
                scene,
                modelPathUtf8.c_str(),
                &model
            ) == WISTERIA_OK &&
                model != 0U,
            "ABI scene model load failed"
        );

        const float position[3] = {0.0f, 0.0f, 0.0f};
        const float euler[3] = {0.0f, 0.0f, 0.0f};
        const float scale[3] = {1.0f, 1.0f, 1.0f};
        WisteriaEntity entity = 0U;
        Require(
            wisteria_scene_instantiate_model(
                context,
                scene,
                model,
                position,
                euler,
                scale,
                &entity
            ) == WISTERIA_OK &&
                entity != 0U,
            "ABI scene instantiate failed"
        );
        Require(
            wisteria_entity_set_transform(
                context,
                scene,
                entity,
                position,
                euler,
                scale
            ) == WISTERIA_OK,
            "ABI entity transform failed"
        );
        float outPosition[3] = {};
        float outEuler[3] = {};
        float outScale[3] = {};
        Require(
            wisteria_entity_get_transform(
                context,
                scene,
                entity,
                outPosition,
                outEuler,
                outScale
            ) == WISTERIA_OK &&
                NearlyEqual(outPosition[0], position[0]) &&
                NearlyEqual(outEuler[1], euler[1]) &&
                NearlyEqual(outScale[2], scale[2]),
            "ABI entity transform get failed"
        );
        int32_t visible = 0;
        Require(
            wisteria_entity_get_visible(
                context,
                scene,
                entity,
                &visible
            ) == WISTERIA_OK,
            "ABI entity visibility get failed"
        );
        Require(
            wisteria_entity_set_visible(context, scene, entity, 1) ==
                    WISTERIA_OK &&
                wisteria_entity_set_visible(context, scene, entity, 0) ==
                    WISTERIA_OK,
            "ABI entity visibility failed"
        );

        const float direction[3] = {-0.35f, -0.75f, -0.45f};
        const float color[3] = {1.0f, 0.96f, 0.92f};
        WisteriaLight light = 0U;
        Require(
            wisteria_scene_add_directional_light(
                context,
                scene,
                direction,
                color,
                1.0f,
                &light
            ) == WISTERIA_OK &&
                light != 0U,
            "ABI scene directional light failed"
        );
        const float lightPosition[3] = {5.0f, 13.0f, 9.0f};
        WisteriaLight extraLight = 0U;
        Require(
            wisteria_scene_add_point_light(
                context,
                scene,
                lightPosition,
                color,
                1.0f,
                35.0f,
                &extraLight
            ) == WISTERIA_OK,
            "ABI scene point light failed"
        );
        Require(
            wisteria_scene_add_point_light(
                context,
                scene,
                lightPosition,
                color,
                1.0f,
                -1.0f,
                &extraLight
            ) == WISTERIA_ERROR_INVALID_ARGUMENT,
            "ABI scene accepted a negative light range"
        );

        // Light update + destroy.
        const float newDirection[3] = {-0.2f, -1.0f, -0.3f};
        const float warmColor[3] = {1.0f, 0.9f, 0.8f};
        Require(
            wisteria_directional_light_set(
                context,
                scene,
                light,
                newDirection,
                warmColor,
                0.8f
            ) == WISTERIA_OK,
            "ABI directional light update failed"
        );
        float outDirection[3] = {};
        float outColor[3] = {};
        float outIntensity = 0.0f;
        float range = 0.0f;
        const float directionLength = std::sqrt(
            newDirection[0] * newDirection[0] +
            newDirection[1] * newDirection[1] +
            newDirection[2] * newDirection[2]
        );
        Require(
            wisteria_directional_light_get(
                context,
                scene,
                light,
                outDirection,
                outColor,
                &outIntensity
            ) == WISTERIA_OK &&
                NearlyEqual(
                    outDirection[0],
                    newDirection[0] / directionLength
                ) &&
                NearlyEqual(outColor[1], warmColor[1]) &&
                NearlyEqual(outIntensity, 0.8f),
            "ABI directional light get failed"
        );
        const float lightPosition2[3] = {0.0f, 5.0f, 0.0f};
        WisteriaLight pointLight = 0U;
        Require(
            wisteria_scene_add_point_light(
                context,
                scene,
                lightPosition2,
                warmColor,
                1.0f,
                20.0f,
                &pointLight
            ) == WISTERIA_OK &&
                wisteria_point_light_set(
                    context,
                    scene,
                    pointLight,
                    lightPosition2,
                    warmColor,
                    1.2f,
                    25.0f
                ) == WISTERIA_OK &&
                wisteria_point_light_get(
                    context,
                    scene,
                    pointLight,
                    outPosition,
                    outColor,
                    &outIntensity,
                    &range
                ) == WISTERIA_OK &&
                NearlyEqual(outIntensity, 1.2f) &&
                NearlyEqual(range, 25.0f) &&
                wisteria_light_destroy(context, scene, pointLight) ==
                    WISTERIA_OK &&
                wisteria_light_destroy(context, scene, pointLight) ==
                    WISTERIA_ERROR_NOT_FOUND,
            "ABI point light lifecycle failed"
        );

        const float spotDirection[3] = {-0.4f, -1.0f, -0.5f};
        WisteriaLight spotLight = 0U;
        Require(
            wisteria_scene_add_spot_light(
                context,
                scene,
                lightPosition2,
                spotDirection,
                warmColor,
                2.0f,
                20.0f,
                12.5f,
                20.0f,
                &spotLight
            ) == WISTERIA_OK &&
                wisteria_spot_light_set(
                    context,
                    scene,
                    spotLight,
                    lightPosition2,
                    spotDirection,
                    warmColor,
                    2.5f,
                    25.0f,
                    10.0f,
                    18.0f
                ) == WISTERIA_OK &&
                wisteria_light_destroy(context, scene, spotLight) ==
                    WISTERIA_OK &&
                wisteria_light_destroy(context, scene, spotLight) ==
                    WISTERIA_ERROR_NOT_FOUND,
            "ABI spot light lifecycle failed"
        );
        Require(
            wisteria_scene_add_spot_light(
                context,
                scene,
                lightPosition2,
                spotDirection,
                warmColor,
                2.0f,
                20.0f,
                25.0f,
                20.0f,
                &spotLight
            ) == WISTERIA_ERROR_INVALID_ARGUMENT,
            "ABI spot light accepted invalid cutoffs"
        );

        // Morph weights on a fixture with known morph names.
        WisteriaSceneModel morphModel = 0U;
        float weight = 0.0f;
        Require(
            wisteria_scene_load_model(
                context,
                scene,
                morphModelPath.c_str(),
                &morphModel
            ) == WISTERIA_OK,
            "ABI scene morph model load failed"
        );
        WisteriaEntity morphEntity = 0U;
        Require(
            wisteria_scene_instantiate_model(
                context,
                scene,
                morphModel,
                position,
                euler,
                scale,
                &morphEntity
            ) == WISTERIA_OK,
            "ABI scene morph entity instantiate failed"
        );
        Require(
            wisteria_entity_set_morph_weight(
                context,
                scene,
                morphEntity,
                "vertex",
                0.5f
            ) == WISTERIA_OK &&
                wisteria_entity_get_morph_weight(
                    context,
                    scene,
                    morphEntity,
                    "vertex",
                    &weight
                ) == WISTERIA_OK &&
                NearlyEqual(weight, 0.5f),
            "ABI entity morph weight failed"
        );
        Require(
            wisteria_entity_set_morph_weight(
                context,
                scene,
                morphEntity,
                "no_such_morph",
                0.5f
            ) == WISTERIA_ERROR_NOT_FOUND,
            "ABI entity accepted an unknown morph"
        );

        // Environment + primitives.
        Require(
            wisteria_scene_set_environment(context, scene, 1, -1.0f) ==
                WISTERIA_OK,
            "ABI scene environment failed"
        );
        const float cubeColor[3] = {0.9f, 0.3f, 0.2f};
        const float cubePosition[3] = {0.0f, 1.0f, 0.0f};
        WisteriaEntity cube = 0U;
        WisteriaEntity ground = 0U;
        WisteriaEntity sphere = 0U;
        WisteriaEntity cylinder = 0U;
        WisteriaEntity capsule = 0U;
        WisteriaEntity cone = 0U;
        WisteriaEntity torus = 0U;
        Require(
            wisteria_scene_add_cube(
                context,
                scene,
                1.0f,
                cubeColor,
                cubePosition,
                &cube
            ) == WISTERIA_OK &&
                wisteria_scene_add_ground_plane(
                    context,
                    scene,
                    40.0f,
                    position,
                    &ground
                ) == WISTERIA_OK &&
                wisteria_scene_add_sphere(
                    context,
                    scene,
                    0.5f,
                    8,
                    12,
                    cubeColor,
                    cubePosition,
                    &sphere
                ) == WISTERIA_OK &&
                wisteria_scene_add_cylinder(
                    context,
                    scene,
                    0.3f,
                    1.0f,
                    12,
                    cubeColor,
                    cubePosition,
                    &cylinder
                ) == WISTERIA_OK &&
                wisteria_scene_add_capsule(
                    context,
                    scene,
                    0.3f,
                    1.0f,
                    12,
                    cubeColor,
                    cubePosition,
                    &capsule
                ) == WISTERIA_OK &&
                wisteria_scene_add_cone(
                    context,
                    scene,
                    0.4f,
                    1.0f,
                    12,
                    cubeColor,
                    cubePosition,
                    &cone
                ) == WISTERIA_OK &&
                wisteria_scene_add_torus(
                    context,
                    scene,
                    0.5f,
                    0.2f,
                    16,
                    8,
                    cubeColor,
                    cubePosition,
                    &torus
                ) == WISTERIA_OK &&
                cube != 0U &&
                ground != 0U &&
                sphere != 0U &&
                cylinder != 0U &&
                capsule != 0U &&
                cone != 0U &&
                torus != 0U,
            "ABI scene primitives failed"
        );

        const float blue[3] = {0.1f, 0.3f, 0.9f};
        Require(
            wisteria_entity_set_part_color(
                context,
                scene,
                cube,
                0,
                blue
            ) == WISTERIA_OK,
            "ABI entity part color failed"
        );
        Require(
            wisteria_entity_set_part_color(
                context,
                scene,
                cube,
                99,
                blue
            ) == WISTERIA_ERROR_NOT_FOUND,
            "ABI entity part color accepted an out-of-range index"
        );

        for (int frame = 0; frame < 20; ++frame)
        {
            const WisteriaStatus renderStatus =
                wisteria_poll_and_render(context, 1.0f / 60.0f);
            if (renderStatus != WISTERIA_OK)
            {
                char errorBuffer[512] = {};
                wisteria_last_error_message(
                    context,
                    errorBuffer,
                    sizeof(errorBuffer)
                );
                std::fprintf(
                    stderr,
                    "ABI scene render failed with: %s\n",
                    errorBuffer
                );
            }
            Require(renderStatus == WISTERIA_OK, "ABI scene render failed");
        }

        // Render readback.
        int32_t frameWidth = 0;
        int32_t frameHeight = 0;
        Require(
            wisteria_window_framebuffer_size(
                context,
                window,
                &frameWidth,
                &frameHeight
            ) == WISTERIA_OK &&
                frameWidth > 0 &&
                frameHeight > 0,
            "ABI framebuffer size query failed"
        );
        std::vector<unsigned char> pixels(
            static_cast<std::size_t>(frameWidth) *
            static_cast<std::size_t>(frameHeight) * 4U
        );
        Require(
            wisteria_window_read_pixels(
                context,
                window,
                pixels.data(),
                pixels.size()
            ) == WISTERIA_OK,
            "ABI readback failed"
        );
        bool hasContent = false;
        for (const unsigned char value : pixels)
        {
            if (value != 0U)
            {
                hasContent = true;
                break;
            }
        }
        Require(hasContent, "ABI readback returned an all-zero frame");

        Require(
            wisteria_entity_destroy(context, scene, entity) == WISTERIA_OK &&
                wisteria_entity_destroy(context, scene, entity) ==
                    WISTERIA_ERROR_NOT_FOUND,
            "ABI entity destroy did not invalidate the handle"
        );
        Require(
            wisteria_scene_destroy(context, scene) == WISTERIA_OK &&
                wisteria_scene_destroy(context, scene) ==
                    WISTERIA_ERROR_NOT_FOUND,
            "ABI scene destroy did not invalidate the handle"
        );
    }
    catch (...)
    {
        std::filesystem::current_path(previousWorkingDirectory);
        throw;
    }
    std::filesystem::current_path(previousWorkingDirectory);
    Require(
        wisteria_window_destroy(context, window) == WISTERIA_OK &&
            wisteria_destroy_context(context) == WISTERIA_OK,
        "ABI scene teardown failed"
    );
}

void TestNativeAbiHeadlessRenderWhenAvailable()
{
    WisteriaContext context = 0U;
    Require(
        wisteria_create_context(&context) == WISTERIA_OK,
        "ABI headless context creation failed"
    );
    WisteriaWindow window = 0U;
    if (wisteria_window_create_hidden(
            context,
            160,
            120,
            &window
        ) != WISTERIA_OK)
    {
        wisteria_destroy_context(context);
        SkipTest("window backend is unavailable in this environment");
    }

    const std::filesystem::path previousWorkingDirectory =
        std::filesystem::current_path();
    try
    {
        std::filesystem::current_path(
            std::filesystem::path(WISTERIA_PROJECT_ASSET_DIR).parent_path()
        );

    WisteriaScene scene = 0U;
    Require(
        wisteria_scene_create(context, window, &scene) == WISTERIA_OK,
        "ABI headless scene create failed"
    );
    const float position[3] = {0.0f, 0.0f, 0.0f};
    const float cubeColor[3] = {0.9f, 0.3f, 0.2f};
    const float cubePosition[3] = {0.0f, 0.5f, 0.0f};
    WisteriaEntity cube = 0U;
    Require(
        wisteria_scene_add_cube(
            context,
            scene,
            1.0f,
            cubeColor,
            cubePosition,
            &cube
        ) == WISTERIA_OK,
        "ABI headless cube failed"
    );
    const float direction[3] = {-0.35f, -0.75f, -0.45f};
    const float lightColor[3] = {1.0f, 0.96f, 0.92f};
    WisteriaLight light = 0U;
    Require(
        wisteria_scene_add_directional_light(
            context,
            scene,
            direction,
            lightColor,
            1.0f,
            &light
        ) == WISTERIA_OK,
        "ABI headless light failed"
    );

    for (int frame = 0; frame < 10; ++frame)
    {
        Require(
            wisteria_poll_and_render(context, 1.0f / 60.0f) == WISTERIA_OK,
            "ABI headless render failed"
        );
    }
    int32_t width = 0;
    int32_t height = 0;
    const WisteriaStatus sizeStatus = wisteria_window_framebuffer_size(
        context,
        window,
        &width,
        &height
    );
    std::fprintf(
        stderr,
        "ABI headless framebuffer size: status=%d %dx%d\n",
        static_cast<int>(sizeStatus),
        width,
        height
    );
    Require(
        sizeStatus == WISTERIA_OK &&
            width > 0 &&
            height > 0,
        "ABI headless framebuffer size is invalid"
    );
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * height * 4U
    );
    Require(
        wisteria_window_read_pixels(
            context,
            window,
            pixels.data(),
            pixels.size()
        ) == WISTERIA_OK,
        "ABI headless readback failed"
    );
    bool hasContent = false;
    for (const unsigned char value : pixels)
    {
        if (value != 0U)
        {
            hasContent = true;
            break;
        }
    }
    Require(
        hasContent,
        "ABI headless readback returned an all-zero frame"
    );

        Require(
            wisteria_scene_destroy(context, scene) == WISTERIA_OK &&
                wisteria_window_destroy(context, window) == WISTERIA_OK &&
                wisteria_destroy_context(context) == WISTERIA_OK,
            "ABI headless teardown failed"
        );
    }
    catch (...)
    {
        std::filesystem::current_path(previousWorkingDirectory);
        throw;
    }
    std::filesystem::current_path(previousWorkingDirectory);
}

#endif

void TestStaticModelImporter()
{
    const ImportedModelData imported = ModelImporter().Import(
        FixturePath("triangle-gltf")
    );
    RequireCoreAsset("triangle-gltf");

    Require(imported.meshes.size() == 1, "Importer mesh count is incorrect");
    Require(imported.materials.size() == 1, "Importer material count is incorrect");
    Require(
        imported.textures.size() == 4,
        "Importer embedded texture count is incorrect: " +
            std::to_string(imported.textures.size())
    );
    Require(imported.parts.size() == 2, "Importer did not preserve both node instances");

    const ImportedMeshData& mesh = imported.meshes[0];
    Require(mesh.data.vertices.size() == 45, "Importer vertex layout is incorrect");
    Require(mesh.data.indices.size() == 3, "Importer index count is incorrect");
    Require(mesh.data.layout.size() == 5, "Importer layout field count is incorrect");
    Require(mesh.materialIndex == 0, "Importer mesh material index is incorrect");
    Require(
        NearlyEqual(mesh.data.vertices[6], 0.0f) &&
        NearlyEqual(mesh.data.vertices[7], 0.0f) &&
        NearlyEqual(mesh.data.vertices[21], 1.0f) &&
        NearlyEqual(mesh.data.vertices[22], 0.0f) &&
        NearlyEqual(mesh.data.vertices[36], 0.0f) &&
        NearlyEqual(mesh.data.vertices[37], 1.0f),
        "Importer vertically flipped glTF texture coordinates"
    );
    for (std::size_t vertex = 0; vertex < 3; ++vertex)
    {
        const std::size_t tangentOffset = vertex * 15 + 11;
        const glm::vec3 tangent(
            mesh.data.vertices[tangentOffset],
            mesh.data.vertices[tangentOffset + 1],
            mesh.data.vertices[tangentOffset + 2]
        );
        Require(
            NearlyEqual(glm::length(tangent), 1.0f),
            "Importer produced a non-unit tangent"
        );
        Require(
            NearlyEqual(std::abs(mesh.data.vertices[tangentOffset + 3]), 1.0f),
            "Importer produced invalid tangent handedness"
        );
    }

    const ImportedMaterialData& material = imported.materials[0];
    Require(material.baseColorTexture == 0, "Importer lost base-color texture binding");
    Require(material.normalTexture == 1, "Importer lost normal texture binding");
    Require(
        material.metallicRoughnessTexture == 2,
        "Importer lost metallic-roughness texture binding"
    );
    Require(material.occlusionTexture == 2, "Importer lost occlusion texture binding");
    Require(material.emissiveTexture == 3, "Importer lost emissive texture binding");
    Require(NearlyEqual(material.normalScale, 0.75f), "Importer changed normal scale");
    Require(NearlyEqual(material.metallicFactor, 0.6f), "Importer changed metallic factor");
    Require(NearlyEqual(material.roughnessFactor, 0.4f), "Importer changed roughness factor");
    Require(NearlyEqual(material.occlusionStrength, 0.8f), "Importer changed AO strength");
    Require(NearlyEqual(material.emissiveFactor.r, 0.1f), "Importer changed emissive factor");
    Require(material.alphaMode == MaterialAlphaMode::Blend, "Importer lost alpha mode");
    Require(material.doubleSided, "Importer lost double-sided material state");
    Require(NearlyEqual(material.alphaCutoff, 0.35f), "Importer alpha cutoff changed");
    Require(NearlyEqual(material.baseColorFactor.r, 0.25f), "Importer base color changed");
    Require(NearlyEqual(material.baseColorFactor.a, 0.8f), "Importer base alpha changed");

    for (const ImportedTextureData& texture : imported.textures)
    {
        Require(
            texture.source.IsEncoded(),
            "Importer did not preserve embedded compressed texture bytes"
        );
        Require(
            !texture.source.data.empty(),
            "Importer produced an empty embedded texture"
        );
    }
    Require(
        imported.textures[0].source.colorSpace == TextureColorSpace::Srgb &&
        imported.textures[1].source.colorSpace == TextureColorSpace::Linear &&
        imported.textures[2].source.colorSpace == TextureColorSpace::Linear &&
        imported.textures[3].source.colorSpace == TextureColorSpace::Srgb,
        "Importer assigned an incorrect PBR texture color space"
    );
    Require(
        NearlyEqual(imported.parts[0].localTransform[3].x, 1.0f) &&
        NearlyEqual(imported.parts[0].localTransform[3].y, 2.0f) &&
        NearlyEqual(imported.parts[0].localTransform[3].z, 3.0f),
        "Importer changed first node transform"
    );
    Require(
        NearlyEqual(imported.parts[1].localTransform[3].x, -1.0f),
        "Importer changed second node transform"
    );
}

void TestImportedResourceCreation()
{
    ResourceManager resources;
    const std::filesystem::path modelPath =
        FixturePath("triangle-gltf");
    RequireCoreAsset("triangle-gltf");
    ModelAsset& model = resources.LoadModel(
        "embeddedTriangle",
        modelPath
    );

    Require(model.PartCount() == 2, "Loaded ModelAsset part count is incorrect");
    Require(resources.ModelCount() == 1, "Imported model was not registered");
    Require(resources.MeshCount() == 1, "Imported shared mesh was duplicated");
    Require(resources.MaterialCount() == 1, "Imported material count is incorrect");
    Require(
        resources.TextureCount() == 4,
        "Imported texture count is incorrect: " +
            std::to_string(resources.TextureCount())
    );
    Require(
        &model.Parts()[0].GetMesh() == &model.Parts()[1].GetMesh(),
        "Node instances do not share their Mesh resource"
    );
    Require(
        &model.Parts()[0].GetMaterial() == &model.Parts()[1].GetMaterial(),
        "Node instances do not share their Material resource"
    );
    Require(
        !resources.GetTexture("embeddedTriangle::texture::0").IsAttached(),
        "CPU import unexpectedly created an OpenGL texture"
    );
    const Material& importedMaterial =
        resources.GetMaterial("embeddedTriangle::material::0");
    Require(
        importedMaterial.HasTexture(importedMaterial.Interface().normalTexture),
        "ResourceManager lost imported normal texture binding"
    );
    Require(
        NearlyEqual(importedMaterial.NormalScale(), 0.75f),
        "ResourceManager changed imported normal scale"
    );
    Require(
        importedMaterial.HasTexture(
            importedMaterial.Interface().metallicRoughnessTexture
        ) &&
        importedMaterial.HasTexture(importedMaterial.Interface().emissiveTexture) &&
        importedMaterial.HasTexture(importedMaterial.Interface().occlusionTexture),
        "ResourceManager lost imported PBR texture bindings"
    );
    Require(
        NearlyEqual(importedMaterial.MetallicFactor(), 0.6f) &&
        NearlyEqual(importedMaterial.RoughnessFactor(), 0.4f) &&
        NearlyEqual(importedMaterial.OcclusionStrength(), 0.8f) &&
        NearlyEqual(importedMaterial.EmissiveFactor().g, 0.2f),
        "ResourceManager changed imported PBR factors"
    );

    const std::filesystem::path equivalentPath =
        modelPath.parent_path() / "." / modelPath.filename();
    ModelAsset& cachedModel = resources.LoadModel(
        "embeddedTriangle",
        equivalentPath
    );
    Require(&cachedModel == &model, "Equivalent model path was imported twice");
    Require(
        resources.FindModelByPath(equivalentPath) == &model,
        "FindModelByPath did not use normalized paths"
    );
    Require(resources.ModelCount() == 1, "Model path cache added a duplicate model");
    Require(resources.MeshCount() == 1, "Model path cache added duplicate meshes");
    Require(resources.MaterialCount() == 1, "Model path cache added duplicate materials");
    Require(resources.TextureCount() == 4, "Model path cache added duplicate textures");

    bool aliasRejected = false;
    try
    {
        resources.LoadModel(
            "triangleAlias",
            equivalentPath
        );
    }
    catch (const std::invalid_argument&)
    {
        aliasRejected = true;
    }
    Require(aliasRejected, "One model file was accepted under two resource names");

    bool nameCollisionRejected = false;
    try
    {
        resources.LoadModel(
            "embeddedTriangle",
            TestAssetDirectory / "models" / "Box.glb"
        );
    }
    catch (const std::invalid_argument&)
    {
        nameCollisionRejected = true;
    }
    Require(
        nameCollisionRejected,
        "One resource name was accepted for two different model files"
    );
    Require(resources.ModelCount() == 1, "Rejected model changed the model registry");
}

void TestImporterRejectsMissingFile()
{
    bool rejected = false;
    try
    {
        ModelImporter().Import(TestAssetDirectory / "models" / "missing.glb");
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Importer accepted a missing model file");
}

void TestImportResourceCollisionIsTransactional()
{
    RequireCoreAsset("box-glb");
    ResourceManager resources;
    resources.CreateTexture(
        "collision::texture::0",
        TextureData::FromEncoded({1})
    );

    bool rejected = false;
    try
    {
        resources.LoadModel(
            "collision",
            FixturePath("triangle-gltf")
        );
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    Require(rejected, "Importer accepted a generated resource-name collision");
    Require(resources.ModelCount() == 0, "Failed import left a model resource behind");
    Require(resources.MeshCount() == 0, "Failed import left mesh resources behind");
    Require(resources.MaterialCount() == 0, "Failed import left material resources behind");
    Require(resources.TextureCount() == 1, "Failed import changed existing textures");
}

void TestConvertedMmdGlbWhenAvailable()
{
    const std::filesystem::path modelPath =
        FixturePath("converted-mmd-glb");
    RequireCoreAsset("converted-mmd-glb");

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.meshes.size() == 21, "Converted MMD mesh primitive count changed");
    Require(imported.materials.size() == 21, "Converted MMD material count changed");
    Require(imported.textures.size() == 6, "Converted MMD embedded texture count changed");
    Require(imported.parts.size() == 21, "Converted MMD render-part count changed");

    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        Require(mesh.data.layout.size() == 5, "Converted MMD mesh layout is invalid");
        Require(!mesh.data.vertices.empty(), "Converted MMD mesh has no vertices");
        Require(!mesh.data.indices.empty(), "Converted MMD mesh has no indices");
        vertexCount += mesh.data.vertices.size() / 15;
        indexCount += mesh.data.indices.size();
    }
    Require(vertexCount >= 40000, "Converted MMD model lost too many vertices");
    Require(indexCount >= 100000, "Converted MMD model lost too many indices");

    std::size_t opaqueMaterialCount = 0;
    std::size_t blendMaterialCount = 0;
    for (const ImportedMaterialData& material : imported.materials)
    {
        opaqueMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Opaque ? 1U : 0U;
        blendMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Blend ? 1U : 0U;
    }
    Require(
        opaqueMaterialCount > 0 && blendMaterialCount > 0,
        "Converted MMD GLB alpha materials were not classified by texture content"
    );

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("yixuan", modelPath);
    Scene scene;
    Entity& instance = scene.InstantiateModel(model);

    Require(resources.ModelCount() == 1, "Converted MMD model was not registered");
    Require(resources.MeshCount() == 21, "Converted MMD mesh resources changed");
    Require(resources.MaterialCount() == 21, "Converted MMD material resources changed");
    Require(resources.TextureCount() == 6, "Converted MMD texture resources changed");
    Require(model.PartCount() == 21, "Converted MMD ModelAsset parts changed");
    Require(instance.RenderPartCount() == 21, "Converted MMD Entity parts changed");
}

void TestConvertedMmdObjWhenAvailable()
{
    const std::filesystem::path modelPath =
        FixturePath("converted-mmd-obj");
    RequireCoreAsset("converted-mmd-obj");

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.meshes.size() == 21, "Converted OBJ mesh count changed");
    Require(imported.materials.size() == 21, "Converted OBJ material count changed");
    Require(imported.textures.size() == 6, "Converted OBJ external texture count changed");
    Require(imported.parts.size() == 21, "Converted OBJ render-part count changed");

    std::size_t opaqueMaterialCount = 0;
    std::size_t blendMaterialCount = 0;
    for (const ImportedMaterialData& material : imported.materials)
    {
        opaqueMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Opaque ? 1U : 0U;
        blendMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Blend ? 1U : 0U;
    }
    Require(
        opaqueMaterialCount > 0 && blendMaterialCount > 0,
        "Converted OBJ alpha materials were not classified by texture content"
    );

    for (const ImportedTextureData& texture : imported.textures)
    {
        Require(texture.source.IsFile(), "OBJ texture was not kept as an external file");
        Require(
            std::filesystem::is_regular_file(texture.source.filePath),
            "OBJ external texture file was not resolved"
        );
        Require(
            texture.source.filePath.parent_path() == modelPath.parent_path(),
            "OBJ texture path was not resolved relative to its MTL"
        );
    }

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("yixuanObj", modelPath);
    Scene scene;
    Entity& instance = scene.InstantiateModel(model);

    Require(resources.ModelCount() == 1, "Converted OBJ model was not registered");
    Require(resources.MeshCount() == 21, "Converted OBJ mesh resources changed");
    Require(resources.MaterialCount() == 21, "Converted OBJ material resources changed");
    Require(resources.TextureCount() == 6, "Converted OBJ texture resources changed");
    Require(model.PartCount() == 21, "Converted OBJ ModelAsset parts changed");
    Require(instance.RenderPartCount() == 21, "Converted OBJ Entity parts changed");

    for (std::size_t index = 0; index < imported.textures.size(); ++index)
    {
        Texture& namedTexture = resources.GetTexture(
            "yixuanObj::texture::" + std::to_string(index)
        );
        Require(
            resources.FindTextureByPath(imported.textures[index].source.filePath) ==
                &namedTexture,
            "External texture path cache points to the wrong resource"
        );
    }

    const std::filesystem::path firstTexturePath =
        imported.textures[0].source.filePath;
    Texture& textureAlias = resources.CreateTexture(
        "yixuanObjTextureAlias",
        TextureData::FromFile(
            firstTexturePath.parent_path() / "." / firstTexturePath.filename()
        )
    );
    Require(
        &textureAlias == &resources.GetTexture("yixuanObj::texture::0"),
        "Equivalent external texture path created a second Texture object"
    );
    Require(
        resources.TextureCount() == 7,
        "Texture alias was not registered as a named resource"
    );

    Texture& linearTexture = resources.CreateTexture(
        "yixuanObjLinearTexture",
        TextureData::FromFile(firstTexturePath, TextureColorSpace::Linear)
    );
    Require(
        &linearTexture != &textureAlias,
        "Texture cache shared one GPU texture across different color spaces"
    );
    Require(
        resources.FindTextureByPath(
            firstTexturePath,
            TextureColorSpace::Linear
        ) == &linearTexture,
        "Linear texture path cache lookup failed"
    );
    Require(resources.TextureCount() == 8, "Linear texture alias was not registered");
}

void TestRiggedGlbImportWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("rigged-glb");
    RequireFullAsset("rigged-glb");

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.skeleton.has_value(), "Rigged GLB lost its Skeleton");
    const Skeleton& skeleton = *imported.skeleton;
    Require(skeleton.BoneCount() >= 400, "Rigged GLB lost too many bones");
    const Pose bindPose(skeleton);
    const std::span<const glm::mat4> glbSkinMatrices =
        bindPose.SkinningMatrices();
    for (std::size_t index = 0; index < glbSkinMatrices.size(); ++index)
    {
        Require(
            MatrixIdentityDeviation(glbSkinMatrices[index]) <= 0.002f,
            "Rigged GLB bind pose changed bone " +
                std::to_string(index) + " (" +
                skeleton.BoneAt(static_cast<BoneIndex>(index)).name +
                "), deviation=" +
                std::to_string(MatrixIdentityDeviation(glbSkinMatrices[index]))
        );
    }

    std::size_t skinnedMeshCount = 0;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        if (mesh.requiredBoneCount == 0)
            continue;
        ++skinnedMeshCount;
        Require(mesh.data.layout.size() == 7, "Rigged GLB skin layout is invalid");
        Require(
            mesh.data.layout[5].name == "boneIndices" &&
            mesh.data.layout[5].location == 7U &&
            mesh.data.layout[6].name == "boneWeights" &&
            mesh.data.layout[6].location == 8U,
            "Rigged GLB skin attributes use incorrect locations"
        );
        constexpr std::size_t Stride = 23U;
        Require(
            mesh.data.vertices.size() % Stride == 0,
            "Rigged GLB vertex data does not match its skin layout"
        );
        for (std::size_t offset = 15U;
             offset < mesh.data.vertices.size();
             offset += Stride)
        {
            float weightSum = 0.0f;
            for (std::size_t influence = 0; influence < 4U; ++influence)
            {
                const float boneIndex = mesh.data.vertices[offset + influence];
                const float weight = mesh.data.vertices[offset + 4U + influence];
                Require(
                    std::isfinite(boneIndex) && boneIndex >= 0.0f &&
                    boneIndex < static_cast<float>(skeleton.BoneCount()),
                    "Rigged GLB contains an invalid vertex bone index"
                );
                Require(
                    std::isfinite(weight) && weight >= 0.0f,
                    "Rigged GLB contains an invalid vertex bone weight"
                );
                weightSum += weight;
            }
            Require(
                NearlyEqual(weightSum, 1.0f),
                "Rigged GLB vertex bone weights are not normalized"
            );
        }
    }
    Require(skinnedMeshCount > 0, "Rigged GLB produced no skinned Mesh");

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("riggedYixuan", modelPath);
    Require(model.HasSkeleton(), "ResourceManager lost the imported Skeleton");
    Scene scene;
    Entity& instance = scene.InstantiateModel(model);
    Require(instance.HasPose(), "Model instance did not create a Pose");
    Require(
        &instance.GetPose().GetSkeleton() == &model.GetSkeleton(),
        "Entity Pose does not reference the ModelAsset Skeleton"
    );
}

void TestDemoPmxPhysicsImportWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    RequireFullAsset("production-pmx-yeshiguang");

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.mmdPhysics.has_value() && imported.skeleton.has_value(),
        "Demo PMX lost its Physics 1 metadata"
    );
    const MmdPhysicsAsset& physics = *imported.mmdPhysics;
    Require(
        physics.RigidBodyCount() == 495U && physics.JointCount() == 568U,
        "Demo PMX rigid-body or joint count changed"
    );

    std::array<std::size_t, 3U> shapeCounts{};
    std::array<std::size_t, 3U> modeCounts{};
    for (const MmdRigidBodyDefinition& body : physics.RigidBodies())
    {
        ++shapeCounts[static_cast<std::size_t>(body.shape)];
        ++modeCounts[static_cast<std::size_t>(body.mode)];
        Require(
            body.bone == InvalidBoneIndex ||
                static_cast<std::size_t>(body.bone) <
                    imported.skeleton->BoneCount(),
            "Demo PMX rigid body references an invalid runtime bone"
        );
    }
    Require(
        std::all_of(shapeCounts.begin(), shapeCounts.end(),
            [](std::size_t count) { return count > 0U; }) &&
        std::all_of(modeCounts.begin(), modeCounts.end(),
            [](std::size_t count) { return count > 0U; }),
        "Demo PMX did not exercise every rigid-body shape and mode"
    );
    Require(
        std::all_of(
            physics.Joints().begin(),
            physics.Joints().end(),
            [](const MmdJointDefinition& joint)
            {
                return joint.type == MmdJointType::Spring6Dof;
            }
        ),
        "Demo PMX joint types changed unexpectedly"
    );
}

void TestDirectPmxMaterialImportWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yixuan");
    RequireFullAsset("production-pmx-yixuan");

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.meshes.size() == 21, "Direct PMX mesh count changed");
    Require(imported.materials.size() == 21, "Direct PMX material count changed");
    Require(imported.parts.size() == 21, "Direct PMX part count changed");
    Require(imported.skeleton.has_value(), "Direct PMX lost its Skeleton");
    Require(!imported.morphs.empty(), "Direct PMX lost its morph definitions");
    std::size_t meshMorphTargetCount = 0U;
    for (const ImportedMeshData& mesh : imported.meshes)
        meshMorphTargetCount += mesh.morphTargets.size();
    Require(
        meshMorphTargetCount > 0U,
        "Direct PMX vertex morphs were not mapped to imported Mesh vertices"
    );
    std::size_t appendConstraintCount = 0U;
    std::size_t ikConstraintCount = 0U;
    for (const Bone& bone : imported.skeleton->Bones())
    {
        appendConstraintCount += bone.appendTransform.has_value() ? 1U : 0U;
        ikConstraintCount += bone.ikConstraint.has_value() ? 1U : 0U;
    }
    Require(
        imported.skeleton->HasMmdConstraints() &&
        appendConstraintCount > 0U &&
        ikConstraintCount > 0U,
        "Direct PMX lost its Append/Grant or IK constraints"
    );
    const Pose pmxBindPose(*imported.skeleton);
    for (const glm::mat4& skinMatrix : pmxBindPose.SkinningMatrices())
    {
        Require(
            NearlyEqual(skinMatrix, glm::mat4(1.0f)),
            "Direct PMX bind pose changed vertex positions"
        );
    }
    Require(
        imported.meshes[0].data.vertices.size() >= 18U &&
        NearlyEqual(imported.meshes[0].data.vertices[6], 0.56001925f) &&
        NearlyEqual(imported.meshes[0].data.vertices[7], 0.56904125f),
        "Direct PMX texture coordinates were vertically flipped"
    );

    std::size_t sphereMaterialCount = 0;
    std::size_t toonMaterialCount = 0;
    std::size_t edgeMaterialCount = 0;
    std::size_t transparentMaterialCount = 0;
    std::size_t maskedMaterialCount = 0;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        Require(
            mesh.data.layout.size() == 9,
            "PMX mesh is missing MMD or skinning attributes"
        );
        const std::size_t stride = 26U;
        Require(
            mesh.data.vertices.size() % stride == 0,
            "PMX vertex data does not match its UV layout"
        );
        for (std::size_t offset = 17;
             offset < mesh.data.vertices.size();
             offset += stride)
        {
            Require(
                std::isfinite(mesh.data.vertices[offset]) &&
                    mesh.data.vertices[offset] >= 0.0f,
                "PMX vertex edge scale is invalid"
            );
        }
        Require(mesh.requiredBoneCount > 0, "PMX mesh lost its bone weights");
        for (std::size_t offset = 22;
             offset < mesh.data.vertices.size();
             offset += stride)
        {
            const float weightSum =
                mesh.data.vertices[offset] +
                mesh.data.vertices[offset + 1U] +
                mesh.data.vertices[offset + 2U] +
                mesh.data.vertices[offset + 3U];
            Require(
                NearlyEqual(weightSum, 1.0f),
                "PMX vertex bone weights are not normalized"
            );
        }
    }
    for (const ImportedMaterialData& material : imported.materials)
    {
        Require(
            material.shadingModel == MaterialShadingModel::MmdToon,
            "PMX material did not select MMD Toon shading"
        );
        sphereMaterialCount += material.sphereTexture.has_value() ? 1U : 0U;
        toonMaterialCount += material.toonTexture.has_value() ? 1U : 0U;
        edgeMaterialCount += material.edgeEnabled ? 1U : 0U;
        transparentMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Blend ? 1U : 0U;
        maskedMaterialCount +=
            material.alphaMode == MaterialAlphaMode::Mask ? 1U : 0U;
    }
    Require(sphereMaterialCount > 0, "PMX sphere maps were not imported");
    Require(toonMaterialCount > 0, "PMX Toon ramps were not imported");
    Require(edgeMaterialCount > 0, "PMX edge flags were not imported");
    Require(transparentMaterialCount > 0, "PMX alpha materials were not imported");
    Require(maskedMaterialCount > 0, "PMX texture cutouts were not classified as Mask");

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("directPmx", modelPath);
    Require(model.PartCount() == 21, "Direct PMX ModelAsset part count changed");
    Require(model.HasSkeleton(), "ResourceManager lost the PMX Skeleton");
    Require(
        model.HasMorphs() &&
        model.GetMorphSet().MorphCount() == imported.morphs.size(),
        "ResourceManager lost the PMX MorphSet"
    );
    Scene morphScene;
    Entity& morphInstance = morphScene.InstantiateModel(model);
    Require(
        morphInstance.HasModelInstance(),
        "Scene did not create a ModelInstance for backend-driven PMX"
    );
    ModelInstance& modelInstance = morphInstance.GetModelInstance();
    IModelRuntimeDriver* runtime = modelInstance.TryGetRuntime();
    Require(
        runtime != nullptr && runtime->BackendName() == "saba-mmd",
        "Backend-driven PMX did not resolve through the Saba MMD runtime"
    );
    Require(
        modelInstance.InstanceMeshCount() > 0U,
        "Backend-driven PMX did not allocate instance-local meshes"
    );

    // Backend-driven morph control is exercised through the runtime, not an
    // Entity-side MorphState (which no longer exists for Saba-driven PMX).
    bool namedMorphAccepted = false;
    for (const MorphDefinition& morph : imported.morphs)
    {
        if (runtime->SetMorphWeight(morph.name, 0.5f))
        {
            namedMorphAccepted = true;
            break;
        }
    }
    Require(
        namedMorphAccepted,
        "Backend-driven PMX runtime did not accept named morph controls"
    );
    for (std::size_t index = 0; index < resources.MaterialCount(); ++index)
    {
        const Material& material = resources.GetMaterial(
            "directPmx::material::" + std::to_string(index)
        );
        Require(
            material.ShadingModel() == MaterialShadingModel::MmdToon,
            "ResourceManager changed PMX shading model"
        );
        Require(
            !material.Interface().imageBasedLightingEnabled,
            "MMD Toon material unexpectedly enabled PBR IBL uniforms"
        );
    }
}

void TestDirectPmxGroupMorphImportWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-aimisi");
    RequireFullAsset("production-pmx-aimisi");

    const ImportedModelData imported = ModelImporter().Import(modelPath);
    const auto group = std::find_if(
        imported.morphs.begin(),
        imported.morphs.end(),
        [](const MorphDefinition& morph)
        {
            return morph.kind == MorphKind::Group &&
                !morph.groupMembers.empty();
        }
    );
    Require(
        group != imported.morphs.end(),
        "PMX importer lost real Group Morph definitions"
    );

    const MorphSet morphSet(imported.morphs);
    const MorphIndex groupIndex = static_cast<MorphIndex>(
        std::distance(imported.morphs.begin(), group)
    );
    MorphState state(morphSet);
    state.SetWeight(groupIndex, 0.75f);
    const std::span<const float> effective = state.EffectiveWeights();
    Require(
        NearlyEqual(effective[groupIndex], 0.0f),
        "Group Morph incorrectly remained a vertex deformation weight"
    );

    bool drivesImportedVertexTarget = false;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        for (const MeshMorphTarget& target : mesh.morphTargets)
        {
            if (std::abs(effective[target.morphIndex]) > Epsilon)
            {
                drivesImportedVertexTarget = true;
                break;
            }
        }
        if (drivesImportedVertexTarget)
            break;
    }
    Require(
        drivesImportedVertexTarget,
        "Imported Group Morph did not resolve to a Mesh vertex target"
    );
}

void TestSabaIkSwitchBridgeWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-leimi");
    RequireFullAsset("production-pmx-leimi");

    SabaMmdRuntimeModel runtime(modelPath, {}, SabaPhysicsSettings{});
    Require(runtime.Initialize(), "Saba model failed to initialize");

    // The 蕾米埃尔 model drives leg IK from the エンジン/エンジンIK bone pair.
    const std::string ikBoneName(
        reinterpret_cast<const char*>(u8"エンジンIK")
    );
    const BoneIndex ikBone = runtime.FindBoneIndex(ikBoneName);
    Require(
        ikBone != InvalidBoneIndex,
        "Saba IK controller bone was not exposed in the engine bone space"
    );
    Require(
        runtime.FindBoneIndex("no_such_bone") == InvalidBoneIndex,
        "Unknown bone name must map to InvalidBoneIndex"
    );

    // Bridge must toggle the saba solver without throwing and survive a frame
    // where the VMD/IK evaluation would normally restore the VMD state.
    runtime.SetMmdIkEnabled(ikBone, false);
    runtime.Update(1.0f / 30.0f);
    runtime.SetMmdIkEnabled(ikBone, true);
    runtime.Update(1.0f / 30.0f);

    const SabaMmdRuntimeModel::VertexDiagnostics diagnostics =
        runtime.DiagnoseVertices();
    Require(
        diagnostics.finite && diagnostics.vertexCount > 0U,
        "Saba IK switch bridge produced non-finite vertices"
    );
}

void TestR1EngineOwnedMmdInstances()
{
    const std::filesystem::path modelPath =
        FixturePath("extended-morph-pmx");
    RequireCoreAsset("extended-morph-pmx");

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("r1::extended", modelPath);
    Require(
        model.BackendKind() == ModelBackendKind::SabaMmd,
        "PMX asset was not assigned to the Saba MMD backend"
    );

    Scene scene;
    Entity& first = scene.InstantiateModel(model);
    Entity& second = scene.InstantiateModel(model);
    Require(
        first.HasModelInstance() && second.HasModelInstance(),
        "Scene did not create WISTERIA-owned model instances"
    );

    ModelInstance& firstInstance = first.GetModelInstance();
    ModelInstance& secondInstance = second.GetModelInstance();
    IModelRuntimeDriver* firstRuntime = firstInstance.TryGetRuntime();
    IModelRuntimeDriver* secondRuntime = secondInstance.TryGetRuntime();
    Require(
        firstRuntime != nullptr && secondRuntime != nullptr &&
        firstRuntime != secondRuntime,
        "Two entities unexpectedly share one mutable runtime driver"
    );
    Require(
        firstRuntime->BackendName() == "saba-mmd" &&
        secondRuntime->BackendName() == "saba-mmd",
        "PMX instance did not resolve through the registered Saba backend"
    );
    // R1.1F: capability advertisement must reflect the real Saba surface.
    const ModelRuntimeCapabilities firstCapabilities =
        firstRuntime->Capabilities();
    const ModelPhysicsRuntimeInfo firstPhysicsInfo =
        firstRuntime->PhysicsInfo();
    Require(
        firstCapabilities.physics.supportsFixedTimeStep &&
        firstCapabilities.physics.supportsMaxSubSteps &&
        firstCapabilities.physics.supportsGravityOverride &&
        firstCapabilities.physics.supportsEnabledSwitch &&
        firstCapabilities.physics.supportsReset,
        "Saba backend did not advertise its real physics knobs"
    );
    Require(
        !firstCapabilities.physics.supportsSolverTuning &&
        !firstCapabilities.physics.supportsCcd &&
        firstCapabilities.physics.supportsSnapshotCapture &&
        firstCapabilities.physics.supportsSnapshotRestore &&
        firstCapabilities.physics.supportsCanonicalRestore,
        "Saba backend advertised unsupported physics capabilities"
    );
    Require(
        firstPhysicsInfo.available &&
        firstPhysicsInfo.ownsSimulationStep,
        "Saba physics availability misreported"
    );
    Require(
        NearlyEqual(firstPhysicsInfo.fixedTimeStep, 1.0f / 120.0f) &&
        firstPhysicsInfo.maxSubSteps > 0,
        "Saba physics settings not reflected in PhysicsInfo"
    );
    Require(
        firstInstance.InstanceMeshCount() > 0U &&
        firstInstance.InstanceMeshCount() == secondInstance.InstanceMeshCount(),
        "Dynamic PMX instances did not allocate instance-local meshes"
    );
    Require(
        first.RenderPartCount() == model.PartCount() &&
        second.RenderPartCount() == model.PartCount(),
        "Runtime-backed entities lost model render parts"
    );
    Require(
        &first.RenderParts()[0].GetMesh() != &model.Parts()[0].GetMesh() &&
        &second.RenderParts()[0].GetMesh() != &model.Parts()[0].GetMesh() &&
        &first.RenderParts()[0].GetMesh() != &second.RenderParts()[0].GetMesh(),
        "Mutable runtime geometry leaked back into a shared ModelAsset mesh"
    );
    Require(
        first.GetPose().BoneCount() > 0U &&
        second.GetPose().BoneCount() == first.GetPose().BoneCount(),
        "Saba runtime did not publish a stable PMX pose through WISTERIA"
    );

    Require(
        firstRuntime->SetMorphWeight("vertex", 1.0f) &&
        secondRuntime->SetMorphWeight("vertex", 0.0f),
        "Runtime backend did not accept independent named morph controls"
    );
    scene.Update(0.0f);
    const std::optional<float> firstWeight =
        firstRuntime->MorphWeight("vertex");
    const std::optional<float> secondWeight =
        secondRuntime->MorphWeight("vertex");
    Require(
        firstWeight.has_value() && secondWeight.has_value() &&
        NearlyEqual(*firstWeight, 1.0f) && NearlyEqual(*secondWeight, 0.0f),
        "One PMX instance overwrote another instance's morph state"
    );
    const ModelVertexFrame firstFrame = firstRuntime->VertexFrame();
    const ModelVertexFrame secondFrame = secondRuntime->VertexFrame();
    Require(
        !firstFrame.positions.empty() && !secondFrame.positions.empty() &&
        firstFrame.positions.data() != secondFrame.positions.data(),
        "Two PMX instances unexpectedly share one mutable vertex frame"
    );

    // R1.1D: WISTERIA-owned frame state. CaptureSnapshot must produce
    // independent per-instance snapshots and must not mutate the shared
    // ModelAsset or the runtime's transient view.
    const ModelFrameSnapshot& firstSnapshot =
        first.GetModelInstance().CaptureSnapshot(CaptureMask::All);
    const ModelFrameSnapshot& secondSnapshot =
        second.GetModelInstance().CaptureSnapshot(CaptureMask::All);
    Require(
        firstSnapshot.pose.captured && secondSnapshot.pose.captured &&
        firstSnapshot.geometry.captured && secondSnapshot.geometry.captured,
        "CaptureSnapshot did not capture requested channels"
    );
    Require(
        firstSnapshot.geometry.positions.size() > 0U &&
        firstSnapshot.geometry.positions.size() ==
            secondSnapshot.geometry.positions.size(),
        "Captured geometry is empty or inconsistent across instances"
    );
    Require(
        firstSnapshot.geometry.positions.data() !=
            secondSnapshot.geometry.positions.data(),
        "Two instances unexpectedly share one captured geometry buffer"
    );
    Require(
        firstSnapshot.pose.localTransforms.size() ==
            first.GetPose().BoneCount() &&
        secondSnapshot.pose.localTransforms.size() ==
            second.GetPose().BoneCount(),
        "PoseSnapshot bone matrix count mismatched the runtime pose"
    );
    Require(
        firstSnapshot.metadata.updateSerial >= 1U &&
            firstSnapshot.metadata.valid,
        "Frame metadata did not advance after update"
    );

    // R1.1 Fixup: snapshotRevision is a monotonic ModelInstance sequence and
    // must advance when any channel changes, even if another channel's
    // revision counter is numerically larger.
    const std::uint64_t initialStateRevision =
        firstSnapshot.metadata.snapshotRevision;
    firstRuntime->SetMorphWeight("vertex", 0.25f);
    scene.Update(0.0f);
    const ModelFrameSnapshot& refreshed =
        first.GetModelInstance().CaptureSnapshot(CaptureMask::All);
    Require(
        refreshed.metadata.snapshotRevision > initialStateRevision,
        "snapshotRevision did not advance after a morph change"
    );

    // Reset invalidates the transient view and the snapshot validity.
    first.GetModelInstance().Reset();
    Require(
        !first.GetModelInstance().LastSnapshot().metadata.valid,
        "Reset left the snapshot marked valid"
    );
    Require(
        first.GetModelInstance().LastFrameView().geometry.positions.empty(),
        "Reset left a stale transient view"
    );
    // R1.1 Final Fix: after Reset, CaptureSnapshot must NOT revalidate the
    // snapshot from stale updateSerial history.
    first.GetModelInstance().CaptureSnapshot(CaptureMask::All);
    Require(
        !first.GetModelInstance().LastSnapshot().metadata.valid,
        "CaptureSnapshot revalidated state before an Update after Reset"
    );
    // A subsequent Update restores validity.
    scene.Update(0.0f);
    first.GetModelInstance().CaptureSnapshot(CaptureMask::All);
    Require(
        first.GetModelInstance().LastSnapshot().metadata.valid,
        "CaptureSnapshot stayed invalid after a fresh Update"
    );

    Require(scene.RemoveEntity(first), "Failed to destroy the first instance");
    Require(scene.EntityCount() == 1U, "Scene retained a destroyed instance");
    scene.Update(0.0f);
    Require(
        second.GetModelInstance().TryGetRuntime() == secondRuntime &&
        secondRuntime->VertexFrame().revision >= secondFrame.revision,
        "Destroying one instance invalidated the surviving instance"
    );
}

// R1.2A helpers -----------------------------------------------------------

namespace
{
std::unique_ptr<SabaMmdRuntimeModel> CreateDeterministicRuntime(
    const std::filesystem::path& modelPath,
    const std::filesystem::path& vmdPath = {}
)
{
    SabaPhysicsSettings settings;
    settings.fixedTimeStep = 1.0f / 120.0f;
    settings.maxSubSteps = 10;
    settings.gravity = glm::vec3(0.0f, -98.0f, 0.0f);
    settings.enabled = true;
    auto runtime = std::make_unique<SabaMmdRuntimeModel>(
        modelPath,
        vmdPath,
        settings
    );
    Require(
        runtime->Initialize(),
        "R1.2A deterministic runtime failed to initialize"
    );
    return runtime;
}

FrameStateHashes CaptureDeterminismHashes(
    SabaMmdRuntimeModel& runtime
)
{
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        &runtime
    );
    Require(observation != nullptr, "R1.2A runtime has no observation");
    PhysicsSnapshot physics;
    Require(
        observation->CaptureState(physics) == TimelineStatus::Ok,
        "R1.2A physics capture failed"
    );

    ModelFrameSnapshot frame;
    const Pose& pose = runtime.GetPose();
    frame.pose.localTransforms.assign(
        pose.LocalMatrices().begin(),
        pose.LocalMatrices().end()
    );
    frame.pose.globalTransforms.assign(
        pose.GlobalMatrices().begin(),
        pose.GlobalMatrices().end()
    );
    frame.pose.skinningTransforms.assign(
        pose.SkinningMatrices().begin(),
        pose.SkinningMatrices().end()
    );
    const ModelVertexFrame vertexFrame = runtime.VertexFrame();
    frame.geometry.positions.assign(
        vertexFrame.positions.begin(),
        vertexFrame.positions.end()
    );
    frame.geometry.normals.assign(
        vertexFrame.normals.begin(),
        vertexFrame.normals.end()
    );
    const FrameStateHashes hashes = ComputeFrameStateHashes(frame, physics);
    Require(
        hashes.pose.valid && hashes.vertex.valid && hashes.physics.valid,
        "R1.2A capture produced an invalid determinism hash"
    );
    return hashes;
}

std::pair<std::size_t, std::size_t> CountBodyKinds(
    const PhysicsSnapshot& physics
)
{
    std::size_t dynamicCount = 0U;
    std::size_t kinematicCount = 0U;
    for (const RigidBodySnapshot& body : physics.rigidBodies)
    {
        if (body.mode == PmxRigidBodyMode::FollowBone)
            ++kinematicCount;
        else
            ++dynamicCount;
    }
    return {dynamicCount, kinematicCount};
}

bool BodyMoved(
    const RigidBodySnapshot& before,
    const RigidBodySnapshot& after
)
{
    const float positionDelta = glm::distance(
        before.worldTransform.position,
        after.worldTransform.position
    );
    float basisDelta = 0.0f;
    for (std::size_t index = 0U; index < 9U; ++index)
    {
        basisDelta = std::max(
            basisDelta,
            std::abs(
                before.worldTransform.rotationBasis[index] -
                after.worldTransform.rotationBasis[index]
            )
        );
    }
    return positionDelta > 1.0e-4f || basisDelta > 1.0e-4f;
}

// Builds a QDEF variant of the pmx_physics core fixture by replacing its
// vertex block. The fixture is a stable PMX 2.1 file with 3 BDEF1 vertices,
// 1-byte indices and (in this vendored Saba layout) a 4-byte edge flag.
// Variants let tests exercise the invalid-bone fallback without shipping
// separate broken assets.
std::filesystem::path BuildQdefPmxVariant(
    const std::array<std::array<int32_t, 4>, 3>& boneIndices,
    const std::array<std::array<float, 4>, 3>& boneWeights
)
{
    const std::filesystem::path source = FixturePath("pmx-physics");
    std::ifstream input(source, std::ios::binary);
    Require(input.is_open(), "Cannot open pmx_physics for QDEF variant");
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };

    // PMX header (17 bytes) + four length-prefixed strings.
    std::size_t offset = 17U;
    for (int stringIndex = 0; stringIndex < 4; ++stringIndex)
    {
        std::uint32_t length = 0U;
        std::memcpy(&length, bytes.data() + offset, sizeof(length));
        offset += sizeof(length) + length;
    }
    std::uint32_t vertexCount = 0U;
    std::memcpy(&vertexCount, bytes.data() + offset, sizeof(vertexCount));
    Require(
        vertexCount == 3U,
        "pmx_physics fixture vertex count changed; QDEF variant stale"
    );
    const std::size_t vertexStart = offset + sizeof(vertexCount);
    // BDEF1 vertex: position 12 + normal 12 + uv 8 + type 1 + bone index 1
    // + edge float 4 = 38 bytes in this fixture's Saba-compatible layout.
    constexpr std::size_t kOriginalVertexSize = 38U;
    const std::size_t vertexEnd =
        vertexStart + static_cast<std::size_t>(vertexCount) *
            kOriginalVertexSize;
    std::uint32_t faceTriple = 0U;
    std::memcpy(&faceTriple, bytes.data() + vertexEnd, sizeof(faceTriple));
    Require(
        faceTriple == 3U,
        "pmx_physics fixture face section shifted; QDEF variant stale"
    );

    const auto appendValue = [](std::vector<std::uint8_t>& output,
                                const void* data,
                                std::size_t size)
    {
        const std::size_t begin = output.size();
        output.resize(begin + size);
        std::memcpy(output.data() + begin, data, size);
    };

    std::vector<std::uint8_t> newVertices;
    newVertices.reserve(3U * 57U);
    for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
    {
        const std::size_t sourceBegin =
            vertexStart + vertex * kOriginalVertexSize;
        // position/normal/uv are unchanged (32 bytes).
        appendValue(
            newVertices,
            bytes.data() + sourceBegin,
            32U
        );
        const std::uint8_t weightType = 4U;  // QDEF
        appendValue(newVertices, &weightType, sizeof(weightType));
        for (int slot = 0; slot < 4; ++slot)
        {
            const std::uint8_t boneIndex = static_cast<std::uint8_t>(
                boneIndices[vertex][slot]
            );
            appendValue(newVertices, &boneIndex, sizeof(boneIndex));
        }
        for (int slot = 0; slot < 4; ++slot)
        {
            const float weight = boneWeights[vertex][slot];
            appendValue(newVertices, &weight, sizeof(weight));
        }
        const float edge = 1.0f;
        appendValue(newVertices, &edge, sizeof(edge));
    }

    std::vector<std::uint8_t> result;
    result.reserve(bytes.size() + newVertices.size() - 3U * kOriginalVertexSize);
    // Header + strings + vertex count only; the original vertex block is
    // replaced by the QDEF block below.
    result.insert(result.end(), bytes.begin(), bytes.begin() + vertexStart);
    result.insert(result.end(), newVertices.begin(), newVertices.end());
    result.insert(result.end(), bytes.begin() + vertexEnd, bytes.end());

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() /
        "wisteria_qdef_invalid_bone_variant.pmx";
    std::ofstream out(output, std::ios::binary);
    Require(out.is_open(), "Cannot write QDEF variant PMX");
    out.write(
        reinterpret_cast<const char*>(result.data()),
        static_cast<std::streamsize>(result.size())
    );
    return output;
}
}  // namespace

void TestR12AFixturePhysicsSanity()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        stepper != nullptr && observation != nullptr,
        "pmx_physics fixture did not expose deterministic interfaces"
    );
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "PrepareFrameZero failed on pmx_physics"
    );
    PhysicsSnapshot physics;
    Require(
        observation->CaptureState(physics) == TimelineStatus::Ok,
        "CaptureState failed on pmx_physics"
    );
    const auto [dynamicCount, kinematicCount] =
        CountBodyKinds(physics);
    Require(
        dynamicCount > 0U && kinematicCount > 0U &&
            physics.jointCount > 0U,
        "pmx_physics fixture lost dynamic/kinematic/joint content"
    );
}

void TestR12AReplayFromStartRepeatable()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto first = CreateDeterministicRuntime(modelPath);
    auto second = CreateDeterministicRuntime(modelPath);
    auto* firstStepper = dynamic_cast<IDeterministicFrameStepper*>(
        first.get()
    );
    auto* secondStepper = dynamic_cast<IDeterministicFrameStepper*>(
        second.get()
    );
    Require(
        firstStepper != nullptr && secondStepper != nullptr,
        "R1.2A runtimes lost the deterministic stepper"
    );
    const ReplayConfig config;
    Require(
        firstStepper->PrepareFrameZero(config) == TimelineStatus::Ok &&
            secondStepper->PrepareFrameZero(config) == TimelineStatus::Ok,
        "PrepareFrameZero failed on one replay runtime"
    );
    for (MotionFrameIndex frame = 1U; frame <= 120U; ++frame)
    {
        Require(
            firstStepper->StepMotionFrameExact(frame, config) ==
                    TimelineStatus::Ok &&
                secondStepper->StepMotionFrameExact(frame, config) ==
                    TimelineStatus::Ok,
            "StepMotionFrameExact failed during repeatability replay"
        );
    }
    const FrameStateHashes firstHashes = CaptureDeterminismHashes(*first);
    const FrameStateHashes secondHashes = CaptureDeterminismHashes(*second);
    Require(
        firstHashes.pose.exactHash == secondHashes.pose.exactHash &&
            firstHashes.vertex.exactHash == secondHashes.vertex.exactHash &&
            firstHashes.physics.exactHash == secondHashes.physics.exactHash,
        "Identical ReplayFromStart runs produced different exact hashes"
    );
}

void TestR12ASeekConsistency()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto direct = CreateDeterministicRuntime(modelPath);
    auto staged = CreateDeterministicRuntime(modelPath);
    auto* directRuntime = dynamic_cast<MmdRuntimeModel*>(direct.get());
    auto* stagedRuntime = dynamic_cast<MmdRuntimeModel*>(staged.get());
    Require(
        directRuntime != nullptr && stagedRuntime != nullptr,
        "R1.2A seek test lost the MMD runtime surface"
    );
    const ReplayConfig config;
    Require(
        directRuntime->EvaluateTick(
            300U,
            SeekPolicy::ReplayFromStart,
            config
        ) == TimelineStatus::Ok,
        "Direct 300-frame replay failed"
    );
    Require(
        stagedRuntime->EvaluateTick(
            150U,
            SeekPolicy::ReplayFromStart,
            config
        ) == TimelineStatus::Ok &&
            stagedRuntime->EvaluateTick(
                300U,
                SeekPolicy::ReplayFromStart,
                config
            ) == TimelineStatus::Ok,
        "Staged 150->300 replay failed"
    );
    const FrameStateHashes directHashes = CaptureDeterminismHashes(*direct);
    const FrameStateHashes stagedHashes = CaptureDeterminismHashes(*staged);
    Require(
        directHashes.pose.exactHash == stagedHashes.pose.exactHash &&
            directHashes.vertex.exactHash == stagedHashes.vertex.exactHash &&
            directHashes.physics.exactHash == stagedHashes.physics.exactHash,
        "Direct 300 != staged 150->300 (seek inconsistency)"
    );
}

void TestR12AResetAtTargetCanonical()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* mmdRuntime = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        mmdRuntime != nullptr && observation != nullptr,
        "ResetAtTarget test lost the runtime surface"
    );
    const ReplayConfig config;
    Require(
        mmdRuntime->EvaluateTick(300U, SeekPolicy::ResetAtTarget, config) ==
            TimelineStatus::Ok,
        "First ResetAtTarget failed"
    );
    const FrameStateHashes firstHashes =
        CaptureDeterminismHashes(*runtime);
    PhysicsSnapshot firstState;
    Require(
        observation->CaptureState(firstState) == TimelineStatus::Ok,
        "First ResetAtTarget capture failed"
    );
    PhysicsStepDiagnostics firstDiagnostics;
    Require(
        observation->ReadStepDiagnostics(firstDiagnostics) ==
            TimelineStatus::Ok,
        "First ResetAtTarget diagnostics failed"
    );

    Require(
        mmdRuntime->EvaluateTick(300U, SeekPolicy::ResetAtTarget, config) ==
            TimelineStatus::Ok,
        "Second ResetAtTarget failed"
    );
    const FrameStateHashes secondHashes =
        CaptureDeterminismHashes(*runtime);
    PhysicsSnapshot secondState;
    Require(
        observation->CaptureState(secondState) == TimelineStatus::Ok,
        "Second ResetAtTarget capture failed"
    );
    PhysicsStepDiagnostics secondDiagnostics;
    Require(
        observation->ReadStepDiagnostics(secondDiagnostics) ==
            TimelineStatus::Ok,
        "Second ResetAtTarget diagnostics failed"
    );

    Require(
        firstHashes.pose.exactHash == secondHashes.pose.exactHash &&
            firstHashes.vertex.exactHash == secondHashes.vertex.exactHash &&
            firstHashes.physics.exactHash == secondHashes.physics.exactHash,
        "ResetAtTarget(300) twice produced different exact hashes"
    );
    Require(
        secondState.rigidBodies.size() == firstState.rigidBodies.size(),
        "ResetAtTarget changed rigid-body count"
    );
    bool allVelocitiesZero = true;
    bool allForcesZero = true;
    for (const RigidBodySnapshot& body : secondState.rigidBodies)
    {
        const float speed = glm::length(body.linearVelocity) +
            glm::length(body.angularVelocity);
        const float force = glm::length(body.totalForce) +
            glm::length(body.totalTorque);
        if (speed > 1.0e-6f)
            allVelocitiesZero = false;
        if (force > 1.0e-6f)
            allForcesZero = false;
    }
    Require(
        allVelocitiesZero && allForcesZero,
        "ResetAtTarget left nonzero velocity or force"
    );
    Require(
        secondDiagnostics.executedSubsteps == 0U &&
            secondDiagnostics.remainingAccumulator == 0.0,
        "ResetAtTarget boundary is not canonical (substeps/accumulator)"
    );
    Require(
        secondState.canonical,
        "ResetAtTarget snapshot is not marked canonical"
    );
}

void TestR12AStepDiagnostics()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        stepper != nullptr && observation != nullptr,
        "Step diagnostics test lost the runtime surface"
    );
    const ReplayConfig config;
    Require(
        stepper->PrepareFrameZero(config) == TimelineStatus::Ok &&
            stepper->StepMotionFrameExact(1U, config) ==
                TimelineStatus::Ok,
        "Step diagnostics replay failed"
    );
    PhysicsStepDiagnostics diagnostics;
    Require(
        observation->ReadStepDiagnostics(diagnostics) ==
            TimelineStatus::Ok,
        "ReadStepDiagnostics failed"
    );
    Require(
        diagnostics.executedSubsteps == 4U,
        "30Hz frame did not execute exactly 4 physics substeps"
    );
    Require(
        diagnostics.remainingAccumulator == 0.0,
        "Canonical frame boundary left a nonzero accumulator"
    );
}

void TestR12ADynamicBodiesMove()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        stepper != nullptr && observation != nullptr,
        "Dynamic movement test lost the runtime surface"
    );
    const ReplayConfig config;
    Require(
        stepper->PrepareFrameZero(config) == TimelineStatus::Ok,
        "Dynamic movement PrepareFrameZero failed"
    );
    PhysicsSnapshot before;
    Require(
        observation->CaptureState(before) == TimelineStatus::Ok,
        "Dynamic movement baseline capture failed"
    );
    for (MotionFrameIndex frame = 1U; frame <= 60U; ++frame)
    {
        Require(
            stepper->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "Dynamic movement replay failed"
        );
    }
    PhysicsSnapshot after;
    Require(
        observation->CaptureState(after) == TimelineStatus::Ok,
        "Dynamic movement final capture failed"
    );
    bool anyDynamicBodyMoved = false;
    for (const RigidBodySnapshot& bodyBefore : before.rigidBodies)
    {
        if (bodyBefore.mode == PmxRigidBodyMode::FollowBone)
            continue;
        const RigidBodySnapshot* bodyAfter = nullptr;
        for (const RigidBodySnapshot& candidate : after.rigidBodies)
        {
            if (candidate.index == bodyBefore.index)
            {
                bodyAfter = &candidate;
                break;
            }
        }
        Require(
            bodyAfter != nullptr,
            "Dynamic movement capture lost a rigid body"
        );
        if (BodyMoved(bodyBefore, *bodyAfter))
            anyDynamicBodyMoved = true;
    }
    Require(
        anyDynamicBodyMoved,
        "No dynamic rigid body moved after 60 deterministic frames"
    );
}

void TestR12ARejectsUnsupportedProfiles()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* mmdRuntime = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    Require(mmdRuntime != nullptr, "Profile rejection test lost runtime");

    ReplayConfig wrongFps;
    wrongFps.motionFps = 60U;
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromStart,
            wrongFps
        ) == TimelineStatus::UnsupportedReplayProfile,
        "60Hz motion profile was accepted"
    );
    ReplayConfig wrongHz;
    wrongHz.physicsHz = 60U;
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromStart,
            wrongHz
        ) == TimelineStatus::UnsupportedReplayProfile,
        "60Hz physics profile was accepted"
    );
    ReplayConfig withWarmup;
    withWarmup.warmupFrames = 1U;
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromStart,
            withWarmup
        ) == TimelineStatus::UnsupportedReplayProfile,
        "warmupFrames profile was accepted"
    );
    ReplayConfig withLoop;
    withLoop.loopMotion = true;
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromStart,
            withLoop
        ) == TimelineStatus::UnsupportedReplayProfile,
        "loopMotion replay profile was accepted"
    );
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromCheckpoint,
            {}
        ) == TimelineStatus::InvalidCheckpoint,
        "ReplayFromCheckpoint did not report InvalidCheckpoint"
    );
}

void TestR12AOutOfRangeHoldsPoseAndStepsPhysics()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        stepper != nullptr && observation != nullptr,
        "Out-of-range test lost the runtime surface"
    );
    const ReplayConfig config;
    Require(
        stepper->PrepareFrameZero(config) == TimelineStatus::Ok,
        "Out-of-range PrepareFrameZero failed"
    );
    const FrameStateHashes zeroHashes = CaptureDeterminismHashes(*runtime);
    PhysicsSnapshot zeroState;
    Require(
        observation->CaptureState(zeroState) == TimelineStatus::Ok,
        "Out-of-range baseline capture failed"
    );
    // No VMD is loaded: motion end is frame 0, so frames 1..300 must hold
    // the initial pose while physics keeps stepping.
    for (MotionFrameIndex frame = 1U; frame <= 300U; ++frame)
    {
        Require(
            stepper->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "Out-of-range replay failed"
        );
    }
    const FrameStateHashes endHashes = CaptureDeterminismHashes(*runtime);
    PhysicsSnapshot endState;
    Require(
        observation->CaptureState(endState) == TimelineStatus::Ok,
        "Out-of-range final capture failed"
    );
    Require(
        zeroHashes.pose.exactHash == endHashes.pose.exactHash &&
            zeroHashes.vertex.exactHash == endHashes.vertex.exactHash,
        "No-VMD replay changed the held initial pose"
    );
    Require(
        zeroHashes.physics.exactHash != endHashes.physics.exactHash,
        "No-VMD replay did not advance physics past motion end"
    );
    Require(
        endState.motionFrame == 300U &&
            endState.physicsTick == 1200U,
        "Out-of-range snapshot recorded the wrong timeline position"
    );
}

void TestR12AIdentityPoseMatchesBind()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    Require(stepper != nullptr, "Identity test lost the stepper");
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "Identity test PrepareFrameZero failed"
    );
    const ModelVertexFrame frame = runtime->VertexFrame();
    const std::span<const glm::vec3> bind = runtime->BindPositions();
    Require(
        !frame.positions.empty() &&
            frame.positions.size() == bind.size() &&
            frame.positions.size() == frame.normals.size(),
        "Identity test fixture lost vertex content"
    );
    for (std::size_t index = 0U; index < frame.positions.size(); ++index)
    {
        const glm::vec3& position = frame.positions[index];
        Require(
            std::isfinite(position.x) &&
                std::isfinite(position.y) &&
                std::isfinite(position.z),
            "Identity pose produced a non-finite deformed vertex"
        );
        Require(
            glm::distance(position, bind[index]) <= 1.0e-3f,
            [&]() {
                char buffer[256];
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "Identity pose did not reproduce bind at v%zu: "
                    "bind=(%.6f,%.6f,%.6f) upd=(%.6f,%.6f,%.6f)",
                    index,
                    static_cast<double>(bind[index].x),
                    static_cast<double>(bind[index].y),
                    static_cast<double>(bind[index].z),
                    static_cast<double>(position.x),
                    static_cast<double>(position.y),
                    static_cast<double>(position.z)
                );
                return std::string(buffer);
            }()
        );
    }
}

void TestR12AFourSubstepProbeLongRun()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        stepper != nullptr && observation != nullptr,
        "Long-run probe lost the runtime surface"
    );
    const ReplayConfig config;
    Require(
        stepper->PrepareFrameZero(config) == TimelineStatus::Ok,
        "Long-run probe PrepareFrameZero failed"
    );
    for (MotionFrameIndex frame = 1U; frame <= 1000U; ++frame)
    {
        Require(
            stepper->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "Long-run probe frame failed"
        );
        PhysicsStepDiagnostics diagnostics;
        Require(
            observation->ReadStepDiagnostics(diagnostics) ==
                TimelineStatus::Ok,
            "Long-run probe diagnostics failed"
        );
        Require(
            diagnostics.executedSubsteps == 4U &&
                diagnostics.remainingAccumulator == 0.0,
            "Long-run probe violated the 4-substep/zero-accumulator boundary"
        );
    }
}

void TestR12AStepStateMachine()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    Require(stepper != nullptr, "State machine test lost the stepper");
    const ReplayConfig config;
    Require(
        stepper->StepMotionFrameExact(1U, config) ==
            TimelineStatus::InvalidState,
        "Step before PrepareFrameZero was accepted"
    );
    Require(
        stepper->PrepareFrameZero(config) == TimelineStatus::Ok,
        "State machine PrepareFrameZero failed"
    );
    Require(
        stepper->StepMotionFrameExact(2U, config) ==
            TimelineStatus::NonSequentialFrame,
        "Jump-ahead frame was accepted"
    );
    Require(
        stepper->StepMotionFrameExact(1U, config) == TimelineStatus::Ok,
        "Expected first frame was rejected"
    );
    Require(
        stepper->StepMotionFrameExact(1U, config) ==
            TimelineStatus::NonSequentialFrame,
        "Repeated frame was accepted"
    );
    Require(
        stepper->StepMotionFrameExact(3U, config) ==
            TimelineStatus::NonSequentialFrame,
        "Skipped frame was accepted"
    );
}

void TestR12ALivePhysicsConfigBinding()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* mmdRuntime = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    Require(mmdRuntime != nullptr, "Live config test lost the runtime");

    MmdPhysicsRuntimeSettings wrongSettings;
    wrongSettings.fixedTimeStep = 1.0f / 60.0f;
    wrongSettings.maxSubSteps = 2;
    wrongSettings.gravity = glm::vec3(0.0f, -98.0f, 0.0f);
    wrongSettings.enabled = true;
    mmdRuntime->SetMmdPhysicsSettings(wrongSettings);
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromStart,
            {}
        ) == TimelineStatus::UnsupportedReplayProfile,
        "Replay accepted a live 1/60 physics configuration"
    );

    MmdPhysicsRuntimeSettings goodSettings;
    goodSettings.fixedTimeStep = 1.0f / 120.0f;
    goodSettings.maxSubSteps = 10;
    goodSettings.gravity = glm::vec3(0.0f, -98.0f, 0.0f);
    goodSettings.enabled = true;
    mmdRuntime->SetMmdPhysicsSettings(goodSettings);
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromStart,
            {}
        ) == TimelineStatus::Ok,
        "Replay rejected a valid restored live physics configuration"
    );
}

void TestR12APhysicsDisabledRejected()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    SabaPhysicsSettings settings;
    settings.fixedTimeStep = 1.0f / 120.0f;
    settings.maxSubSteps = 10;
    settings.gravity = glm::vec3(0.0f, -98.0f, 0.0f);
    settings.enabled = false;
    auto runtime = std::make_unique<SabaMmdRuntimeModel>(
        modelPath,
        std::filesystem::path{},
        settings
    );
    Require(
        runtime->Initialize(),
        "Disabled-physics runtime failed to initialize"
    );
    auto* mmdRuntime = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    Require(mmdRuntime != nullptr, "Disabled-physics test lost the runtime");
    Require(
        mmdRuntime->EvaluateTick(
            10U,
            SeekPolicy::ReplayFromStart,
            {}
        ) == TimelineStatus::UnsupportedReplayProfile,
        "Deterministic replay ran with physics disabled"
    );
}

void TestR12ADifferentHistoryResetConverges()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto shortHistory = CreateDeterministicRuntime(modelPath);
    auto longHistory = CreateDeterministicRuntime(modelPath);
    auto* shortStepper = dynamic_cast<IDeterministicFrameStepper*>(
        shortHistory.get()
    );
    auto* longStepper = dynamic_cast<IDeterministicFrameStepper*>(
        longHistory.get()
    );
    auto* shortMmd = dynamic_cast<MmdRuntimeModel*>(shortHistory.get());
    auto* longMmd = dynamic_cast<MmdRuntimeModel*>(longHistory.get());
    Require(
        shortStepper != nullptr && longStepper != nullptr &&
            shortMmd != nullptr && longMmd != nullptr,
        "Reset convergence test lost the runtime surfaces"
    );
    const ReplayConfig config;
    Require(shortStepper->PrepareFrameZero(config) == TimelineStatus::Ok &&
            longStepper->PrepareFrameZero(config) == TimelineStatus::Ok,
        "Reset convergence PrepareFrameZero failed");
    for (MotionFrameIndex frame = 1U; frame <= 30U; ++frame)
    {
        Require(
            shortStepper->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "Short-history replay failed"
        );
    }
    for (MotionFrameIndex frame = 1U; frame <= 120U; ++frame)
    {
        Require(
            longStepper->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "Long-history replay failed"
        );
    }
    // Different histories, same canonical reset target: the reset must
    // erase the history difference.
    Require(
        shortMmd->EvaluateTick(120U, SeekPolicy::ResetAtTarget, config) ==
                TimelineStatus::Ok &&
            longMmd->EvaluateTick(120U, SeekPolicy::ResetAtTarget, config) ==
                TimelineStatus::Ok,
        "ResetAtTarget failed on divergent histories"
    );
    const FrameStateHashes resetShort = CaptureDeterminismHashes(*shortHistory);
    const FrameStateHashes resetLong = CaptureDeterminismHashes(*longHistory);
    Require(
        resetShort.pose.exactHash == resetLong.pose.exactHash &&
            resetShort.vertex.exactHash == resetLong.vertex.exactHash &&
            resetShort.physics.exactHash == resetLong.physics.exactHash,
        "Canonical reset did not converge divergent histories"
    );
    // And the first steps after a fresh PrepareFrameZero must also converge.
    Require(
        shortStepper->PrepareFrameZero(config) == TimelineStatus::Ok &&
            longStepper->PrepareFrameZero(config) == TimelineStatus::Ok,
        "Converged PrepareFrameZero failed"
    );
    for (MotionFrameIndex frame = 1U; frame <= 10U; ++frame)
    {
        Require(
            shortStepper->StepMotionFrameExact(frame, config) ==
                    TimelineStatus::Ok &&
                longStepper->StepMotionFrameExact(frame, config) ==
                    TimelineStatus::Ok,
            "Converged post-reset replay failed"
        );
    }
    const FrameStateHashes stepShort = CaptureDeterminismHashes(*shortHistory);
    const FrameStateHashes stepLong = CaptureDeterminismHashes(*longHistory);
    Require(
        stepShort.pose.exactHash == stepLong.pose.exactHash &&
            stepShort.vertex.exactHash == stepLong.vertex.exactHash &&
            stepShort.physics.exactHash == stepLong.physics.exactHash,
        "Post-reset first steps diverged between histories"
    );
}

void TestR12AMorphOverrideLifecycle()
{
    const std::filesystem::path modelPath =
        FixturePath("extended-morph-pmx");
    RequireCoreAsset("extended-morph-pmx");

    const std::filesystem::path vmdPath =
        std::filesystem::temp_directory_path() /
        "wisteria_morph_override_test.vmd";
    {
        std::vector<std::uint8_t> bytes;
        const auto appendValue = [&bytes]<typename T>(const T& value)
        {
            const std::size_t offset = bytes.size();
            bytes.resize(offset + sizeof(T));
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
        };
        const auto appendFixed = [&bytes](
            std::string_view value,
            std::size_t size
        )
        {
            const std::size_t begin = bytes.size();
            bytes.resize(begin + size, 0U);
            const std::size_t copySize = std::min(value.size(), size);
            std::memcpy(bytes.data() + begin, value.data(), copySize);
        };
        appendFixed("Vocaloid Motion Data 0002", 30U);
        appendFixed("testModel", 20U);
        appendValue(std::uint32_t{0U});  // bone frames
        appendValue(std::uint32_t{2U});  // morph frames
        const auto appendMorphFrame = [&appendFixed, &appendValue](
            std::string_view name,
            std::uint32_t frame,
            float weight
        )
        {
            appendFixed(name, 15U);
            appendValue(frame);
            appendValue(weight);
        };
        appendMorphFrame("vertex", 0U, 0.0f);
        appendMorphFrame("vertex", 10U, 1.0f);
        appendValue(std::uint32_t{0U});  // camera frames
        appendValue(std::uint32_t{0U});  // light frames
        std::ofstream out(vmdPath, std::ios::binary);
        Require(out.is_open(), "Cannot write morph override VMD fixture");
        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }

    SabaMmdRuntimeModel runtime(modelPath);
    Require(runtime.Initialize(), "Morph override runtime failed to init");
    Require(runtime.LoadMotion(vmdPath), "Morph override VMD failed to load");
    runtime.SetMotionFrame(0.0);

    // SetMorphWeight stays instantaneous: the next VMD evaluation wins.
    Require(
        runtime.SetMorphWeight("vertex", 1.0f),
        "SetMorphWeight rejected a valid morph"
    );
    runtime.Update(0.0f);
    const std::optional<float> afterInstant = runtime.MorphWeight("vertex");
    Require(
        afterInstant.has_value() && NearlyEqual(*afterInstant, 0.0f),
        "SetMorphWeight unexpectedly created a persistent override"
    );

    // SetMorphOverride persists across VMD evaluations.
    Require(
        runtime.SetMorphOverride("vertex", 1.0f),
        "SetMorphOverride rejected a valid morph"
    );
    runtime.Update(0.0f);
    const std::optional<float> withOverride = runtime.MorphWeight("vertex");
    Require(
        withOverride.has_value() && NearlyEqual(*withOverride, 1.0f),
        "Morph override did not survive VMD evaluation"
    );

    // ClearMorphOverride restores VMD-driven weights.
    runtime.ClearMorphOverride("vertex");
    runtime.Update(0.0f);
    const std::optional<float> afterClear = runtime.MorphWeight("vertex");
    Require(
        afterClear.has_value() && NearlyEqual(*afterClear, 0.0f),
        "ClearMorphOverride did not restore VMD-driven weights"
    );

    // ClearAllMorphOverrides clears every entry.
    Require(
        runtime.SetMorphOverride("vertex", 0.5f),
        "Second SetMorphOverride rejected"
    );
    runtime.ClearAllMorphOverrides();
    runtime.Update(0.0f);
    const std::optional<float> afterClearAll = runtime.MorphWeight("vertex");
    Require(
        afterClearAll.has_value() && NearlyEqual(*afterClearAll, 0.0f),
        "ClearAllMorphOverrides did not restore VMD-driven weights"
    );

    std::error_code ignored;
    std::filesystem::remove(vmdPath, ignored);
}

void TestR12AQdefInvalidBoneFallback()
{
    RequireCoreAsset("pmx-physics");
    const auto runVariant =
        [](const std::array<std::array<int32_t, 4>, 3>& bones,
           const std::array<std::array<float, 4>, 3>& weights,
           bool expectBindFallback)
    {
        const std::filesystem::path variant =
            BuildQdefPmxVariant(bones, weights);
        SabaPhysicsSettings settings;
        settings.fixedTimeStep = 1.0f / 120.0f;
        settings.maxSubSteps = 10;
        settings.gravity = glm::vec3(0.0f, -98.0f, 0.0f);
        settings.enabled = true;
        SabaMmdRuntimeModel runtime(
            variant,
            std::filesystem::path{},
            settings
        );
        Require(
            runtime.Initialize(),
            "QDEF invalid-bone variant failed to initialize"
        );
        auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
            &runtime
        );
        Require(stepper != nullptr, "QDEF variant lost the stepper");
        Require(
            stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
            "QDEF variant PrepareFrameZero failed"
        );
        const ModelVertexFrame frame = runtime.VertexFrame();
        const std::span<const glm::vec3> bind = runtime.BindPositions();
        Require(
            !frame.positions.empty() &&
                frame.positions.size() == bind.size() &&
                frame.normals.size() == frame.positions.size(),
            "QDEF variant lost vertex content"
        );
        for (std::size_t index = 0U; index < frame.positions.size(); ++index)
        {
            const glm::vec3& position = frame.positions[index];
            const glm::vec3& normal = frame.normals[index];
            Require(
                std::isfinite(position.x) &&
                    std::isfinite(position.y) &&
                    std::isfinite(position.z),
                "QDEF invalid-bone fallback produced a non-finite vertex"
            );
            Require(
                std::isfinite(normal.x) &&
                    std::isfinite(normal.y) &&
                    std::isfinite(normal.z),
                "QDEF invalid-bone fallback produced a non-finite normal"
            );
            if (expectBindFallback)
            {
                Require(
                    glm::distance(position, bind[index]) <= 1.0e-3f,
                    "All-invalid QDEF did not fall back to the bind pose"
                );
            }
        }
        std::error_code ignored;
        std::filesystem::remove(variant, ignored);
    };

    // Case 1: slot 0 references an out-of-range bone, slot 1 is valid.
    runVariant(
        {{
            {{99, 0, 0, 0}},
            {{0, 0, 0, 0}},
            {{0, 0, 0, 0}}
        }},
        {{
            {{0.5f, 0.5f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, 0.0f, 0.0f}}
        }},
        false
    );
    // Case 2: an invalid slot carries a nonzero raw weight.
    runVariant(
        {{
            {{0, 99, 0, 0}},
            {{0, 0, 0, 0}},
            {{0, 0, 0, 0}}
        }},
        {{
            {{0.4f, 0.2f, 0.4f, 0.0f}},
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, 0.0f, 0.0f}}
        }},
        false
    );
    // Case 3: all four slots invalid -> identity fallback (bind pose).
    runVariant(
        {{
            {{99, 99, 99, 99}},
            {{99, 99, 99, 99}},
            {{99, 99, 99, 99}}
        }},
        {{
            {{0.25f, 0.25f, 0.25f, 0.25f}},
            {{0.25f, 0.25f, 0.25f, 0.25f}},
            {{0.25f, 0.25f, 0.25f, 0.25f}}
        }},
        true
    );
}

// Parser-level regression: the four QDEF bone weights must land in their
// exact slots. A skinning-output-only test cannot detect a parser that
// misplaces weights[2]/[3] (the old vendored code wrote to [3] and out of
// bounds at [4]).
void TestPmxQdefParserWeights()
{
    RequireCoreAsset("pmx-physics");
    const std::array<std::array<int32_t, 4>, 3> bones{{
        {{0, 1, 2, 3}},
        {{0, 1, 2, 3}},
        {{0, 1, 2, 3}}
    }};
    const std::array<std::array<float, 4>, 3> weights{{
        {{0.1f, 0.2f, 0.3f, 0.4f}},
        {{0.4f, 0.3f, 0.2f, 0.1f}},
        {{0.25f, 0.25f, 0.25f, 0.25f}}
    }};
    const std::filesystem::path variant =
        BuildQdefPmxVariant(bones, weights);
    saba::PMXFile parsed;
    Require(
        saba::ReadPMXFile(&parsed, variant.string().c_str()),
        "QDEF parser failed to read the generated variant"
    );
    Require(
        parsed.m_vertices.size() == 3U,
        "QDEF parser changed the variant vertex count"
    );
    for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            Require(
                parsed.m_vertices[vertex].m_boneIndices[slot] ==
                    bones[vertex][slot],
                "QDEF parser misplaced a bone index"
            );
            Require(
                NearlyEqual(
                    parsed.m_vertices[vertex].m_boneWeights[slot],
                    weights[vertex][slot]
                ),
                "QDEF parser misplaced a bone weight"
            );
        }
    }
    std::error_code ignored;
    std::filesystem::remove(variant, ignored);
}

// R1.2B helpers ------------------------------------------------------------

namespace
{
std::string TimelineStatusName(TimelineStatus status)
{
    switch (status)
    {
    case TimelineStatus::Ok:
        return "Ok";
    case TimelineStatus::NoPhysics:
        return "NoPhysics";
    case TimelineStatus::InvalidCheckpoint:
        return "InvalidCheckpoint";
    case TimelineStatus::UnsupportedReplayProfile:
        return "UnsupportedReplayProfile";
    case TimelineStatus::InvalidState:
        return "InvalidState";
    case TimelineStatus::NonSequentialFrame:
        return "NonSequentialFrame";
    case TimelineStatus::DeterminismViolation:
        return "DeterminismViolation";
    case TimelineStatus::SnapshotMismatch:
        return "SnapshotMismatch";
    case TimelineStatus::InvalidSnapshot:
        return "InvalidSnapshot";
    case TimelineStatus::Poisoned:
        return "Poisoned";
    }
    return "Unknown(" + std::to_string(static_cast<int>(status)) + ")";
}

bool SameTransformSnapshot(
    const RigidTransformSnapshot& left,
    const RigidTransformSnapshot& right
)
{
    if (left.position != right.position)
        return false;
    for (std::size_t index = 0U; index < 9U; ++index)
    {
        if (left.rotationBasis[index] != right.rotationBasis[index])
            return false;
    }
    return true;
}

bool SnapshotsEqualExceptFollowBoneActivation(
    const PhysicsSnapshot& left,
    const PhysicsSnapshot& right
)
{
    if (left.rigidBodies.size() != right.rigidBodies.size() ||
        left.jointCount != right.jointCount ||
        left.motionFrame != right.motionFrame ||
        left.physicsTick != right.physicsTick ||
        left.schemaVersion != right.schemaVersion ||
        left.layoutFingerprint != right.layoutFingerprint ||
        left.physicsConfigurationFingerprint !=
            right.physicsConfigurationFingerprint ||
        left.canonical != right.canonical)
    {
        return false;
    }
    for (std::size_t index = 0U; index < left.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& a = left.rigidBodies[index];
        const RigidBodySnapshot& b = right.rigidBodies[index];
        if (a.index != b.index || a.mode != b.mode ||
            a.definitionMass != b.definitionMass)
        {
            std::printf(
                "[R12B DIFF] body %zu identity\n", index
            );
            return false;
        }
        if (!SameTransformSnapshot(a.worldTransform, b.worldTransform))
        {
            std::printf(
                "[R12B DIFF] body %zu world\n  a pos=(%g,%g,%g) b pos=(%g,%g,%g)\n",
                index,
                static_cast<double>(a.worldTransform.position.x),
                static_cast<double>(a.worldTransform.position.y),
                static_cast<double>(a.worldTransform.position.z),
                static_cast<double>(b.worldTransform.position.x),
                static_cast<double>(b.worldTransform.position.y),
                static_cast<double>(b.worldTransform.position.z)
            );
            std::printf("  basis a=[");
            for (float component : a.worldTransform.rotationBasis)
                std::printf("%g ", static_cast<double>(component));
            std::printf("]\n  basis b=[");
            for (float component : b.worldTransform.rotationBasis)
                std::printf("%g ", static_cast<double>(component));
            std::printf("]\n");
            return false;
        }
        if (!SameTransformSnapshot(
                a.interpolationTransform,
                b.interpolationTransform
            ))
        {
            std::printf(
                "[R12B DIFF] body %zu interpolation\n  a pos=(%g,%g,%g) b pos=(%g,%g,%g)\n",
                index,
                static_cast<double>(a.interpolationTransform.position.x),
                static_cast<double>(a.interpolationTransform.position.y),
                static_cast<double>(a.interpolationTransform.position.z),
                static_cast<double>(b.interpolationTransform.position.x),
                static_cast<double>(b.interpolationTransform.position.y),
                static_cast<double>(b.interpolationTransform.position.z)
            );
            std::printf("  basis a=[");
            for (float component : a.interpolationTransform.rotationBasis)
                std::printf("%g ", static_cast<double>(component));
            std::printf("]\n  basis b=[");
            for (float component : b.interpolationTransform.rotationBasis)
                std::printf("%g ", static_cast<double>(component));
            std::printf("]\n");
            return false;
        }
        if (a.linearVelocity != b.linearVelocity ||
            a.angularVelocity != b.angularVelocity ||
            a.interpolationLinearVelocity != b.interpolationLinearVelocity ||
            a.interpolationAngularVelocity != b.interpolationAngularVelocity)
        {
            std::printf("[R12B DIFF] body %zu velocity\n", index);
            return false;
        }
        if (a.totalForce != b.totalForce ||
            a.totalTorque != b.totalTorque)
        {
            std::printf("[R12B DIFF] body %zu force\n", index);
            return false;
        }
        if (a.mode != PmxRigidBodyMode::FollowBone)
        {
            if (a.activationState != b.activationState ||
                a.deactivationTime != b.deactivationTime)
            {
                std::printf("[R12B DIFF] body %zu activation\n", index);
                return false;
            }
        }
    }
    return true;
}

PhysicsSnapshot CaptureCanonicalAt(
    SabaMmdRuntimeModel& runtime,
    MotionFrameIndex frame
)
{
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(&runtime);
    Require(stepper != nullptr, "R1.2B helper lost the stepper");
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "R1.2B helper PrepareFrameZero failed"
    );
    for (MotionFrameIndex current = 1U; current <= frame; ++current)
    {
        Require(
            stepper->StepMotionFrameExact(current, {}) ==
                TimelineStatus::Ok,
            "R1.2B helper replay failed"
        );
    }
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        &runtime
    );
    Require(observation != nullptr, "R1.2B helper lost the observation");
    PhysicsSnapshot snapshot;
    Require(
        observation->CaptureState(snapshot) == TimelineStatus::Ok,
        "R1.2B helper capture failed"
    );
    Require(
        snapshot.canonical,
        "R1.2B helper produced a non-canonical snapshot"
    );
    return snapshot;
}

void RequireRestoreAnimationFrame(
    SabaMmdRuntimeModel& runtime,
    double frame
)
{
    // Restore requires the animation evaluation frame to equal the snapshot
    // frame. SetMotionFrame moves the tag without evaluating physics.
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(&runtime);
    Require(mmd != nullptr, "R1.2B helper lost the MMD runtime");
    mmd->SetMotionFrame(frame);
}
}  // namespace

void TestR12BRestoreRoundTrip()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);

    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        mmd != nullptr && restore != nullptr && observation != nullptr,
        "R1.2B round-trip lost a runtime surface"
    );
    Require(
        mmd->EvaluateTick(30U, SeekPolicy::ReplayFromStart, {}) ==
            TimelineStatus::Ok,
        "R1.2B round-trip perturbation replay failed"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    const TimelineStatus restoreStatus = restore->RestoreState(snapshot);
    Require(
        restoreStatus == TimelineStatus::Ok,
        "R1.2B RestoreState failed: " + TimelineStatusName(restoreStatus)
    );
    PhysicsSnapshot after;
    Require(
        observation->CaptureState(after) == TimelineStatus::Ok,
        "R1.2B round-trip capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(snapshot, after),
        "R1.2B Capture -> perturb -> Restore -> Capture diverged"
    );
    Require(
        after.canonical,
        "R1.2B restore did not produce a canonical boundary"
    );
}

void TestR12BRestoreRotationBasisRoundTrip()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 300U);
    // The round-trip must exercise a significant rotation (not just the
    // near-identity frame-0 pose). Require at least ~29 degrees of rotation
    // on some body (sin(theta/2) > 0.25).
    float maxBasisDeviation = 0.0f;
    for (const RigidBodySnapshot& body : snapshot.rigidBodies)
    {
        for (std::size_t component = 0U; component < 9U; ++component)
        {
            const float identity = (component % 4U == 0U) ? 1.0f : 0.0f;
            maxBasisDeviation = std::max(
                maxBasisDeviation,
                std::abs(
                    body.worldTransform.rotationBasis[component] - identity
                )
            );
        }
    }
    Require(
        maxBasisDeviation > 0.5f,
        "R1.2B rotation fixture did not produce a significant rotation"
    );
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        mmd != nullptr && restore != nullptr && observation != nullptr,
        "R1.2B rotation round-trip lost a runtime surface"
    );
    Require(
        mmd->EvaluateTick(340U, SeekPolicy::ReplayFromStart, {}) ==
            TimelineStatus::Ok,
        "R1.2B rotation perturbation failed"
    );
    RequireRestoreAnimationFrame(*runtime, 300.0);
    const TimelineStatus restoreStatus = restore->RestoreState(snapshot);
    Require(
        restoreStatus == TimelineStatus::Ok,
        "R1.2B rotation RestoreState failed: " +
            TimelineStatusName(restoreStatus)
    );
    PhysicsSnapshot after;
    Require(
        observation->CaptureState(after) == TimelineStatus::Ok,
        "R1.2B rotation capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(snapshot, after),
        "R1.2B rotation basis did not round-trip bit-exactly"
    );
}

void TestR12BRestoreIdempotent()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        restore != nullptr && observation != nullptr,
        "R1.2B idempotent test lost a runtime surface"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    const TimelineStatus restoreStatus = restore->RestoreState(snapshot);
    Require(
        restoreStatus == TimelineStatus::Ok,
        "R1.2B first restore failed: " + TimelineStatusName(restoreStatus)
    );
    PhysicsSnapshot first;
    Require(
        observation->CaptureState(first) == TimelineStatus::Ok,
        "R1.2B first capture failed"
    );
    Require(
        restore->RestoreState(snapshot) == TimelineStatus::Ok,
        "R1.2B second restore failed"
    );
    PhysicsSnapshot second;
    Require(
        observation->CaptureState(second) == TimelineStatus::Ok,
        "R1.2B second capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(first, second),
        "R1.2B restore was not idempotent"
    );
}

void TestR12BRestoreDiagnosticsAndZeroStep()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        restore != nullptr && observation != nullptr,
        "R1.2B diagnostics test lost a runtime surface"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    Require(
        restore->RestoreState(snapshot) == TimelineStatus::Ok,
        "R1.2B diagnostics restore failed"
    );
    PhysicsStepDiagnostics diagnostics;
    Require(
        observation->ReadStepDiagnostics(diagnostics) ==
            TimelineStatus::Ok,
        "R1.2B diagnostics read failed"
    );
    Require(
        diagnostics.executedSubsteps == 0U &&
            diagnostics.remainingAccumulator == 0.0 &&
            !diagnostics.poisoned,
        "R1.2B restore boundary diagnostics are wrong"
    );
    PhysicsSnapshot immediate;
    PhysicsSnapshot zeroStep;
    Require(
        observation->CaptureState(immediate) == TimelineStatus::Ok &&
            observation->CaptureState(zeroStep) == TimelineStatus::Ok,
        "R1.2B zero-step capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(immediate, zeroStep),
        "R1.2B zero-step capture diverged"
    );
}

void TestR12BDivergentHistoryOneStep()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto shortHistory = CreateDeterministicRuntime(modelPath);
    auto longHistory = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot =
        CaptureCanonicalAt(*shortHistory, 0U);
    auto* shortMmd = dynamic_cast<MmdRuntimeModel*>(shortHistory.get());
    auto* longMmd = dynamic_cast<MmdRuntimeModel*>(longHistory.get());
    auto* shortRestore = dynamic_cast<IPhysicsStateAccess*>(
        shortHistory.get()
    );
    auto* longRestore = dynamic_cast<IPhysicsStateAccess*>(
        longHistory.get()
    );
    auto* shortObservation =
        dynamic_cast<IDeterministicPhysicsObservation*>(shortHistory.get());
    auto* longObservation =
        dynamic_cast<IDeterministicPhysicsObservation*>(longHistory.get());
    Require(
        shortMmd != nullptr && longMmd != nullptr &&
            shortRestore != nullptr && longRestore != nullptr &&
            shortObservation != nullptr && longObservation != nullptr,
        "R1.2B divergent test lost a runtime surface"
    );
    Require(
        shortMmd->EvaluateTick(30U, SeekPolicy::ReplayFromStart, {}) ==
                TimelineStatus::Ok &&
            longMmd->EvaluateTick(90U, SeekPolicy::ReplayFromStart, {}) ==
                TimelineStatus::Ok,
        "R1.2B divergent histories failed"
    );
    RequireRestoreAnimationFrame(*shortHistory, 0.0);
    RequireRestoreAnimationFrame(*longHistory, 0.0);
    Require(
        shortRestore->RestoreState(snapshot) == TimelineStatus::Ok &&
            longRestore->RestoreState(snapshot) == TimelineStatus::Ok,
        "R1.2B divergent restore failed"
    );
#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)
    Require(
        shortHistory->StepRestoredPhysicsForProbe(4U) ==
                TimelineStatus::Ok &&
            longHistory->StepRestoredPhysicsForProbe(4U) ==
                TimelineStatus::Ok,
        "R1.2B divergent one-step probe failed"
    );
    PhysicsSnapshot shortState;
    PhysicsSnapshot longState;
    Require(
        shortObservation->CaptureState(shortState) ==
                TimelineStatus::Ok &&
            longObservation->CaptureState(longState) ==
                TimelineStatus::Ok,
        "R1.2B divergent one-step capture failed"
    );
    const DeterminismHashes shortHash = HashPhysics(shortState);
    const DeterminismHashes longHash = HashPhysics(longState);
    Require(
        shortHash.valid && longHash.valid &&
            shortHash.exactHash == longHash.exactHash,
        "Divergent collision/solver history leaked into the first step"
    );
#else
    Require(false, "R1.2B test hooks are not compiled in");
#endif
}

void TestR12BInvalidSnapshotRejections()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot valid = CaptureCanonicalAt(*runtime, 0U);
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        restore != nullptr && observation != nullptr,
        "R1.2B rejection test lost a runtime surface"
    );
    const auto runRejected = [&](const PhysicsSnapshot& candidate,
                                 TimelineStatus expected)
    {
        // SetMotionFrame is a non-deterministic mutator (R1.3 Final
        // Validation 2) and invalidates the canonical flag; establish the
        // restore precondition before capturing the baseline so the
        // before/after comparison only measures world state.
        RequireRestoreAnimationFrame(*runtime, 0.0);
        PhysicsSnapshot before;
        Require(
            observation->CaptureState(before) == TimelineStatus::Ok,
            "R1.2B rejection baseline capture failed"
        );
        const TimelineStatus status = restore->RestoreState(candidate);
        Require(
            status == expected,
            "R1.2B rejection returned the wrong status"
        );
        PhysicsSnapshot after;
        Require(
            observation->CaptureState(after) == TimelineStatus::Ok,
            "R1.2B rejection post capture failed"
        );
        Require(
            SnapshotsEqualExceptFollowBoneActivation(before, after),
            "R1.2B rejected restore modified the world"
        );
    };

    PhysicsSnapshot nonFiniteForce = valid;
    nonFiniteForce.rigidBodies[0].totalForce.x =
        std::numeric_limits<float>::quiet_NaN();
    runRejected(nonFiniteForce, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot nanBasis = valid;
    nanBasis.rigidBodies[0].worldTransform.rotationBasis[0] =
        std::numeric_limits<float>::infinity();
    runRejected(nanBasis, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot nonFinitePosition = valid;
    nonFinitePosition.rigidBodies[0].worldTransform.position.x =
        std::numeric_limits<float>::quiet_NaN();
    runRejected(nonFinitePosition, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot columnLengthError = valid;
    columnLengthError.rigidBodies[0].worldTransform.rotationBasis[0] = 2.0f;
    runRejected(columnLengthError, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot nonOrthogonalBasis = valid;
    // c0 and c1 are both unit-ish but not orthogonal (dot = 0.2).
    nonOrthogonalBasis.rigidBodies[0].worldTransform.rotationBasis = {
        1.0f, 0.0f, 0.0f,
        0.2f, 0.98f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    runRejected(nonOrthogonalBasis, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot interpolationBasisError = valid;
    interpolationBasisError.rigidBodies[0]
        .interpolationTransform.rotationBasis[8] = -1.0f;
    runRejected(interpolationBasisError, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot reflectionBasis = valid;
    reflectionBasis.rigidBodies[0].worldTransform.rotationBasis[8] = -1.0f;
    runRejected(reflectionBasis, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot countMismatch = valid;
    countMismatch.rigidBodies.pop_back();
    runRejected(countMismatch, TimelineStatus::SnapshotMismatch);

    PhysicsSnapshot modeTampered = valid;
    modeTampered.rigidBodies[0].mode = PmxRigidBodyMode::Physics;
    runRejected(modeTampered, TimelineStatus::SnapshotMismatch);

    PhysicsSnapshot schemaMismatch = valid;
    schemaMismatch.schemaVersion = 3U;
    runRejected(schemaMismatch, TimelineStatus::SnapshotMismatch);

    PhysicsSnapshot massTampered = valid;
    std::uint32_t massBits = 0U;
    std::memcpy(
        &massBits,
        &massTampered.rigidBodies[0].definitionMass,
        sizeof(massBits)
    );
    massBits ^= 1U;
    std::memcpy(
        &massTampered.rigidBodies[0].definitionMass,
        &massBits,
        sizeof(massBits)
    );
    runRejected(massTampered, TimelineStatus::SnapshotMismatch);

    PhysicsSnapshot nonCanonical = valid;
    nonCanonical.canonical = false;
    runRejected(nonCanonical, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot badForce = valid;
    badForce.rigidBodies[0].totalForce.x = 1.0f;
    runRejected(badForce, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot negativeZeroForce = valid;
    negativeZeroForce.rigidBodies[0].totalForce.x = -0.0f;
    runRejected(negativeZeroForce, TimelineStatus::InvalidSnapshot);

    // Activation preconditions only apply to Physics/PhysicsWithBone;
    // FollowBone activation is informational.
    std::size_t physicsBodyIndex = 0U;
    for (std::size_t index = 0U; index < valid.rigidBodies.size(); ++index)
    {
        if (valid.rigidBodies[index].mode !=
            PmxRigidBodyMode::FollowBone)
        {
            physicsBodyIndex = index;
            break;
        }
    }
    PhysicsSnapshot badActivation = valid;
    badActivation.rigidBodies[physicsBodyIndex].activationState = 0;
    runRejected(badActivation, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot negativeZeroDeactivation = valid;
    negativeZeroDeactivation.rigidBodies[physicsBodyIndex].deactivationTime =
        -0.0f;
    runRejected(negativeZeroDeactivation, TimelineStatus::InvalidSnapshot);

    PhysicsSnapshot badTick = valid;
    badTick.physicsTick = valid.motionFrame * 4U + 1U;
    runRejected(badTick, TimelineStatus::InvalidSnapshot);
}

void TestR12BConfigurationFingerprintMismatch()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto reference = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot =
        CaptureCanonicalAt(*reference, 0U);

    const auto expectMismatch = [&](SabaPhysicsSettings settings,
                                    const char* what)
    {
        auto other = std::make_unique<SabaMmdRuntimeModel>(
            modelPath,
            std::filesystem::path{},
            settings
        );
        Require(
            other->Initialize(),
            "R1.2B config-mismatch runtime failed to initialize"
        );
        auto* restore = dynamic_cast<IPhysicsStateAccess*>(other.get());
        Require(
            restore != nullptr,
            "R1.2B config-mismatch lost restore"
        );
        RequireRestoreAnimationFrame(*other, 0.0);
        Require(
            restore->RestoreState(snapshot) ==
                TimelineStatus::SnapshotMismatch,
            std::string("Different ") + what +
                " configuration passed the restore gate"
        );
    };

    SabaPhysicsSettings differentGravity;
    differentGravity.fixedTimeStep = 1.0f / 120.0f;
    differentGravity.maxSubSteps = 10;
    differentGravity.gravity = glm::vec3(0.0f, -99.0f, 0.0f);
    differentGravity.enabled = true;
    expectMismatch(differentGravity, "gravity");

    SabaPhysicsSettings differentTimestep;
    differentTimestep.fixedTimeStep = 1.0f / 60.0f;
    differentTimestep.maxSubSteps = 10;
    differentTimestep.gravity = glm::vec3(0.0f, -98.0f, 0.0f);
    differentTimestep.enabled = true;
    expectMismatch(differentTimestep, "fixed timestep");
}

void TestR12BCrossLayoutRejected()
{
    const std::filesystem::path sourcePath =
        FixturePath("pmx21-flip-impulse");
    RequireCoreAsset("pmx21-flip-impulse");
    auto source = CreateDeterministicRuntime(sourcePath);
    const PhysicsSnapshot snapshot =
        CaptureCanonicalAt(*source, 0U);

    const std::filesystem::path targetPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto target = CreateDeterministicRuntime(targetPath);
    auto* targetRestore = dynamic_cast<IPhysicsStateAccess*>(target.get());
    Require(
        targetRestore != nullptr,
        "R1.2B cross-layout test lost restore"
    );
    RequireRestoreAnimationFrame(*target, 0.0);
    Require(
        targetRestore->RestoreState(snapshot) ==
            TimelineStatus::SnapshotMismatch,
        "Cross-layout snapshot passed the restore gate"
    );
}

void TestR12BAnimationPrecondition()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        mmd != nullptr && restore != nullptr && observation != nullptr,
        "R1.2B animation precondition lost a runtime surface"
    );
    // Move the animation tag away from the snapshot frame.
    mmd->SetMotionFrame(5.0);
    PhysicsSnapshot before;
    Require(
        observation->CaptureState(before) == TimelineStatus::Ok,
        "R1.2B animation precondition baseline capture failed"
    );
    Require(
        restore->RestoreState(snapshot) == TimelineStatus::InvalidState,
        "R1.2B restore accepted a mismatched animation frame"
    );
    PhysicsSnapshot after;
    Require(
        observation->CaptureState(after) == TimelineStatus::Ok,
        "R1.2B animation precondition post capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(before, after),
        "R1.2B animation precondition failure modified the world"
    );

    // FollowBone transform mismatch with a correct frame tag must also be
    // rejected as InvalidState.
    mmd->SetMotionFrame(0.0);
    PhysicsSnapshot tamperedFollowBone = snapshot;
    for (RigidBodySnapshot& body : tamperedFollowBone.rigidBodies)
    {
        if (body.mode == PmxRigidBodyMode::FollowBone)
        {
            body.worldTransform.position.x += 1.0f;
            break;
        }
    }
    Require(
        restore->RestoreState(tamperedFollowBone) ==
            TimelineStatus::InvalidState,
        "R1.2B restore accepted a mismatched FollowBone transform"
    );
}

void TestR12BNoDirectStepAfterRestore()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(runtime.get());
    Require(
        restore != nullptr && stepper != nullptr,
        "R1.2B no-step test lost a runtime surface"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    Require(
        restore->RestoreState(snapshot) == TimelineStatus::Ok,
        "R1.2B no-step restore failed"
    );
    Require(
        stepper->StepMotionFrameExact(1U, {}) ==
            TimelineStatus::InvalidState,
        "R1.2B restore unexpectedly enabled direct stepping"
    );
}

void TestR12BFollowBoneAndMode2Restore()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    bool hasFollowBone = false;
    bool hasPhysicsWithBone = false;
    std::size_t mode2Index = 0U;
    for (std::size_t index = 0U; index < snapshot.rigidBodies.size(); ++index)
    {
        const RigidBodySnapshot& body = snapshot.rigidBodies[index];
        hasFollowBone =
            hasFollowBone || body.mode == PmxRigidBodyMode::FollowBone;
        if (body.mode == PmxRigidBodyMode::PhysicsWithBone)
        {
            hasPhysicsWithBone = true;
            mode2Index = index;
        }
    }
    Require(
        hasFollowBone && hasPhysicsWithBone,
        "pmx_physics fixture lacks FollowBone or Mode 2 coverage"
    );
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        restore != nullptr && mmd != nullptr && observation != nullptr,
        "R1.2B mode test lost a runtime surface"
    );

    // Replay long enough for Mode 2 to move and rotate, then record the
    // snapshot-time pose (body + bone local matrices).
    const PhysicsSnapshot snapshot300 = CaptureCanonicalAt(*runtime, 300U);
    Require(
        BodyMoved(
            snapshot.rigidBodies[mode2Index],
            snapshot300.rigidBodies[mode2Index]
        ),
        "R1.2B Mode 2 body did not move during replay"
    );
    const std::vector<glm::mat4> poseAtSnapshot(
        runtime->GetPose().LocalMatrices().begin(),
        runtime->GetPose().LocalMatrices().end()
    );

    Require(
        mmd->EvaluateTick(340U, SeekPolicy::ReplayFromStart, {}) ==
            TimelineStatus::Ok,
        "R1.2B Mode 2 perturbation failed"
    );
    RequireRestoreAnimationFrame(*runtime, 300.0);
    Require(
        restore->RestoreState(snapshot300) == TimelineStatus::Ok,
        "R1.2B Mode 2 restore failed"
    );
    PhysicsSnapshot after;
    Require(
        observation->CaptureState(after) == TimelineStatus::Ok,
        "R1.2B Mode 2 capture failed"
    );
    for (std::size_t index = 0U; index < snapshot300.rigidBodies.size(); ++index)
    {
        Require(
            after.rigidBodies[index].mode ==
                snapshot300.rigidBodies[index].mode,
            "R1.2B restore changed a rigid-body mode"
        );
    }
    // Mode 2 merge write-back: body transform and bone hierarchy must match
    // the snapshot-time state bit-exactly.
    Require(
        SameTransformSnapshot(
            after.rigidBodies[mode2Index].worldTransform,
            snapshot300.rigidBodies[mode2Index].worldTransform
        ),
        "R1.2B Mode 2 body transform did not round-trip"
    );
    const std::span<const glm::mat4> poseAfter =
        runtime->GetPose().LocalMatrices();
    Require(
        poseAfter.size() == poseAtSnapshot.size(),
        "R1.2B Mode 2 pose bone count changed"
    );
    std::size_t mismatchBone = 0U;
    glm::length_t mismatchColumn = 0U;
    glm::length_t mismatchRow = 0U;
    bool foundMismatch = false;
    for (std::size_t bone = 0U; bone < poseAfter.size(); ++bone)
    {
        for (glm::length_t column = 0; column < 4; ++column)
        {
            for (glm::length_t row = 0; row < 4; ++row)
            {
                if (poseAfter[bone][column][row] !=
                    poseAtSnapshot[bone][column][row])
                {
                    mismatchBone = bone;
                    mismatchColumn = column;
                    mismatchRow = row;
                    foundMismatch = true;
                    break;
                }
            }
            if (foundMismatch)
                break;
        }
        if (foundMismatch)
            break;
    }
    if (foundMismatch)
    {
        std::printf(
            "[R12B MODE2] snapshot body basis=[%g %g %g %g %g %g %g %g %g]\n"
            "  after body basis=[%g %g %g %g %g %g %g %g %g]\n"
            "  poseBefore[0]=[%g %g %g %g; %g %g %g %g; %g %g %g %g; %g %g %g %g]\n"
            "  poseAfter[0]=[%g %g %g %g; %g %g %g %g; %g %g %g %g; %g %g %g %g]\n",
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[0]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[1]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[2]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[3]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[4]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[5]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[6]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[7]
            ),
            static_cast<double>(
                snapshot300.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[8]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[0]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[1]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[2]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[3]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[4]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[5]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[6]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[7]
            ),
            static_cast<double>(
                after.rigidBodies[mode2Index]
                    .worldTransform.rotationBasis[8]
            ),
            static_cast<double>(poseAtSnapshot[0][0][0]),
            static_cast<double>(poseAtSnapshot[0][0][1]),
            static_cast<double>(poseAtSnapshot[0][0][2]),
            static_cast<double>(poseAtSnapshot[0][0][3]),
            static_cast<double>(poseAtSnapshot[0][1][0]),
            static_cast<double>(poseAtSnapshot[0][1][1]),
            static_cast<double>(poseAtSnapshot[0][1][2]),
            static_cast<double>(poseAtSnapshot[0][1][3]),
            static_cast<double>(poseAtSnapshot[0][2][0]),
            static_cast<double>(poseAtSnapshot[0][2][1]),
            static_cast<double>(poseAtSnapshot[0][2][2]),
            static_cast<double>(poseAtSnapshot[0][2][3]),
            static_cast<double>(poseAtSnapshot[0][3][0]),
            static_cast<double>(poseAtSnapshot[0][3][1]),
            static_cast<double>(poseAtSnapshot[0][3][2]),
            static_cast<double>(poseAtSnapshot[0][3][3]),
            static_cast<double>(poseAfter[0][0][0]),
            static_cast<double>(poseAfter[0][0][1]),
            static_cast<double>(poseAfter[0][0][2]),
            static_cast<double>(poseAfter[0][0][3]),
            static_cast<double>(poseAfter[0][1][0]),
            static_cast<double>(poseAfter[0][1][1]),
            static_cast<double>(poseAfter[0][1][2]),
            static_cast<double>(poseAfter[0][1][3]),
            static_cast<double>(poseAfter[0][2][0]),
            static_cast<double>(poseAfter[0][2][1]),
            static_cast<double>(poseAfter[0][2][2]),
            static_cast<double>(poseAfter[0][2][3]),
            static_cast<double>(poseAfter[0][3][0]),
            static_cast<double>(poseAfter[0][3][1]),
            static_cast<double>(poseAfter[0][3][2]),
            static_cast<double>(poseAfter[0][3][3])
        );
        Require(
            false,
            "R1.2B Mode 2 restore changed bone local matrix at bone " +
                std::to_string(mismatchBone) + " [" +
                std::to_string(mismatchColumn) + "][" +
                std::to_string(mismatchRow) + "] before=" +
                std::to_string(
                    static_cast<double>(
                        poseAtSnapshot[mismatchBone][mismatchColumn][mismatchRow]
                    )
                ) +
                " after=" +
                std::to_string(
                    static_cast<double>(
                        poseAfter[mismatchBone][mismatchColumn][mismatchRow]
                    )
                )
        );
    }
    else
    {
        Require(
            true,
            "R1.2B Mode 2 pose matrices matched"
        );
    }
}

void TestR12BPoisonedFaultInjection()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(runtime.get());
    Require(
        mmd != nullptr && restore != nullptr &&
            observation != nullptr && stepper != nullptr,
        "R1.2B Poisoned test lost a runtime surface"
    );
#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)
    Require(
        mmd->EvaluateTick(30U, SeekPolicy::ReplayFromStart, {}) ==
            TimelineStatus::Ok,
        "R1.2B Poisoned perturbation failed"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    runtime->SetFaultInjectionPhase(4);
    Require(
        restore->RestoreState(snapshot) == TimelineStatus::Poisoned,
        "R1.2B fault injection did not poison the instance"
    );
    PhysicsSnapshot untouched;
    Require(
        observation->CaptureState(untouched) == TimelineStatus::Poisoned,
        "R1.2B CaptureState did not report Poisoned"
    );
    Require(
        stepper->StepMotionFrameExact(1U, {}) == TimelineStatus::Poisoned,
        "R1.2B StepMotionFrameExact did not report Poisoned"
    );
    Require(
        mmd->EvaluateTick(10U, SeekPolicy::ReplayFromStart, {}) ==
            TimelineStatus::Poisoned,
        "R1.2B EvaluateTick did not report Poisoned"
    );
    PhysicsStepDiagnostics diagnostics;
    Require(
        observation->ReadStepDiagnostics(diagnostics) ==
                TimelineStatus::Ok &&
            diagnostics.poisoned,
        "R1.2B diagnostics did not expose Poisoned"
    );
    // Recovery entry.
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "R1.2B PrepareFrameZero recovery failed"
    );
    PhysicsSnapshot recovered;
    Require(
        observation->CaptureState(recovered) == TimelineStatus::Ok,
        "R1.2B recovery capture failed"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    Require(
        restore->RestoreState(snapshot) == TimelineStatus::Ok,
        "R1.2B restore after recovery failed"
    );
#else
    Require(false, "R1.2B test hooks are not compiled in");
#endif
}

void TestR12BRestoreStressRoundTrip()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        restore != nullptr && observation != nullptr,
        "R1.2B stress test lost a runtime surface"
    );
    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        RequireRestoreAnimationFrame(*runtime, 0.0);
        Require(
            restore->RestoreState(snapshot) == TimelineStatus::Ok,
            "R1.2B stress restore failed"
        );
    }
    PhysicsSnapshot final;
    Require(
        observation->CaptureState(final) == TimelineStatus::Ok,
        "R1.2B stress capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(snapshot, final),
        "R1.2B 1000 restore round-trips drifted"
    );
}

void TestR12BDisableDeactivationHistory()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(runtime.get());
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        stepper != nullptr && restore != nullptr && observation != nullptr,
        "R1.2B DISABLE history test lost a runtime surface"
    );
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "R1.2B DISABLE history PrepareFrameZero failed"
    );
#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)
    // Build a real DISABLE_DEACTIVATION history on the target instance.
    runtime->SetAllDynamicBodiesActivationForProbe(4);  // DISABLE_DEACTIVATION
    PhysicsSnapshot disableHistory;
    Require(
        observation->CaptureState(disableHistory) == TimelineStatus::Ok,
        "R1.2B DISABLE history capture failed"
    );
    Require(
        disableHistory.canonical,
        "R1.2B DISABLE history capture lost its canonical claim"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    Require(
        restore->RestoreState(disableHistory) ==
            TimelineStatus::InvalidSnapshot,
        "R1.2B DISABLE_DEACTIVATION history passed the restore gate"
    );
    // PrepareFrameZero normalizes activation back to ACTIVE_TAG.
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "R1.2B DISABLE recovery failed"
    );
    PhysicsSnapshot canonical;
    Require(
        observation->CaptureState(canonical) == TimelineStatus::Ok,
        "R1.2B DISABLE canonical capture failed"
    );
    for (const RigidBodySnapshot& body : canonical.rigidBodies)
    {
        if (body.mode != PmxRigidBodyMode::FollowBone)
        {
            Require(
                body.activationState == 1,  // ACTIVE_TAG
                "R1.2B canonical recovery left a non-ACTIVE_TAG body"
            );
        }
    }
    RequireRestoreAnimationFrame(*runtime, 0.0);
    Require(
        restore->RestoreState(canonical) == TimelineStatus::Ok,
        "R1.2B canonical restore after DISABLE history failed"
    );
#else
    Require(false, "R1.2B test hooks are not compiled in");
#endif
}

void TestR12BFollowBoneNonZeroDefinitionMass()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(runtime.get());
    Require(stepper != nullptr, "R1.2B T23 lost the stepper");
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "R1.2B T23 PrepareFrameZero failed"
    );
    PhysicsSnapshot baseline;
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(observation != nullptr, "R1.2B T23 lost observation");
    Require(
        observation->CaptureState(baseline) == TimelineStatus::Ok,
        "R1.2B T23 baseline capture failed"
    );
    std::size_t followBoneIndex = 0U;
    for (std::size_t index = 0U; index < baseline.rigidBodies.size(); ++index)
    {
        if (baseline.rigidBodies[index].mode ==
            PmxRigidBodyMode::FollowBone)
        {
            followBoneIndex = index;
            break;
        }
    }
    // A FollowBone body may legally carry a nonzero raw PMX mass; runtime
    // mode (not mass) decides kinematic semantics.
    runtime->SetDefinitionMassForProbe(
        static_cast<std::uint32_t>(followBoneIndex),
        123.0f
    );
    PhysicsSnapshot snapshot = CaptureCanonicalAt(*runtime, 0U);
    Require(
        snapshot.rigidBodies[followBoneIndex].definitionMass == 123.0f,
        "R1.2B T23 definition-mass override did not reach capture"
    );
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* restore = dynamic_cast<IPhysicsStateAccess*>(runtime.get());
    Require(
        mmd != nullptr && restore != nullptr,
        "R1.2B T23 lost a runtime surface"
    );
    Require(
        mmd->EvaluateTick(30U, SeekPolicy::ReplayFromStart, {}) ==
            TimelineStatus::Ok,
        "R1.2B T23 perturbation failed"
    );
    RequireRestoreAnimationFrame(*runtime, 0.0);
    Require(
        restore->RestoreState(snapshot) == TimelineStatus::Ok,
        "R1.2B T23 restore with nonzero FollowBone mass failed"
    );
    PhysicsSnapshot after;
    Require(
        observation->CaptureState(after) == TimelineStatus::Ok,
        "R1.2B T23 capture failed"
    );
    Require(
        after.rigidBodies[followBoneIndex].mode ==
                PmxRigidBodyMode::FollowBone &&
            after.rigidBodies[followBoneIndex].definitionMass == 123.0f,
        "R1.2B T23 FollowBone semantics changed under a nonzero raw mass"
    );
#else
    Require(false, "R1.2B test hooks are not compiled in");
#endif
}

// R1.2C helpers ------------------------------------------------------------

namespace
{
FrameCheckpoint CreateCheckpointAt(
    SabaMmdRuntimeModel& runtime,
    MotionFrameIndex frame
)
{
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(&runtime);
    Require(mmd != nullptr, "R1.2C helper lost the MMD runtime");
    mmd->SetMotionLooping(false);
    CaptureCanonicalAt(runtime, frame);
    FrameCheckpoint checkpoint;
    Require(
        mmd->CreateCheckpoint(checkpoint) == TimelineStatus::Ok,
        "R1.2C helper CreateCheckpoint failed"
    );
    return checkpoint;
}

void RequireEquivalentToFromStart(
    MotionFrameIndex checkpointFrame,
    MotionFrameIndex target
)
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto baseline = CreateDeterministicRuntime(modelPath);
    auto source = CreateDeterministicRuntime(modelPath);
    auto diverged = CreateDeterministicRuntime(modelPath);

    CaptureCanonicalAt(*baseline, target);
    const FrameStateHashes baselineHashes =
        CaptureDeterminismHashes(*baseline);

    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, checkpointFrame);

    auto* divergedMmd = dynamic_cast<MmdRuntimeModel*>(diverged.get());
    Require(divergedMmd != nullptr, "R1.2C equivalence lost runtime");
    // Force a different history before replaying from the checkpoint.
    CaptureCanonicalAt(*diverged, checkpointFrame + 30U);
    Require(
        divergedMmd->ReplayFromCheckpoint(checkpoint, target) ==
            TimelineStatus::Ok,
        "R1.2C ReplayFromCheckpoint failed"
    );
    const FrameStateHashes divergedHashes =
        CaptureDeterminismHashes(*diverged);
    Require(
        baselineHashes.pose.exactHash == divergedHashes.pose.exactHash &&
            baselineHashes.vertex.exactHash ==
                divergedHashes.vertex.exactHash &&
            baselineHashes.physics.exactHash ==
                divergedHashes.physics.exactHash,
        "R1.2C ReplayFromCheckpoint diverged from ReplayFromStart"
    );
}

void RequireEquivalentToFromStartWithVmd(
    MotionFrameIndex checkpointFrame,
    MotionFrameIndex target,
    const std::filesystem::path& vmdPath
)
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto baseline = CreateDeterministicRuntime(modelPath, vmdPath);
    auto source = CreateDeterministicRuntime(modelPath, vmdPath);
    auto diverged = CreateDeterministicRuntime(modelPath, vmdPath);

    CaptureCanonicalAt(*baseline, target);
    const FrameStateHashes baselineHashes =
        CaptureDeterminismHashes(*baseline);

    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, checkpointFrame);

    auto* divergedMmd = dynamic_cast<MmdRuntimeModel*>(diverged.get());
    Require(divergedMmd != nullptr, "R1.2C VMD equivalence lost runtime");
    CaptureCanonicalAt(*diverged, checkpointFrame + 30U);
    Require(
        divergedMmd->ReplayFromCheckpoint(checkpoint, target) ==
            TimelineStatus::Ok,
        "R1.2C VMD ReplayFromCheckpoint failed"
    );
    const FrameStateHashes divergedHashes =
        CaptureDeterminismHashes(*diverged);
    Require(
        baselineHashes.pose.exactHash == divergedHashes.pose.exactHash &&
            baselineHashes.vertex.exactHash ==
                divergedHashes.vertex.exactHash &&
            baselineHashes.physics.exactHash ==
                divergedHashes.physics.exactHash,
        "R1.2C VMD ReplayFromCheckpoint diverged from ReplayFromStart"
    );
}

// Writes a minimal but structurally valid VMD: header, one root-bone motion
// track with two keyframes (frame 0 and maxFrame), and no morph/camera
// sections. The variant selects the frame-maxFrame translate x so two files
// hash differently for cross-VMD rejection. Saba's reader only requires the
// 30-byte header, 20-byte model name, motion count, per-motion records and
// then stops at end-of-file, so this is a self-contained CORE fixture.
std::filesystem::path WriteMinimalVmd(
    std::uint32_t variant,
    std::uint32_t maxFrame
)
{
    namespace fs = std::filesystem;
    const fs::path directory =
        fs::temp_directory_path() / "wisteria_r12c_fixtures";
    fs::create_directories(directory);
    const fs::path path =
        directory / ("r12c_motion_" + std::to_string(variant) + ".vmd");

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(out.is_open(), "R1.2C VMD fixture could not be written");

    const auto writeBytes = [&out](const void* data, std::size_t size)
    {
        out.write(
            static_cast<const char*>(data),
            static_cast<std::streamsize>(size)
        );
    };
    const auto writeU32 = [&out, &writeBytes](std::uint32_t value)
    {
        writeBytes(&value, sizeof(value));
    };
    const auto writeF32 = [&out, &writeBytes](float value)
    {
        writeBytes(&value, sizeof(value));
    };
    const auto writeFixed = [&writeBytes](
        const char* text,
        std::size_t capacity
    )
    {
        char buffer[32]{};
        const std::size_t length = std::strlen(text);
        std::memcpy(
            buffer,
            text,
            std::min(length, capacity)
        );
        writeBytes(buffer, capacity);
    };

    // Header: "Vocaloid Motion Data 0002" padded to 30 bytes, then a
    // 20-byte model-name field. Saba ignores the model name on load.
    const char header[30] = "Vocaloid Motion Data 0002";
    writeBytes(header, sizeof(header));
    char modelName[20]{};
    writeBytes(modelName, sizeof(modelName));

    // One bone track: keyframes at frame 0 and maxFrame.
    writeU32(2U);
    const float translateZero[3] = {0.0f, 0.0f, 0.0f};
    const float translateEnd[3] = {
        variant == 0U ? 0.3f : 0.5f,
        0.0f,
        0.0f
    };
    const float identityQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const unsigned char interpolation[64] = {};
    const auto writeKey = [&](
        std::uint32_t frame,
        const float translate[3]
    )
    {
        writeFixed("root", 15U);
        writeU32(frame);
        writeF32(translate[0]);
        writeF32(translate[1]);
        writeF32(translate[2]);
        writeF32(identityQuat[0]);
        writeF32(identityQuat[1]);
        writeF32(identityQuat[2]);
        writeF32(identityQuat[3]);
        writeBytes(interpolation, sizeof(interpolation));
    };
    writeKey(0U, translateZero);
    writeKey(maxFrame, translateEnd);

    // Morph section: none. The reader stops cleanly at end-of-file.
    writeU32(0U);
    out.close();
    return path;
}
}  // namespace

void TestR12CEquivalenceMatrix()
{
    RequireEquivalentToFromStart(0U, 1U);
    RequireEquivalentToFromStart(0U, 150U);
    RequireEquivalentToFromStart(0U, 300U);
    RequireEquivalentToFromStart(1U, 150U);
    RequireEquivalentToFromStart(1U, 2U);
    RequireEquivalentToFromStart(1U, 3U);
    RequireEquivalentToFromStart(150U, 300U);
    RequireEquivalentToFromStart(300U, 340U);

    // The equivalence matrix is the gate for the checkpoint capability
    // bits: only after every matrix case passes may the backend advertise
    // the surface. This guards against flipping the bits on compile-only.
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const ModelRuntimeCapabilities capabilities = runtime->Capabilities();
    Require(
        capabilities.checkpoint.supportsCheckpointCapture &&
            capabilities.checkpoint.supportsCheckpointRestore &&
            capabilities.checkpoint.supportsReplayFromCheckpoint,
        "R1.2C checkpoint capabilities must be open after the "
        "equivalence matrix passes"
    );
}

void TestR12CZeroStepRestore()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto source = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, 150U);

    auto diverged = CreateDeterministicRuntime(modelPath);
    CaptureCanonicalAt(*diverged, 180U);
    auto* divergedMmd = dynamic_cast<MmdRuntimeModel*>(diverged.get());
    auto* divergedStepper = dynamic_cast<IDeterministicFrameStepper*>(
        diverged.get()
    );
    auto* divergedObservation =
        dynamic_cast<IDeterministicPhysicsObservation*>(diverged.get());
    Require(
        divergedMmd != nullptr && divergedStepper != nullptr &&
            divergedObservation != nullptr,
        "R1.2C zero-step lost a runtime surface"
    );
    Require(
        divergedMmd->ReplayFromCheckpoint(checkpoint, 150U) ==
            TimelineStatus::Ok,
        "R1.2C zero-step ReplayFromCheckpoint failed"
    );
    PhysicsSnapshot restored;
    Require(
        divergedObservation->CaptureState(restored) == TimelineStatus::Ok,
        "R1.2C zero-step capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(
            checkpoint.physics,
            restored
        ),
        "R1.2C zero-step restore did not reproduce the checkpoint physics"
    );
    Require(
        divergedStepper->StepMotionFrameExact(151U, {}) ==
            TimelineStatus::Ok,
        "R1.2C zero-step restore did not prepare frame N+1"
    );

    // Also verify the restore reproduces a non-settled frame (N=1).
    auto sourceOne = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint checkpointOne =
        CreateCheckpointAt(*sourceOne, 1U);
    auto divergedOne = CreateDeterministicRuntime(modelPath);
    CaptureCanonicalAt(*divergedOne, 31U);
    auto* divergedOneMmd = dynamic_cast<MmdRuntimeModel*>(divergedOne.get());
    auto* divergedOneObservation =
        dynamic_cast<IDeterministicPhysicsObservation*>(divergedOne.get());
    Require(
        divergedOneMmd != nullptr && divergedOneObservation != nullptr,
        "R1.2C zero-step N=1 lost a runtime surface"
    );
    Require(
        divergedOneMmd->ReplayFromCheckpoint(checkpointOne, 1U) ==
            TimelineStatus::Ok,
        "R1.2C zero-step N=1 replay failed"
    );
    PhysicsSnapshot restoredOne;
    Require(
        divergedOneObservation->CaptureState(restoredOne) ==
            TimelineStatus::Ok,
        "R1.2C zero-step N=1 capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(
            checkpointOne.physics,
            restoredOne
        ),
        "R1.2C zero-step restore diverged at frame 1"
    );
}

void TestR12CBeyondMotionEnd()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto source = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, 300U);
    const FrameStateHashes atCheckpoint =
        CaptureDeterminismHashes(*source);

    auto diverged = CreateDeterministicRuntime(modelPath);
    CaptureCanonicalAt(*diverged, 330U);
    auto* divergedMmd = dynamic_cast<MmdRuntimeModel*>(diverged.get());
    Require(divergedMmd != nullptr, "R1.2C beyond-end lost runtime");
    Require(
        divergedMmd->ReplayFromCheckpoint(checkpoint, 340U) ==
            TimelineStatus::Ok,
        "R1.2C beyond-end replay failed"
    );
    const FrameStateHashes beyondEnd =
        CaptureDeterminismHashes(*diverged);
    Require(
        atCheckpoint.pose.exactHash == beyondEnd.pose.exactHash &&
            atCheckpoint.vertex.exactHash == beyondEnd.vertex.exactHash,
        "R1.2C beyond-end replay changed the held motion-end pose"
    );
    Require(
        atCheckpoint.physics.exactHash != beyondEnd.physics.exactHash,
        "R1.2C beyond-end replay did not advance physics"
    );
}

void TestR12CCreateRejectsNonCanonical()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    Require(mmd != nullptr, "R1.2C create-reject lost runtime");
    // Normal (non-canonical) playback state.
    runtime->Update(1.0f / 30.0f);
    FrameCheckpoint output;
    output.frame = 99U;
    Require(
        mmd->CreateCheckpoint(output) == TimelineStatus::InvalidState,
        "R1.2C CreateCheckpoint accepted a non-canonical boundary"
    );
    Require(
        output.frame == 99U,
        "R1.2C CreateCheckpoint modified output on failure"
    );
}

void TestR12CCheckpointStructuralRejections()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint valid = CreateCheckpointAt(*runtime, 0U);
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* observation = dynamic_cast<IDeterministicPhysicsObservation*>(
        runtime.get()
    );
    Require(
        mmd != nullptr && observation != nullptr,
        "R1.2C structural test lost a runtime surface"
    );
    const auto expectRejected = [&](const FrameCheckpoint& candidate,
                                    TimelineStatus expected)
    {
        PhysicsSnapshot before;
        Require(
            observation->CaptureState(before) == TimelineStatus::Ok,
            "R1.2C structural baseline capture failed"
        );
        const TimelineStatus status = mmd->RestoreCheckpoint(candidate);
        Require(
            status == expected,
            "R1.2C structural rejection returned the wrong status"
        );
        PhysicsSnapshot after;
        Require(
            observation->CaptureState(after) == TimelineStatus::Ok,
            "R1.2C structural post capture failed"
        );
        Require(
            SnapshotsEqualExceptFollowBoneActivation(before, after),
            "R1.2C structural rejection modified the world"
        );
    };

    FrameCheckpoint badFrame = valid;
    badFrame.frame += 1U;
    expectRejected(badFrame, TimelineStatus::InvalidCheckpoint);

    FrameCheckpoint badConfig = valid;
    badConfig.config.motionFps = 60U;
    expectRejected(badConfig, TimelineStatus::InvalidCheckpoint);

    FrameCheckpoint badLoop = valid;
    badLoop.overrides.loopMotion = true;
    expectRejected(badLoop, TimelineStatus::InvalidCheckpoint);

    FrameCheckpoint badHash = valid;
    badHash.fingerprint.state.physics.exactHash ^= 1U;
    expectRejected(badHash, TimelineStatus::InvalidCheckpoint);

    FrameCheckpoint badCanonical = valid;
    badCanonical.physics.canonical = false;
    expectRejected(badCanonical, TimelineStatus::InvalidCheckpoint);
}

void TestR12CCrossCompatibilityRejected()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto source = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint valid = CreateCheckpointAt(*source, 0U);

    SabaPhysicsSettings differentSettings;
    differentSettings.fixedTimeStep = 1.0f / 120.0f;
    differentSettings.maxSubSteps = 10;
    differentSettings.gravity = glm::vec3(0.0f, -99.0f, 0.0f);
    differentSettings.enabled = true;
    auto other = std::make_unique<SabaMmdRuntimeModel>(
        modelPath,
        std::filesystem::path{},
        differentSettings
    );
    Require(
        other->Initialize(),
        "R1.2C cross-compat runtime failed to initialize"
    );
    auto* otherMmd = dynamic_cast<MmdRuntimeModel*>(other.get());
    Require(otherMmd != nullptr, "R1.2C cross-compat lost runtime");
    otherMmd->SetMotionLooping(false);
    Require(
        otherMmd->RestoreCheckpoint(valid) ==
            TimelineStatus::SnapshotMismatch,
        "R1.2C accepted a checkpoint from a different configuration"
    );

    FrameCheckpoint badAsset = valid;
    badAsset.fingerprint.asset.pmxFileHash ^= 1U;
    auto* sourceMmd = dynamic_cast<MmdRuntimeModel*>(source.get());
    Require(
        sourceMmd->RestoreCheckpoint(badAsset) ==
            TimelineStatus::SnapshotMismatch,
        "R1.2C accepted a checkpoint with a different asset identity"
    );
}

void TestR12CPostRestoreHashPoisoned()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto source = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, 0U);
    auto diverged = CreateDeterministicRuntime(modelPath);
    CaptureCanonicalAt(*diverged, 30U);
    auto* divergedMmd = dynamic_cast<MmdRuntimeModel*>(diverged.get());
    auto* divergedStepper = dynamic_cast<IDeterministicFrameStepper*>(
        diverged.get()
    );
    Require(
        divergedMmd != nullptr && divergedStepper != nullptr,
        "R1.2C hash-poison test lost a runtime surface"
    );
#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)
    diverged->SetPostRestoreHashCorruptionForProbe(1U);
    Require(
        divergedMmd->ReplayFromCheckpoint(checkpoint, 1U) ==
            TimelineStatus::DeterminismViolation,
        "R1.2C post-restore hash corruption was not detected"
    );
    FrameCheckpoint ignored;
    Require(
        divergedMmd->CreateCheckpoint(ignored) == TimelineStatus::Poisoned,
        "R1.2C hash corruption did not poison the instance"
    );
    Require(
        divergedStepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "R1.2C hash corruption recovery failed"
    );
    diverged->SetPostRestoreHashCorruptionForProbe(0U);
    Require(
        divergedMmd->ReplayFromCheckpoint(checkpoint, 1U) ==
            TimelineStatus::Ok,
        "R1.2C replay after recovery failed"
    );
#else
    Require(false, "R1.2C test hooks are not compiled in");
#endif
}

void TestR12CCheckpointStressRoundTrip()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint checkpoint = CreateCheckpointAt(*runtime, 0U);
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    Require(mmd != nullptr, "R1.2C stress lost runtime");
    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        Require(
            mmd->RestoreCheckpoint(checkpoint) == TimelineStatus::Ok,
            "R1.2C stress RestoreCheckpoint failed"
        );
    }
    FrameCheckpoint recaptured;
    Require(
        mmd->CreateCheckpoint(recaptured) == TimelineStatus::Ok,
        "R1.2C stress recapture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(
            checkpoint.physics,
            recaptured.physics
        ),
        "R1.2C 1000 create/restore cycles drifted"
    );
}

void TestR12CVmdEquivalence()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const std::filesystem::path vmdPath = WriteMinimalVmd(0U, 30U);

    // Constructor-supplied VMD must enter asset identity.
    auto identityProbe = CreateDeterministicRuntime(modelPath, vmdPath);
    auto* identityMmd = dynamic_cast<MmdRuntimeModel*>(identityProbe.get());
    Require(
        identityMmd != nullptr,
        "R1.2C VMD identity lost runtime"
    );
    Require(
        identityMmd->HasMotion(),
        "R1.2C constructor VMD lost hasMotion identity"
    );

    // The fixture must actually drive animation state; otherwise the
    // equivalence below would be vacuous.
    auto poseProbe = CreateDeterministicRuntime(modelPath, vmdPath);
    CaptureCanonicalAt(*poseProbe, 0U);
    const FrameStateHashes frame0 =
        CaptureDeterminismHashes(*poseProbe);
    CaptureCanonicalAt(*poseProbe, 30U);
    const FrameStateHashes frame30 =
        CaptureDeterminismHashes(*poseProbe);
    Require(
        frame0.pose.exactHash != frame30.pose.exactHash ||
            frame0.vertex.exactHash != frame30.vertex.exactHash,
        "R1.2C VMD fixture did not drive animation state"
    );

    RequireEquivalentToFromStartWithVmd(0U, 2U, vmdPath);
    RequireEquivalentToFromStartWithVmd(1U, 30U, vmdPath);
    RequireEquivalentToFromStartWithVmd(15U, 30U, vmdPath);
    // Checkpoint at the true VMD end, replay beyond it.
    RequireEquivalentToFromStartWithVmd(30U, 45U, vmdPath);
}

void TestR12CTrueMotionEndHold()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const std::filesystem::path vmdPath = WriteMinimalVmd(0U, 30U);

    auto source = CreateDeterministicRuntime(modelPath, vmdPath);
    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, 30U);
    const FrameStateHashes atEnd =
        CaptureDeterminismHashes(*source);

    auto diverged = CreateDeterministicRuntime(modelPath, vmdPath);
    CaptureCanonicalAt(*diverged, 35U);
    auto* divergedMmd = dynamic_cast<MmdRuntimeModel*>(diverged.get());
    Require(divergedMmd != nullptr, "R1.2C motion-end lost runtime");
    Require(
        divergedMmd->ReplayFromCheckpoint(checkpoint, 45U) ==
            TimelineStatus::Ok,
        "R1.2C motion-end replay failed"
    );
    const FrameStateHashes beyondEnd =
        CaptureDeterminismHashes(*diverged);
    Require(
        atEnd.pose.exactHash == beyondEnd.pose.exactHash &&
            atEnd.vertex.exactHash == beyondEnd.vertex.exactHash,
        "R1.2C VMD motion end did not hold pose/vertex"
    );
    Require(
        atEnd.physics.exactHash != beyondEnd.physics.exactHash,
        "R1.2C VMD motion-end replay did not advance physics"
    );
}

void TestR12CMorphOverrideRestore()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");

    auto source = CreateDeterministicRuntime(modelPath);
    auto* sourceMmd = dynamic_cast<MmdRuntimeModel*>(source.get());
    Require(sourceMmd != nullptr, "R1.2C override lost runtime");
    sourceMmd->SetMotionLooping(false);
    Require(
        sourceMmd->SetMorphOverride("vertex", 0.5f),
        "R1.2C override fixture could not set source morph"
    );
    CaptureCanonicalAt(*source, 0U);
    FrameCheckpoint checkpoint;
    Require(
        sourceMmd->CreateCheckpoint(checkpoint) == TimelineStatus::Ok,
        "R1.2C override CreateCheckpoint failed"
    );
    Require(
        checkpoint.overrides.morphOverrides.size() == 1U &&
            checkpoint.overrides.morphOverrides[0].first == "vertex" &&
            checkpoint.overrides.morphOverrides[0].second == 0.5f,
        "R1.2C checkpoint did not capture the morph override"
    );

    auto target = CreateDeterministicRuntime(modelPath);
    auto* targetMmd = dynamic_cast<MmdRuntimeModel*>(target.get());
    targetMmd->SetMotionLooping(false);
    Require(
        targetMmd->SetMorphOverride("vertex", 0.9f),
        "R1.2C override fixture could not set target morph"
    );
    CaptureCanonicalAt(*target, 5U);
    Require(
        targetMmd->ReplayFromCheckpoint(checkpoint, 0U) ==
            TimelineStatus::Ok,
        "R1.2C override restore failed"
    );
    const std::optional<float> weight =
        targetMmd->MorphWeight("vertex");
    Require(
        weight.has_value() && *weight == 0.5f,
        "R1.2C morph override was not replaced by the checkpoint"
    );
    FrameCheckpoint recaptured;
    Require(
        targetMmd->CreateCheckpoint(recaptured) == TimelineStatus::Ok,
        "R1.2C override recapture failed"
    );
    Require(
        recaptured.overrides.morphOverrides ==
            checkpoint.overrides.morphOverrides,
        "R1.2C morph override fingerprint diverged after restore"
    );
}

void TestR12CCrossVmdRejected()
{
    const std::filesystem::path modelPath =
        FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const std::filesystem::path vmdA = WriteMinimalVmd(0U, 30U);
    const std::filesystem::path vmdB = WriteMinimalVmd(1U, 30U);

    auto source = CreateDeterministicRuntime(modelPath, vmdA);
    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, 0U);

    auto target = CreateDeterministicRuntime(modelPath, vmdB);
    auto* targetMmd = dynamic_cast<MmdRuntimeModel*>(target.get());
    Require(targetMmd != nullptr, "R1.2C cross-VMD lost runtime");
    Require(
        targetMmd->RestoreCheckpoint(checkpoint) ==
            TimelineStatus::SnapshotMismatch,
        "R1.2C cross-VMD checkpoint was not rejected"
    );

    auto same = CreateDeterministicRuntime(modelPath, vmdA);
    auto* sameMmd = dynamic_cast<MmdRuntimeModel*>(same.get());
    Require(
        sameMmd->RestoreCheckpoint(checkpoint) == TimelineStatus::Ok,
        "R1.2C same-VMD checkpoint was rejected"
    );
}

void TestR12CIkOverrideRestoreWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-leimi");
    RequireFullAsset("production-pmx-leimi");

    auto source = CreateDeterministicRuntime(modelPath);
    auto* sourceMmd = dynamic_cast<MmdRuntimeModel*>(source.get());
    Require(sourceMmd != nullptr, "R1.2C IK restore lost runtime");
    sourceMmd->SetMotionLooping(false);
    const std::string ikBoneName(
        reinterpret_cast<const char*>(u8"エンジンIK")
    );
    const BoneIndex ikBone = sourceMmd->FindBoneIndex(ikBoneName);
    Require(
        ikBone != InvalidBoneIndex,
        "R1.2C IK restore fixture lost the IK controller bone"
    );
    sourceMmd->SetMmdIkEnabled(ikBone, false);
    CaptureCanonicalAt(*source, 0U);
    FrameCheckpoint checkpoint;
    Require(
        sourceMmd->CreateCheckpoint(checkpoint) == TimelineStatus::Ok,
        "R1.2C IK CreateCheckpoint failed"
    );
    Require(
        !checkpoint.overrides.ikOverrides.empty() &&
            !checkpoint.overrides.ikOverrides[0].second,
        "R1.2C IK override was not captured as disabled"
    );

    auto target = CreateDeterministicRuntime(modelPath);
    auto* targetMmd = dynamic_cast<MmdRuntimeModel*>(target.get());
    targetMmd->SetMotionLooping(false);
    targetMmd->SetMmdIkEnabled(ikBone, true);
    CaptureCanonicalAt(*target, 3U);
    Require(
        targetMmd->ReplayFromCheckpoint(checkpoint, 0U) ==
            TimelineStatus::Ok,
        "R1.2C IK restore failed"
    );
    FrameCheckpoint recaptured;
    Require(
        targetMmd->CreateCheckpoint(recaptured) == TimelineStatus::Ok,
        "R1.2C IK recapture failed"
    );
    Require(
        recaptured.overrides.ikOverrides ==
            checkpoint.overrides.ikOverrides,
        "R1.2C IK override fingerprint diverged after restore"
    );
}

void TestR1ProjectMmdInstanceWhenAvailable()
{
    RequireFullAssetsTier();
    const std::filesystem::path modelPath =
        FixturePath("production-pmx-yeshiguang");
    const std::filesystem::path motionPath =
        FixturePath("production-vmd-body");
    RequireFullAsset("production-pmx-yeshiguang");
    RequireFullAsset("production-vmd-body");

    ResourceManager resources;
    ModelAsset& model = resources.LoadModel("r1::project", modelPath);
    Scene scene;
    Entity& first = scene.InstantiateModel(model);
    Entity& second = scene.InstantiateModel(model);
    Require(
        first.HasModelInstance() && second.HasModelInstance(),
        "Project PMX has no WISTERIA ModelInstance"
    );
    auto* firstRuntime = dynamic_cast<MmdRuntimeModel*>(
        first.GetModelInstance().TryGetRuntime()
    );
    auto* secondRuntime = dynamic_cast<MmdRuntimeModel*>(
        second.GetModelInstance().TryGetRuntime()
    );
    Require(
        firstRuntime != nullptr && secondRuntime != nullptr &&
        firstRuntime != secondRuntime,
        "Project PMX instances did not receive independent MMD runtimes"
    );
    scene.Update(0.0f);
    Require(
        first.GetPose().BoneCount() > 1U &&
        second.GetPose().BoneCount() == first.GetPose().BoneCount(),
        "Project PMX still exposes a placeholder or unstable pose"
    );
    for (BoneIndex bone = 0U; bone < first.GetPose().BoneCount(); ++bone)
    {
        const glm::mat4& matrix = first.GetPose().LocalMatrix(bone);
        for (glm::length_t column = 0; column < 4; ++column)
        {
            for (glm::length_t row = 0; row < 4; ++row)
            {
                Require(
                    std::isfinite(matrix[column][row]),
                    "Project PMX published a non-finite WISTERIA pose"
                );
            }
        }
    }
    const ModelVertexFrame firstFrame = firstRuntime->VertexFrame();
    const ModelVertexFrame secondFrame = secondRuntime->VertexFrame();
    Require(
        !firstFrame.positions.empty() &&
        firstFrame.positions.size() == firstFrame.normals.size() &&
        firstFrame.positions.data() != secondFrame.positions.data(),
        "Project PMX runtime did not publish independent render geometry"
    );
    Require(
        firstRuntime->TryGetPhysicsInstance() != nullptr &&
        secondRuntime->TryGetPhysicsInstance() != nullptr &&
        firstRuntime->TryGetPhysicsInstance() !=
            secondRuntime->TryGetPhysicsInstance(),
        "Project PMX instances unexpectedly share one physics owner"
    );

    Require(
        firstRuntime->LoadMotion(motionPath) &&
        secondRuntime->LoadMotion(motionPath),
        "Project VMD could not be attached through the runtime interface"
    );
    const double maximumFrame = firstRuntime->MotionMaxFrame();
    Require(maximumFrame > 0.0, "Project VMD has no animation frames");
    firstRuntime->SetMotionFrame(0.0);
    secondRuntime->SetMotionFrame(std::min(10.0, maximumFrame));
    firstRuntime->Update(0.0f);
    secondRuntime->Update(0.0f);
    Require(
        NearlyEqual(static_cast<float>(firstRuntime->MotionFrame()), 0.0f) &&
        secondRuntime->MotionFrame() > firstRuntime->MotionFrame(),
        "Two project PMX instances did not preserve independent timelines"
    );
}

void TestSabaImporterMorphTargets()
{
    const std::filesystem::path modelPath =
        FixturePath("extended-morph-pmx");
    RequireCoreAsset("extended-morph-pmx");
    const ImportedModelData imported = SabaMmdImporter().Import(modelPath);

    const MorphSet morphSet(imported.morphs);
    const std::optional<MorphIndex> vertexMorph =
        morphSet.FindMorph("vertex");
    const std::optional<MorphIndex> uvMorph = morphSet.FindMorph("uv");
    Require(
        vertexMorph.has_value() && uvMorph.has_value() &&
        !imported.meshes.empty(),
        "Saba importer lost the extended morph fixture"
    );

    bool foundVertexTarget = false;
    bool foundUvTarget = false;
    for (const ImportedMeshData& mesh : imported.meshes)
    {
        for (const MeshMorphTarget& target : mesh.morphTargets)
        {
            if (target.morphIndex == *vertexMorph && !target.offsets.empty())
                foundVertexTarget = true;
            if (target.morphIndex == *uvMorph && !target.uvOffsets.empty())
            {
                foundUvTarget = true;
                for (const UvMorphOffset& offset : target.uvOffsets)
                {
                    Require(
                        offset.channel < MmdUvChannelCount,
                        "Saba UV morph offset channel is out of range"
                    );
                }
            }
        }
    }
    Require(
        foundVertexTarget,
        "Saba importer dropped vertex morph offsets from mesh targets"
    );
    Require(
        foundUvTarget,
        "Saba importer dropped UV morph offsets from mesh targets"
    );
}

namespace
{
std::unique_ptr<SabaMmdRuntimeModel> CreateConfiguredRuntime(
    const std::filesystem::path& modelPath,
    const MmdPhysicsConfiguration& configuration
)
{
    auto runtime = std::make_unique<SabaMmdRuntimeModel>(modelPath);
    Require(
        runtime->SetMmdPhysicsConfiguration(configuration) ==
            TimelineStatus::Ok,
        "pre-initialize R1.3 configuration apply failed"
    );
    Require(
        runtime->Initialize(),
        "configured R1.3 runtime failed to initialize"
    );
    return runtime;
}

std::vector<MmdPhysicsTraceFrame> CaptureTraceFrames(
    SabaMmdRuntimeModel& runtime,
    MotionFrameIndex target
)
{
    std::vector<MmdPhysicsTraceFrame> frames;
    frames.reserve(static_cast<std::size_t>(target) + 1U);
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(&runtime);
    Require(stepper != nullptr, "runtime lost the deterministic stepper");
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "PrepareFrameZero failed during trace capture"
    );
    for (MotionFrameIndex frame = 0U; frame <= target; ++frame)
    {
        MmdPhysicsTraceFrame trace;
        Require(
            runtime.CapturePhysicsTraceFrame(trace),
            "CapturePhysicsTraceFrame failed"
        );
        frames.push_back(std::move(trace));
        if (frame < target)
        {
            Require(
                stepper->StepMotionFrameExact(frame + 1U, {}) ==
                    TimelineStatus::Ok,
                "StepMotionFrameExact failed during trace capture"
            );
        }
    }
    return frames;
}

bool IsSortedTrace(const MmdPhysicsTraceFrame& frame)
{
    for (std::size_t index = 1U; index < frame.bodies.size(); ++index)
    {
        if (frame.bodies[index].index < frame.bodies[index - 1U].index)
            return false;
    }
    for (std::size_t index = 1U; index < frame.bones.size(); ++index)
    {
        if (frame.bones[index].index < frame.bones[index - 1U].index)
            return false;
    }
    for (std::size_t index = 1U; index < frame.joints.size(); ++index)
    {
        if (frame.joints[index].index < frame.joints[index - 1U].index)
            return false;
    }
    for (std::size_t index = 1U; index < frame.contactPairs.size(); ++index)
    {
        const MmdPhysicsTraceContactPair& previous =
            frame.contactPairs[index - 1U];
        const MmdPhysicsTraceContactPair& current =
            frame.contactPairs[index];
        if (current.bodyA < previous.bodyA ||
            (current.bodyA == previous.bodyA &&
             current.bodyB < previous.bodyB))
        {
            return false;
        }
    }
    return true;
}

std::string TraceLines(const std::vector<MmdPhysicsTraceFrame>& frames)
{
    std::ostringstream output;
    for (const MmdPhysicsTraceFrame& frame : frames)
    {
        Require(
            wisteria::trace::WriteTraceFrameJson(frame, output),
            "WriteTraceFrameJson failed"
        );
    }
    return output.str();
}
}  // namespace

void TestR13TraceReproducibleAndSchema()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const MmdPhysicsConfiguration raw =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);

    auto first = CreateConfiguredRuntime(modelPath, raw);
    auto second = CreateConfiguredRuntime(modelPath, raw);
    constexpr MotionFrameIndex Target = 120U;
    const std::vector<MmdPhysicsTraceFrame> framesA =
        CaptureTraceFrames(*first, Target);
    const std::vector<MmdPhysicsTraceFrame> framesB =
        CaptureTraceFrames(*second, Target);
    Require(
        framesA.size() == static_cast<std::size_t>(Target) + 1U,
        "trace frame count mismatch"
    );

    const std::string linesA = TraceLines(framesA);
    const std::string linesB = TraceLines(framesB);
    Require(linesA == linesB, "two from-start traces diverged");

    std::istringstream parser(linesA);
    std::string line;
    MotionFrameIndex expectedFrame = 0U;
    std::size_t parsed = 0U;
    while (std::getline(parser, line))
    {
        if (line.empty())
            continue;
        MmdPhysicsTraceFrame frame;
        Require(
            wisteria::trace::ReadTraceFrameJson(line, frame),
            "trace JSONL line failed to parse"
        );
        Require(
            frame.traceSchemaVersion == MmdPhysicsTraceSchemaVersion,
            "trace schema version mismatch"
        );
        Require(
            frame.backendIdentity == "saba-mmd",
            "trace backend identity mismatch"
        );
        Require(
            frame.presetIdentity == "mmd-raw-v1",
            "trace preset identity mismatch"
        );
        Require(
            frame.effectiveConfigurationHash.size() == 16U,
            "effective configuration hash is not 16 hex chars"
        );
        Require(
            frame.executionProfile == "deterministic-cold-step-v1",
            "trace execution profile mismatch"
        );
        Require(frame.hasMotion == false, "CORE fixture unexpectedly has VMD");
        Require(frame.motionHash == "0000000000000000", "empty motion hash mismatch");
        Require(frame.frame == expectedFrame, "trace frame order mismatch");
        Require(
            frame.physicsTick == expectedFrame * 4U,
            "trace physicsTick mismatch"
        );
        Require(frame.canonical, "trace frame is not canonical");
        Require(
            frame.poseHash.valid &&
                frame.physicsHash.valid &&
                frame.vertexHash.valid,
            "trace state hashes are not valid"
        );
        Require(
            frame.poseHash.hex.size() == 16U &&
                frame.physicsHash.hex.size() == 16U &&
                frame.vertexHash.hex.size() == 16U,
            "trace state hash length mismatch"
        );
        Require(!frame.bodies.empty(), "trace has no rigid bodies");
        Require(!frame.bones.empty(), "trace has no bones");
        for (const MmdPhysicsTraceJoint& joint : frame.joints)
        {
            Require(
                joint.linearViolation >= 0.0f &&
                    joint.linearViolation <=
                        joint.rawLinearError + 1.0e-3f,
                "joint linear violation exceeds its raw error norm"
            );
            Require(
                joint.angularViolationDeg >= 0.0f &&
                    joint.angularViolationDeg <=
                        joint.rawAngularErrorDeg + 1.0e-3f,
                "joint angular violation exceeds its raw error norm"
            );
        }
        Require(
            IsSortedTrace(frame),
            "trace arrays are not stably sorted"
        );
        bool motionStateRead = false;
        for (const MmdPhysicsTraceBody& body : frame.bodies)
            motionStateRead = motionStateRead || body.motionStateAvailable;
        Require(motionStateRead, "no body exposed a motion-state transform");
        ++expectedFrame;
        ++parsed;
    }
    Require(
        parsed == static_cast<std::size_t>(Target) + 1U,
        "trace JSONL parsed line count mismatch"
    );
}

void TestR13TraceDiffLocatesInjection()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateConfiguredRuntime(
        modelPath,
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw)
    );
    const std::vector<MmdPhysicsTraceFrame> frames =
        CaptureTraceFrames(*runtime, 150U);
    const std::string linesA = TraceLines(frames);

    std::istringstream left(linesA);
    std::istringstream right(linesA);
    const wisteria::trace::TraceDiffResult identical =
        wisteria::trace::DiffTraceStreams(left, right);
    Require(
        identical.identical,
        "identical traces were reported as divergent"
    );

    std::istringstream source(linesA);
    std::ostringstream injected;
    std::string line;
    while (std::getline(source, line))
    {
        if (line.empty())
            continue;
        MmdPhysicsTraceFrame frame;
        Require(
            wisteria::trace::ReadTraceFrameJson(line, frame),
            "trace line failed to parse during injection"
        );
        if (frame.frame == 150U && !frame.bodies.empty())
        {
            frame.bodies[0U].worldTransform.position.x += 0.001f;
            Require(
                wisteria::trace::WriteTraceFrameJson(frame, injected),
                "injected trace line failed to write"
            );
        }
        else
        {
            injected << line << '\n';
        }
    }

    std::istringstream leftInjected(linesA);
    std::istringstream rightInjected(injected.str());
    const wisteria::trace::TraceDiffResult result =
        wisteria::trace::DiffTraceStreams(leftInjected, rightInjected);
    Require(!result.identical, "injected trace was reported identical");
    Require(result.firstFound, "diff tool did not locate a first divergence");
    Require(
        result.firstFrame == 150U && result.firstBody == 0U,
        "diff tool located the wrong first divergence"
    );
    Require(
        std::abs(result.firstPositionError - 0.001f) < 1.0e-4f,
        "diff tool reported the wrong position error"
    );
    const std::string formatted = wisteria::trace::FormatTraceDiff(result);
    Require(
        formatted.find("First divergence: frame=150 body=0") !=
            std::string::npos,
        "formatted diff misses the first divergence"
    );
}

void TestR13TraceDiffExtendedLocators()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateConfiguredRuntime(
        modelPath,
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw)
    );
    const std::vector<MmdPhysicsTraceFrame> frames =
        CaptureTraceFrames(*runtime, 250U);
    const std::string lines = TraceLines(frames);

    const auto rewrite = [&lines](const auto& mutate)
    {
        std::istringstream source(lines);
        std::ostringstream output;
        std::string line;
        while (std::getline(source, line))
        {
            if (line.empty())
                continue;
            MmdPhysicsTraceFrame frame;
            Require(
                wisteria::trace::ReadTraceFrameJson(line, frame),
                "trace line failed to parse during extended diff rewrite"
            );
            mutate(frame);
            Require(
                wisteria::trace::WriteTraceFrameJson(frame, output),
                "trace line failed to write during extended diff rewrite"
            );
        }
        return output.str();
    };
    const auto diffFrom = [&lines](const std::string& modified)
    {
        std::istringstream left(lines);
        std::istringstream right(modified);
        return wisteria::trace::DiffTraceStreams(left, right);
    };

    // Contact-topology divergence: an extra pair at frame 100.
    const std::string topologyLines = rewrite(
        [](MmdPhysicsTraceFrame& frame)
        {
            if (frame.frame != 100U)
                return;
            MmdPhysicsTraceContactPair pair;
            pair.bodyA = 250U;
            pair.bodyB = 251U;
            frame.contactPairs.push_back(pair);
            std::sort(
                frame.contactPairs.begin(),
                frame.contactPairs.end(),
                [](const MmdPhysicsTraceContactPair& left,
                   const MmdPhysicsTraceContactPair& right)
                {
                    if (left.bodyA != right.bodyA)
                        return left.bodyA < right.bodyA;
                    return left.bodyB < right.bodyB;
                }
            );
        }
    );
    const wisteria::trace::TraceDiffResult topologyResult =
        diffFrom(topologyLines);
    Require(
        !topologyResult.identical && topologyResult.contactTopologyFound,
        "contact-topology divergence was not located"
    );
    Require(
        topologyResult.contactTopologyFrame == 100U &&
            topologyResult.contactTopologyBodyA == 250U &&
            topologyResult.contactTopologyBodyB == 251U,
        "contact-topology locator points at the wrong pair"
    );
    Require(
        wisteria::trace::FormatTraceDiff(topologyResult).find(
            "First contact-topology divergence: frame=100 pair=(250,251)"
        ) != std::string::npos,
        "formatted diff misses the contact-topology divergence"
    );

    // Motion-state divergence: body 0 motion-state position shifts at 120.
    const std::string motionLines = rewrite(
        [](MmdPhysicsTraceFrame& frame)
        {
            if (frame.frame != 120U || frame.bodies.empty())
                return;
            frame.bodies[0].motionStateTransform.position.x += 0.001f;
        }
    );
    const wisteria::trace::TraceDiffResult motionResult =
        diffFrom(motionLines);
    Require(
        !motionResult.identical && motionResult.motionStateFound,
        "motion-state divergence was not located"
    );
    Require(
        motionResult.motionStateFrame == 120U &&
            motionResult.motionStateBody == 0U,
        "motion-state locator points at the wrong body"
    );
    Require(
        std::abs(motionResult.motionStatePositionError - 0.001f) < 1.0e-4f,
        "motion-state position error mismatch"
    );

    // Bone divergence: bone 0 global translation shifts at 200.
    const std::string boneLines = rewrite(
        [](MmdPhysicsTraceFrame& frame)
        {
            if (frame.frame != 200U || frame.bones.empty())
                return;
            frame.bones[0].globalMatrix[12] += 0.001f;
        }
    );
    const wisteria::trace::TraceDiffResult boneResult =
        diffFrom(boneLines);
    Require(
        !boneResult.identical && boneResult.boneFound,
        "bone divergence was not located"
    );
    Require(
        boneResult.boneFrame == 200U && boneResult.boneIndex == 0U,
        "bone locator points at the wrong bone"
    );
    Require(
        std::abs(boneResult.boneMaxMatrixDelta - 0.001f) < 1.0e-4f,
        "bone matrix delta mismatch"
    );

    // Bone presence must be symmetric: removing a bone from B is located,
    // and adding a bone only to B is located too.
    const std::string boneRemovedLines = rewrite(
        [](MmdPhysicsTraceFrame& frame)
        {
            if (frame.frame != 150U || frame.bones.empty())
                return;
            frame.bones.erase(frame.bones.begin());
        }
    );
    const wisteria::trace::TraceDiffResult boneRemovedResult =
        diffFrom(boneRemovedLines);
    Require(
        !boneRemovedResult.identical && boneRemovedResult.boneFound &&
            boneRemovedResult.boneFrame == 150U &&
            boneRemovedResult.boneIndex == 0U,
        "removed bone was not located"
    );
    Require(
        boneRemovedResult.boneMaxMatrixDelta > 1.0e6f,
        "removed bone delta is not sentinel-sized"
    );

    const std::string boneAddedLines = rewrite(
        [](MmdPhysicsTraceFrame& frame)
        {
            if (frame.frame != 200U)
                return;
            MmdPhysicsTraceBone extra;
            extra.index = 999U;
            frame.bones.push_back(extra);
        }
    );
    const wisteria::trace::TraceDiffResult boneAddedResult =
        diffFrom(boneAddedLines);
    Require(
        !boneAddedResult.identical && boneAddedResult.boneFound &&
            boneAddedResult.boneFrame == 200U &&
            boneAddedResult.boneIndex == 999U,
        "extra bone present only in B was not located"
    );

    // Joint presence must be symmetric as well.
    const std::string jointRemovedLines = rewrite(
        [](MmdPhysicsTraceFrame& frame)
        {
            if (frame.frame != 100U || frame.joints.empty())
                return;
            frame.joints.erase(frame.joints.begin());
        }
    );
    const wisteria::trace::TraceDiffResult jointRemovedResult =
        diffFrom(jointRemovedLines);
    Require(
        !jointRemovedResult.identical &&
            jointRemovedResult.jointPresenceFound &&
            jointRemovedResult.jointPresenceFrame == 100U &&
            jointRemovedResult.jointPresenceIndex == 0U,
        "removed joint was not located"
    );

    const std::string jointAddedLines = rewrite(
        [](MmdPhysicsTraceFrame& frame)
        {
            if (frame.frame != 220U)
                return;
            MmdPhysicsTraceJoint extra;
            extra.index = 999U;
            frame.joints.push_back(extra);
        }
    );
    const wisteria::trace::TraceDiffResult jointAddedResult =
        diffFrom(jointAddedLines);
    Require(
        !jointAddedResult.identical &&
            jointAddedResult.jointPresenceFound &&
            jointAddedResult.jointPresenceFrame == 220U &&
            jointAddedResult.jointPresenceIndex == 999U,
        "extra joint present only in B was not located"
    );
}

void TestR13ThreePresetsThreeHundredFrames()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    constexpr MotionFrameIndex Target = 300U;

    const MmdPhysicsPreset presets[] = {
        MmdPhysicsPreset::MmdRaw,
        MmdPhysicsPreset::MmdCommunity,
        MmdPhysicsPreset::WisteriaAdaptive
    };
    std::vector<MmdPhysicsTraceFrame> frames;
    std::vector<std::string> identities;
    std::vector<std::string> hashes;
    for (const MmdPhysicsPreset preset : presets)
    {
        auto runtime = CreateConfiguredRuntime(
            modelPath,
            BuildPresetConfiguration(preset)
        );
        const std::vector<MmdPhysicsTraceFrame> trace =
            CaptureTraceFrames(*runtime, Target);
        frames.push_back(trace.back());
        identities.push_back(trace.back().presetIdentity);
        hashes.push_back(trace.back().effectiveConfigurationHash);
    }

    Require(
        identities[0] == "mmd-raw-v1" &&
            identities[1] == "mmd-community-v1" &&
            identities[2] == "wisteria-adaptive-v1",
        "preset trace identities mismatch"
    );
    // Phase 0A: behaviour-identical presets share one effective hash and one
    // physics result.
    Require(
        hashes[0] == hashes[1] && hashes[1] == hashes[2],
        "behaviour-identical presets produced different effective hashes"
    );
    Require(
        frames[0].physicsHash.hex == frames[1].physicsHash.hex &&
            frames[1].physicsHash.hex == frames[2].physicsHash.hex,
        "behaviour-identical presets produced different physics results"
    );
    for (const MmdPhysicsTraceFrame& frame : frames)
    {
        Require(frame.canonical, "preset trace frame is not canonical");
        Require(
            frame.frame == Target &&
                frame.physicsTick == Target * 4U,
            "preset trace frame timeline mismatch"
        );
    }
}

void TestR13LinkedBodyAbSmoke()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const MmdPhysicsConfiguration raw =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);

    MmdPhysicsDiagnosticOverrides overrides;
    overrides.linkedBodyCollision =
        MmdLinkedBodyCollisionMode::DisableConstraintLinkedPairs;
    MmdPhysicsConfiguration linked;
    Require(
        DeriveDiagnosticConfiguration(raw, overrides, linked) ==
            TimelineStatus::Ok,
        "deriving DisableConstraintLinkedPairs failed"
    );

    auto first = CreateConfiguredRuntime(modelPath, linked);
    auto second = CreateConfiguredRuntime(modelPath, linked);
    const std::vector<MmdPhysicsTraceFrame> traceA =
        CaptureTraceFrames(*first, 300U);
    const std::vector<MmdPhysicsTraceFrame> traceB =
        CaptureTraceFrames(*second, 300U);
    Require(
        traceA.back().canonical && traceB.back().canonical,
        "linked-body A/B replay did not reach a canonical boundary"
    );
    Require(
        traceA.back().physicsHash.hex == traceB.back().physicsHash.hex,
        "linked-body mode replay is not deterministic"
    );
    Require(
        traceA.back().effectiveConfigurationHash !=
            FormatTraceHex(
                ComputeEffectiveConfigurationFingerprint(raw)
            ),
        "linked-body override did not change the effective hash"
    );

    // Behaviour evidence on the CORE fixture:
    // - ground contacts must be identical and non-zero under both modes
    //   (DisableConstraintLinkedPairs never affects the ground);
    // - pmx_physics has 6 constraint-linked pairs, but none of them overlap,
    //   so linked contacts are zero in both modes. The "linked collision is
    //   really disabled" semantic is covered by the Bullet-level unit test
    //   (addConstraint(constraint, true)) which is the exact mechanism the
    //   Saba adapter uses.
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(imported.mmdPhysics.has_value(), "fixture lost physics data");
    std::vector<std::pair<std::uint32_t, std::uint32_t>> linkedPairs;
    for (const MmdJointDefinition& joint :
         imported.mmdPhysics->Joints())
    {
        if (joint.bodyA != InvalidRigidBodyIndex &&
            joint.bodyB != InvalidRigidBodyIndex &&
            joint.bodyA != joint.bodyB)
        {
            linkedPairs.push_back({
                std::min(joint.bodyA, joint.bodyB),
                std::max(joint.bodyA, joint.bodyB)
            });
        }
    }
    Require(
        !linkedPairs.empty(),
        "pmx-physics fixture unexpectedly has no linked pairs"
    );
    auto rawRuntime = CreateConfiguredRuntime(modelPath, raw);
    const std::vector<MmdPhysicsTraceFrame> rawTrace =
        CaptureTraceFrames(*rawRuntime, 300U);
    auto countContacts = [&linkedPairs](
        const std::vector<MmdPhysicsTraceFrame>& trace)
    {
        int linkedContacts = 0;
        int groundContacts = 0;
        for (const MmdPhysicsTraceFrame& frame : trace)
        {
            for (const MmdPhysicsTraceContactPair& pair :
                 frame.contactPairs)
            {
                if (pair.bodyA == MmdPhysicsTraceGroundBodyIndex ||
                    pair.bodyB == MmdPhysicsTraceGroundBodyIndex)
                {
                    ++groundContacts;
                    continue;
                }
                for (const auto& link : linkedPairs)
                {
                    if ((pair.bodyA == link.first &&
                         pair.bodyB == link.second) ||
                        (pair.bodyA == link.second &&
                         pair.bodyB == link.first))
                    {
                        ++linkedContacts;
                        break;
                    }
                }
            }
        }
        return std::pair<int, int>{linkedContacts, groundContacts};
    };
    const auto rawCounts = countContacts(rawTrace);
    const auto disableCounts = countContacts(traceA);
    Require(
        rawCounts.second > 0 && disableCounts.second > 0 &&
            rawCounts.second == disableCounts.second,
        "DisableConstraintLinkedPairs changed ground contacts"
    );
    Require(
        rawCounts.first == 0 && disableCounts.first == 0,
        "CORE fixture unexpectedly produced linked contacts"
    );
}

void TestR13Mode2AbSmoke()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const MmdPhysicsConfiguration raw =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);

    MmdPhysicsDiagnosticOverrides overrides;
    overrides.mode2 = MmdMode2WritebackMode::FullTransformDiagnostic;
    MmdPhysicsConfiguration diagnostic;
    Require(
        DeriveDiagnosticConfiguration(raw, overrides, diagnostic) ==
            TimelineStatus::Ok,
        "deriving FullTransformDiagnostic failed"
    );

    // CORE smoke: both modes replay deterministically and the effective
    // configuration hash changes. The pmx_physics root bone is marked
    // deform-after-physics, so the writeback cannot be observed through the
    // engine pose on this fixture; the pose-level assertion runs under
    // FULL_ASSETS where Mode 2 bodies drive visible skeleton translation.
    auto baseline = CreateConfiguredRuntime(modelPath, raw);
    auto full = CreateConfiguredRuntime(modelPath, diagnostic);
    const std::vector<MmdPhysicsTraceFrame> baselineTrace =
        CaptureTraceFrames(*baseline, 300U);
    const std::vector<MmdPhysicsTraceFrame> fullTrace =
        CaptureTraceFrames(*full, 300U);
    Require(
        fullTrace.back().canonical,
        "Mode 2 diagnostic replay did not reach a canonical boundary"
    );
    Require(
        baselineTrace.back().physicsHash.hex ==
            fullTrace.back().physicsHash.hex,
        "Mode 2 writeback mode changed the simulated physics state"
    );
    Require(
        fullTrace.back().effectiveConfigurationHash !=
            FormatTraceHex(
                ComputeEffectiveConfigurationFingerprint(raw)
            ),
        "Mode 2 override did not change the effective hash"
    );
}

void TestR13Mode2WritebackPose()
{
    RequireFullAssetsTier();
    const MmdPhysicsConfiguration raw =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    MmdPhysicsDiagnosticOverrides overrides;
    overrides.mode2 = MmdMode2WritebackMode::FullTransformDiagnostic;
    MmdPhysicsConfiguration diagnostic;
    Require(
        DeriveDiagnosticConfiguration(raw, overrides, diagnostic) ==
            TimelineStatus::Ok,
        "deriving FullTransformDiagnostic failed"
    );
    const std::filesystem::path productionPath =
        FixturePath("production-pmx-yeshiguang");
    RequireFullAsset("production-pmx-yeshiguang");
    auto productionBaseline = CreateConfiguredRuntime(productionPath, raw);
    auto productionFull = CreateConfiguredRuntime(productionPath, diagnostic);
    const std::vector<MmdPhysicsTraceFrame> productionBaselineTrace =
        CaptureTraceFrames(*productionBaseline, 300U);
    const std::vector<MmdPhysicsTraceFrame> productionFullTrace =
        CaptureTraceFrames(*productionFull, 300U);
    float maxBoneDiff = 0.0f;
    const std::size_t sharedBones = std::min(
        productionBaselineTrace.back().bones.size(),
        productionFullTrace.back().bones.size()
    );
    for (std::size_t index = 0U; index < sharedBones; ++index)
    {
        for (std::size_t element = 0U; element < 16U; ++element)
        {
            maxBoneDiff = std::max(
                maxBoneDiff,
                std::abs(
                    productionBaselineTrace.back()
                        .bones[index]
                        .globalMatrix[element] -
                    productionFullTrace.back()
                        .bones[index]
                        .globalMatrix[element]
                )
            );
        }
    }
    Require(
        maxBoneDiff > 1.0e-4f,
        "FullTransformDiagnostic did not change the production skeleton pose"
    );
}

void TestR13UnitAuditOnFixture()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    const ImportedModelData imported = ModelImporter().Import(modelPath);
    Require(
        imported.mmdPhysics.has_value() && imported.skeleton.has_value(),
        "pmx-physics fixture import lost physics or skeleton data"
    );

    const MmdPhysicsAuditResult audit = RunMmdPhysicsAudit(
        *imported.mmdPhysics,
        imported.skeleton->Bones(),
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw)
    );
    Require(
        audit.rigidBodySize.available &&
            audit.rigidBodySize.count ==
                imported.mmdPhysics->RigidBodyCount() &&
            audit.jointLinearRange.available &&
            audit.jointLinearRange.count ==
                imported.mmdPhysics->JointCount(),
        "fixture audit lost rigid-body or joint ranges"
    );
    Require(
        std::isfinite(audit.gravityMagnitude) &&
            std::isfinite(audit.fixedTimeStep) &&
            std::isfinite(audit.rigidBodySize.median) &&
            std::isfinite(audit.jointLinearRange.median) &&
            std::isfinite(audit.jointAngularRangeDeg.median),
        "fixture audit produced non-finite values"
    );
    Require(
        audit.gravityAvailable &&
            NearlyEqual(audit.gravityMagnitude, 98.0f) &&
            NearlyEqual(audit.fixedTimeStep, 1.0f / 120.0f),
        "fixture audit gravity or timestep mismatch"
    );
    // No model bounds supplied: height-derived metrics must stay unavailable
    // instead of dividing by zero.
    Require(
        !audit.modelHeightAvailable &&
            !audit.gravityPerModelHeightAvailable &&
            !audit.shapeMarginRatioAvailable,
        "fixture audit reported ratios without required inputs"
    );
}

void TestR13MmdPhysicsConfigurationRuntime()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");

    // Phase 0A: apply the authoritative configuration before Initialize().
    SabaMmdRuntimeModel runtime(modelPath);
    const MmdPhysicsConfiguration raw =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw);
    Require(
        runtime.SetMmdPhysicsConfiguration(raw) == TimelineStatus::Ok,
        "pre-initialize MMD_RAW configuration apply failed"
    );
    MmdPhysicsConfiguration foreign = raw;
    foreign.identity.backend = "other-backend";
    Require(
        runtime.SetMmdPhysicsConfiguration(foreign) ==
            TimelineStatus::InvalidState,
        "foreign backend identity was accepted"
    );
    MmdPhysicsConfiguration mutated = raw;
    mutated.runtime.gravity.y = -9.8f;
    Require(
        runtime.SetMmdPhysicsConfiguration(mutated) ==
            TimelineStatus::InvalidState,
        "mutated MMD_RAW configuration was accepted"
    );
    MmdPhysicsConfiguration readBack;
    Require(
        runtime.GetMmdPhysicsConfiguration(readBack),
        "GetMmdPhysicsConfiguration failed"
    );
    Require(
        FormatConfigurationIdentity(readBack) == "mmd-raw-v1",
        "read-back configuration identity mismatch"
    );
    Require(
        runtime.Initialize(),
        "Saba runtime failed to initialize with R1.3 configuration"
    );

    // Metadata-only switch must not touch the Bullet world: every captured
    // state must stay bit-identical. Establish a canonical boundary first.
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(&runtime);
    Require(stepper != nullptr, "runtime lost the deterministic stepper");
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "PrepareFrameZero failed before the label switch"
    );
    MmdPhysicsTraceFrame before;
    Require(
        runtime.CapturePhysicsTraceFrame(before),
        "trace capture before label switch failed"
    );

    // Label-only switch (behaviour-identical) after Initialize stays valid.
    const MmdPhysicsConfiguration community =
        BuildPresetConfiguration(MmdPhysicsPreset::MmdCommunity);
    Require(
        runtime.SetMmdPhysicsConfiguration(community) == TimelineStatus::Ok,
        "label-only profile switch was rejected"
    );
    Require(
        runtime.GetMmdPhysicsConfiguration(readBack),
        "GetMmdPhysicsConfiguration failed after label switch"
    );
    Require(
        FormatConfigurationIdentity(readBack) == "mmd-community-v1",
        "label switch identity mismatch"
    );

    MmdPhysicsTraceFrame after;
    Require(
        runtime.CapturePhysicsTraceFrame(after),
        "trace capture after label switch failed"
    );
    Require(
        before.poseHash.hex == after.poseHash.hex &&
            before.physicsHash.hex == after.physicsHash.hex &&
            before.vertexHash.hex == after.vertexHash.hex,
        "label switch changed a state hash"
    );
    Require(
        before.bodies.size() == after.bodies.size(),
        "label switch changed the rigid-body set"
    );
    for (std::size_t index = 0U; index < before.bodies.size(); ++index)
    {
        const MmdPhysicsTraceBody& left = before.bodies[index];
        const MmdPhysicsTraceBody& right = after.bodies[index];
        Require(
            left.worldTransform.position ==
                    right.worldTransform.position &&
                left.worldTransform.rotationBasis ==
                    right.worldTransform.rotationBasis &&
                left.interpolationWorldTransform.position ==
                    right.interpolationWorldTransform.position &&
                left.interpolationWorldTransform.rotationBasis ==
                    right.interpolationWorldTransform.rotationBasis &&
                left.motionStateTransform.position ==
                    right.motionStateTransform.position &&
                left.motionStateTransform.rotationBasis ==
                    right.motionStateTransform.rotationBasis &&
                left.motionStateAvailable == right.motionStateAvailable &&
                left.linearVelocity == right.linearVelocity &&
                left.angularVelocity == right.angularVelocity,
            "label switch changed a rigid-body transform"
        );
    }
    Require(
        before.bones.size() == after.bones.size(),
        "label switch changed the bone set"
    );
    for (std::size_t index = 0U; index < before.bones.size(); ++index)
    {
        Require(
            before.bones[index].localMatrix ==
                    after.bones[index].localMatrix &&
                before.bones[index].globalMatrix ==
                    after.bones[index].globalMatrix,
            "label switch changed a bone matrix"
        );
    }

    // Phase 0A forbids switching effective behaviour of a live runtime; the
    // candidate must be a valid custom config so rejection comes from the
    // live-switch rule, not from configuration validation.
    MmdPhysicsDiagnosticOverrides liveOverrides;
    liveOverrides.linkedBodyCollision =
        MmdLinkedBodyCollisionMode::DisableConstraintLinkedPairs;
    MmdPhysicsConfiguration liveSwitch;
    Require(
        DeriveDiagnosticConfiguration(
            community,
            liveOverrides,
            liveSwitch
        ) == TimelineStatus::Ok,
        "deriving the live-switch candidate failed"
    );
    Require(
        runtime.SetMmdPhysicsConfiguration(liveSwitch) ==
            TimelineStatus::UnsupportedReplayProfile,
        "live behaviour switch was accepted"
    );

    // Anonymous configurations are rejected at the runtime entry too.
    MmdPhysicsConfiguration anonymous;
    anonymous.identity.backend.clear();
    Require(
        runtime.SetMmdPhysicsConfiguration(anonymous) ==
            TimelineStatus::InvalidState,
        "anonymous configuration was accepted by the runtime"
    );

    // Legacy constructor overrides must not claim a direct preset identity.
    SabaPhysicsSettings legacySettings;
    legacySettings.fixedTimeStep = 1.0f / 60.0f;
    legacySettings.maxSubSteps = 4;
    legacySettings.gravity = glm::vec3(0.0f, -99.0f, 0.0f);
    SabaMmdRuntimeModel legacy(modelPath, {}, legacySettings);
    MmdPhysicsConfiguration legacyConfig;
    Require(
        legacy.GetMmdPhysicsConfiguration(legacyConfig),
        "legacy runtime config read failed"
    );
    Require(
        FormatConfigurationIdentity(legacyConfig) ==
            "custom-from-mmd-raw-v1",
        "legacy custom settings kept the direct MMD_RAW identity"
    );
    // Authority invariant: every configuration the runtime exposes must pass
    // its own validator, and the pre-init roundtrip must be accepted.
    Require(
        ValidateConfiguration(legacyConfig),
        "legacy custom configuration fails its own validator"
    );
    Require(
        legacy.SetMmdPhysicsConfiguration(legacyConfig) ==
            TimelineStatus::Ok,
        "pre-init custom configuration roundtrip was rejected"
    );

    // Low-level settings override after Initialize must produce a valid
    // custom configuration too.
    SabaPhysicsSettings overridden = SabaPhysicsSettings{};
    overridden.gravity.y = -99.0f;
    runtime.SetMmdPhysicsSettings(overridden);
    MmdPhysicsConfiguration overriddenConfig;
    Require(
        runtime.GetMmdPhysicsConfiguration(overriddenConfig),
        "overridden runtime config read failed"
    );
    Require(
        FormatConfigurationIdentity(overriddenConfig) ==
            "custom-from-mmd-community-v1",
        "SetMmdPhysicsSettings override kept a direct preset identity"
    );
    Require(
        ValidateConfiguration(overriddenConfig),
        "SetMmdPhysicsSettings produced a configuration its own validator "
        "rejects"
    );

    // Guard Fix: invalid low-level input must be a no-op for the
    // authoritative configuration and the Get invariant must hold.
    MmdPhysicsConfiguration validBeforeInvalid;
    Require(
        runtime.GetMmdPhysicsConfiguration(validBeforeInvalid),
        "baseline config read failed before invalid settings"
    );
    SabaPhysicsSettings zeroStep = SabaPhysicsSettings{};
    zeroStep.fixedTimeStep = 0.0f;
    runtime.SetMmdPhysicsSettings(zeroStep);
    MmdPhysicsConfiguration afterZeroStep;
    Require(
        runtime.GetMmdPhysicsConfiguration(afterZeroStep),
        "Get failed after rejected zero timestep settings"
    );
    Require(
        ValidateConfiguration(afterZeroStep),
        "rejected zero timestep settings corrupted the configuration"
    );
    Require(
        afterZeroStep.runtime.fixedTimeStep ==
                validBeforeInvalid.runtime.fixedTimeStep &&
            afterZeroStep.runtime.gravity ==
                validBeforeInvalid.runtime.gravity,
        "rejected zero timestep settings modified the configuration"
    );

    SabaPhysicsSettings nanGravity = SabaPhysicsSettings{};
    nanGravity.gravity.y = std::numeric_limits<float>::quiet_NaN();
    runtime.SetMmdPhysicsSettings(nanGravity);
    MmdPhysicsConfiguration afterNanGravity;
    Require(
        runtime.GetMmdPhysicsConfiguration(afterNanGravity),
        "Get failed after rejected NaN gravity settings"
    );
    Require(
        ValidateConfiguration(afterNanGravity),
        "rejected NaN gravity settings corrupted the configuration"
    );
    Require(
        afterNanGravity.runtime.gravity ==
            validBeforeInvalid.runtime.gravity,
        "rejected NaN gravity settings modified the configuration"
    );

    // Guard Fix: a legacy constructor with invalid settings must fail
    // Initialize and must not expose an invalid configuration.
    SabaPhysicsSettings invalidCtorSettings;
    invalidCtorSettings.fixedTimeStep = 0.0f;
    SabaMmdRuntimeModel invalidRuntime(modelPath, {}, invalidCtorSettings);
    Require(
        !invalidRuntime.Initialize(),
        "Initialize accepted an invalid legacy configuration"
    );
    MmdPhysicsConfiguration neverExposed;
    neverExposed.runtime.maxSubSteps = 42;
    Require(
        !invalidRuntime.GetMmdPhysicsConfiguration(neverExposed),
        "Get exposed an invalid legacy configuration"
    );
    Require(
        neverExposed.runtime.maxSubSteps == 42,
        "failed Get modified the caller's output"
    );
}

void TestR13TraceCanonicalGate()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    SabaMmdRuntimeModel runtime(modelPath);

    MmdPhysicsTraceFrame untouched;
    untouched.frame = 999U;
    untouched.presetIdentity = "sentinel";
    Require(
        !runtime.CapturePhysicsTraceFrame(untouched),
        "trace capture succeeded before Initialize"
    );
    Require(
        untouched.frame == 999U &&
            untouched.presetIdentity == "sentinel",
        "failed trace capture modified the caller's output"
    );

    Require(runtime.Initialize(), "Saba runtime failed to initialize");
    Require(
        !runtime.CapturePhysicsTraceFrame(untouched),
        "trace capture succeeded outside a canonical boundary"
    );

    runtime.Update(1.0f / 30.0f);
    Require(
        !runtime.CapturePhysicsTraceFrame(untouched),
        "trace capture succeeded after an ordinary Update"
    );

    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(&runtime);
    Require(stepper != nullptr, "runtime lost the deterministic stepper");
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "PrepareFrameZero failed"
    );
    MmdPhysicsTraceFrame canonical;
    Require(
        runtime.CapturePhysicsTraceFrame(canonical),
        "trace capture failed at the canonical frame 0"
    );
    Require(
        canonical.canonical && canonical.frame == 0U,
        "canonical frame 0 trace is not canonical"
    );
    Require(
        stepper->StepMotionFrameExact(1U, {}) == TimelineStatus::Ok,
        "StepMotionFrameExact failed"
    );
    Require(
        runtime.CapturePhysicsTraceFrame(canonical),
        "trace capture failed after StepMotionFrameExact"
    );
    Require(
        canonical.canonical && canonical.frame == 1U &&
            canonical.physicsTick == 4U,
        "post-step trace is not the canonical frame 1"
    );

    // PrepareFrameZero -> ordinary Update must invalidate every
    // deterministic surface: trace, checkpoint and direct stepping.
    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "PrepareFrameZero failed before the invalidation check"
    );
    runtime.Update(1.0f / 30.0f);
    Require(
        !runtime.CapturePhysicsTraceFrame(canonical),
        "trace accepted after Update following a canonical frame"
    );
    FrameCheckpoint checkpoint;
    Require(
        runtime.CreateCheckpoint(checkpoint) != TimelineStatus::Ok,
        "checkpoint accepted after Update following a canonical frame"
    );
    Require(
        stepper->StepMotionFrameExact(2U, {}) != TimelineStatus::Ok,
        "direct step accepted after Update following a canonical frame"
    );

    // Every non-deterministic mutator must invalidate the canonical
    // boundary.
    const auto expectInvalidated = [&](
        const char* label,
        const auto& mutate)
    {
        Require(
            stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
            "PrepareFrameZero failed before a mutator check"
        );
        MmdPhysicsTraceFrame trace;
        Require(
            runtime.CapturePhysicsTraceFrame(trace),
            "canonical capture failed before a mutator check"
        );
        mutate();
        Require(
            !runtime.CapturePhysicsTraceFrame(trace),
            std::string(label) + " did not invalidate the canonical boundary"
        );
    };
    expectInvalidated("SetMotionFrame", [&]() { runtime.SetMotionFrame(3.0); });
    expectInvalidated(
        "SetMmdIkEnabled",
        [&]() { runtime.SetMmdIkEnabled(0U, false); }
    );
    expectInvalidated(
        "SetPhysicsSettings",
        [&]() { runtime.SetPhysicsSettings(SabaPhysicsSettings{}); }
    );
    expectInvalidated(
        "ResetMmdPhysics",
        [&]() { runtime.ResetMmdPhysics(); }
    );
    expectInvalidated("PauseMotion", [&]() { runtime.PauseMotion(); });
    expectInvalidated("ResumeMotion", [&]() { runtime.ResumeMotion(); });
    expectInvalidated(
        "SetMotionLooping",
        [&]() { runtime.SetMotionLooping(false); }
    );
    expectInvalidated(
        "RestartMotion",
        [&]() { runtime.RestartMotion(false); }
    );
    expectInvalidated("ClearMotion", [&]() { runtime.ClearMotion(); });
}

void TestR14FrameDomainGuard()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");
    auto runtime = CreateConfiguredRuntime(
        modelPath,
        BuildPresetConfiguration(MmdPhysicsPreset::MmdRaw)
    );
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(runtime.get());
    Require(
        mmd != nullptr && stepper != nullptr,
        "R1.4 frame-domain guard test lost runtime surfaces"
    );

    const MotionFrameIndex maxFrame =
        std::numeric_limits<MotionFrameIndex>::max();
    Require(
        mmd->EvaluateTick(maxFrame, SeekPolicy::ReplayFromStart, {}) ==
            TimelineStatus::InvalidState,
        "ReplayFromStart accepted UINT64_MAX"
    );
    Require(
        mmd->EvaluateTick(
            maxFrame / 4U + 1U,
            SeekPolicy::ReplayFromStart,
            {}
        ) == TimelineStatus::InvalidState,
        "ReplayFromStart accepted an overflowing frame"
    );
    Require(
        mmd->EvaluateTick(maxFrame, SeekPolicy::ResetAtTarget, {}) ==
            TimelineStatus::InvalidState,
        "ResetAtTarget accepted UINT64_MAX"
    );

    Require(
        stepper->PrepareFrameZero({}) == TimelineStatus::Ok,
        "PrepareFrameZero failed in the frame-domain guard test"
    );
    Require(
        stepper->StepMotionFrameExact(maxFrame, {}) ==
            TimelineStatus::InvalidState,
        "StepMotionFrameExact accepted UINT64_MAX"
    );
    Require(
        stepper->StepMotionFrameExact(maxFrame / 4U + 1U, {}) ==
            TimelineStatus::InvalidState,
        "StepMotionFrameExact accepted an overflowing frame"
    );

    // Guard rejections must not corrupt the deterministic stepping machine.
    Require(
        stepper->StepMotionFrameExact(1U, {}) == TimelineStatus::Ok,
        "valid step failed after guard rejections"
    );
    Require(
        stepper->StepMotionFrameExact(2U, {}) == TimelineStatus::Ok,
        "second valid step failed after guard rejections"
    );
    MmdPhysicsTraceFrame trace;
    Require(
        runtime->CapturePhysicsTraceFrame(trace),
        "trace capture failed after the frame-domain guard test"
    );
    Require(
        trace.frame == 2U && trace.physicsTick == 8U,
        "physicsTick mismatch after the frame-domain guard test"
    );
}

void TestR14RuntimeCreationOptions()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");

    ModelAsset asset("pmx-physics");
    ModelSourceDescriptor descriptor;
    descriptor.sourcePath = modelPath;
    descriptor.backend = ModelBackendKind::SabaMmd;
    asset.SetSourceDescriptor(descriptor);

    ModelBackendRegistry registry;
    RegisterDefaultModelBackends(registry);

    // Default options: MMD_RAW baseline with SabaBaseline settings.
    {
        auto runtime = registry.CreateRuntime(asset);
        auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
        Require(
            mmd != nullptr,
            "default creation did not produce an MMD runtime"
        );
        MmdPhysicsConfiguration config;
        Require(
            mmd->GetMmdPhysicsConfiguration(config),
            "default creation config read failed"
        );
        Require(
            FormatConfigurationIdentity(config) == "mmd-raw-v1",
            "default creation identity mismatch"
        );
        Require(
            ValidateConfiguration(config),
            "default creation config invalid"
        );
        const ModelPhysicsRuntimeInfo info = mmd->PhysicsInfo();
        Require(
            NearlyEqual(info.gravity, glm::vec3(0.0f, -98.0f, 0.0f)),
            "default creation gravity mismatch"
        );
    }

    // Semantic preset + custom stable settings: custom identity, applied
    // before Initialize.
    {
        RuntimeCreationOptions options;
        options.compatibility =
            RuntimeCompatibilityProfile::Community;
        options.physics.gravity =
            glm::vec3(0.0f, -55.0f, 0.0f);
        auto runtime = registry.CreateRuntime(asset, options);
        auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
        Require(
            mmd != nullptr,
            "optioned creation did not produce an MMD runtime"
        );
        MmdPhysicsConfiguration config;
        Require(
            mmd->GetMmdPhysicsConfiguration(config),
            "optioned creation config read failed"
        );
        Require(
            FormatConfigurationIdentity(config) ==
                "custom-from-mmd-community-v1",
            "optioned creation identity mismatch"
        );
        Require(
            ValidateConfiguration(config),
            "optioned creation config invalid"
        );
        Require(
            config.runtime.gravity == glm::vec3(0.0f, -55.0f, 0.0f),
            "optioned gravity not applied to the authoritative config"
        );
        const ModelPhysicsRuntimeInfo info = mmd->PhysicsInfo();
        Require(
            NearlyEqual(info.gravity, glm::vec3(0.0f, -55.0f, 0.0f)),
            "optioned gravity not applied to the physics world"
        );
    }

    // Preset-only options (settings equal SabaBaseline) keep the direct
    // preset identity.
    {
        RuntimeCreationOptions options;
        options.compatibility =
            RuntimeCompatibilityProfile::Community;
        auto runtime = registry.CreateRuntime(asset, options);
        auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
        MmdPhysicsConfiguration config;
        Require(
            mmd->GetMmdPhysicsConfiguration(config),
            "preset-only config read failed"
        );
        Require(
            FormatConfigurationIdentity(config) == "mmd-community-v1",
            "preset-only identity mismatch"
        );
    }
}

void TestR14RuntimeCreationFailureTransaction()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");

    ModelAsset asset("pmx-physics");
    ModelSourceDescriptor descriptor;
    descriptor.sourcePath = modelPath;
    descriptor.backend = ModelBackendKind::SabaMmd;
    asset.SetSourceDescriptor(descriptor);

    ModelBackendRegistry registry;
    RegisterDefaultModelBackends(registry);

    // Invalid options must throw at the registry/adapter boundary.
    RuntimeCreationOptions zeroStep;
    zeroStep.physics.fixedTimeStep = 0.0f;
    bool threw = false;
    try
    {
        (void)registry.CreateRuntime(asset, zeroStep);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Require(threw, "zero fixedTimeStep options were accepted");

    RuntimeCreationOptions nanGravity;
    nanGravity.physics.gravity.y =
        std::numeric_limits<float>::quiet_NaN();
    threw = false;
    try
    {
        (void)registry.CreateRuntime(asset, nanGravity);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Require(threw, "NaN gravity options were accepted");

    // Scene-level failure transaction: failed InstantiateModel must not
    // leave a ghost Entity behind.
    Scene scene;
    Require(scene.EntityCount() == 0U, "scene did not start empty");
    try
    {
        (void)scene.InstantiateModel(asset, zeroStep);
    }
    catch (const std::exception&)
    {
    }
    Require(
        scene.EntityCount() == 0U,
        "failed InstantiateModel left a ghost entity"
    );

    // A valid creation still produces exactly one Entity.
    (void)scene.InstantiateModel(
        asset,
        RuntimeCreationOptions{}
    );
    Require(
        scene.EntityCount() == 1U,
        "valid InstantiateModel did not create exactly one entity"
    );
}

void TestR14CheckpointWireRoundTrip()
{
    const std::filesystem::path modelPath = FixturePath("pmx-physics");
    RequireCoreAsset("pmx-physics");

    auto source = CreateDeterministicRuntime(modelPath);
    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*source, 30U);

    const std::vector<std::uint8_t> bytes =
        SerializeCheckpoint(checkpoint);
    Require(
        bytes.size() > CheckpointWireHeaderSize,
        "R1.4 wire serialization produced a degenerate payload"
    );

    FrameCheckpoint decoded;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            {},
            decoded
        ) == TimelineStatus::Ok,
        "R1.4 wire deserialize failed on a valid checkpoint"
    );

    auto diverged = CreateDeterministicRuntime(modelPath);
    CaptureCanonicalAt(*diverged, 90U);
    auto* divergedMmd = dynamic_cast<MmdRuntimeModel*>(diverged.get());
    auto* divergedStepper = dynamic_cast<IDeterministicFrameStepper*>(
        diverged.get()
    );
    auto* divergedObservation =
        dynamic_cast<IDeterministicPhysicsObservation*>(diverged.get());
    Require(
        divergedMmd != nullptr && divergedStepper != nullptr &&
            divergedObservation != nullptr,
        "R1.4 wire round trip lost runtime surfaces"
    );
    Require(
        divergedMmd->ReplayFromCheckpoint(decoded, 30U) ==
            TimelineStatus::Ok,
        "R1.4 wire-restored checkpoint replay failed"
    );
    PhysicsSnapshot restored;
    Require(
        divergedObservation->CaptureState(restored) == TimelineStatus::Ok,
        "R1.4 wire-restored state capture failed"
    );
    Require(
        SnapshotsEqualExceptFollowBoneActivation(
            decoded.physics,
            restored
        ),
        "R1.4 wire-restored physics diverged from the checkpoint"
    );
    Require(
        divergedStepper->StepMotionFrameExact(31U, {}) ==
            TimelineStatus::Ok,
        "R1.4 wire restore did not prepare the next frame"
    );

    // Corrupted wire bytes must never reach the restore path.
    std::vector<std::uint8_t> corrupted = bytes;
    corrupted[corrupted.size() / 2U] ^= 0x80U;
    FrameCheckpoint ignored;
    Require(
        DeserializeCheckpoint(
            corrupted.data(),
            corrupted.size(),
            {},
            ignored
        ) == TimelineStatus::InvalidCheckpoint,
        "R1.4 wire accepted corrupted bytes"
    );

    // Build-compatibility mismatch is rejected before restore.
    CheckpointSerializationOptions foreignBuild;
    foreignBuild.buildCompatibilityIdOverride = 0xDEADBEEFULL;
    Require(
        DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            foreignBuild,
            ignored
        ) == TimelineStatus::InvalidCheckpoint,
        "R1.4 wire accepted a foreign build identity"
    );
}

}

int main()
{
    int failures = 0;
    failures += !RunTest("GLM multiply sanity", TestGlmMultiplySanity);
    failures += !RunTest("Animated model importer", TestAnimatedModelImporter);
    failures += !RunTest(
        "Extended PMX morph importer",
        TestExtendedPmxMorphImporter
    );
    failures += !RunTest(
        "PMX 2.1 Flip/Impulse importer",
        TestPmx21FlipImpulseImporter
    );
    failures += !RunTest("PMX Physics 1 importer", TestPmxPhysicsImporter);
    failures += !RunTest(
        "PMX Physics 1 importer validation",
        TestPmxPhysicsImporterValidation
    );
    failures += !RunTest(
        "MMD physics asset validation",
        TestMmdPhysicsAssetValidation
    );
    failures += !RunTest("VMD importer", TestVmdImporter);
    failures += !RunTest("VMD asset integration", TestVmdAssetWhenAvailable);
    failures += !RunTest("ModelAsset skeleton", TestModelAssetSkeleton);
    failures += !RunTest(
        "Extended MMD morph runtime",
        TestExtendedMmdMorphRuntime
    );
    failures += !RunTest(
        "PMX 2.1 Flip/Impulse runtime",
        TestPmx21FlipImpulseMorphRuntime
    );
    failures += !RunTest(
        "Saba importer PMX data comparison",
        TestSabaMmdImporterWhenAvailable
    );
    failures += !RunTest(
        "Saba runtime skinning",
        TestSabaSkinningWhenAvailable
    );
    failures += !RunTest(
        "Saba importer cross-model comparison",
        TestSabaImporterAcrossModelsWhenAvailable
    );
    failures += !RunTest(
        "Saba physics 720-frame long-run",
        TestSabaMmdPhysicsLongRunWhenAvailable
    );
    failures += !RunTest(
        "Saba physics compat baseline",
        TestSabaMmdPhysicsCompatBaselineWhenAvailable
    );
    failures += !RunTest(
        "Saba motion/camera/light interface",
        TestSabaMotionCameraLightInterfaceWhenAvailable
    );
    failures += !RunTest(
        "Saba light VMD sampling",
        TestSabaLightVmdSampling
    );
    failures += !RunTest(
        "Morph revision advances on VMD update",
        TestMorphRevisionAdvancesOnVmdUpdate
    );
    failures += !RunTest(
        "MMD camera conversion golden regression",
        TestMmdCameraConversionMatchesSaba
    );
    #if defined(WISTERIA_TEST_NATIVE_ABI)
    failures += !RunTest(
        "Native ABI lifecycle",
        TestNativeAbiLifecycle
    );
    failures += !RunTest(
        "Native ABI handle boundaries",
        TestNativeAbiHandleBoundaries
    );
    failures += !RunTest(
        "Native ABI window/scene cascade",
        TestNativeAbiWindowSceneCascade
    );
    failures += !RunTest(
        "Native ABI exception boundary",
        TestNativeAbiExceptionBoundary
    );
    failures += !RunTest(
        "Native ABI Saba runtime",
        TestNativeAbiSabaWhenAvailable
    );
    failures += !RunTest(
        "Native ABI window",
        TestNativeAbiWindowWhenAvailable
    );
    failures += !RunTest(
        "Native ABI MMD control",
        TestNativeAbiMmdControl
    );
    failures += !RunTest(
        "Native ABI scene",
        TestNativeAbiSceneWhenAvailable
    );
    failures += !RunTest(
        "Native ABI headless render",
        TestNativeAbiHeadlessRenderWhenAvailable
    );
    #endif
    failures += !RunTest("Static model importer", TestStaticModelImporter);
    failures += !RunTest("Imported resource creation", TestImportedResourceCreation);
    failures += !RunTest("Importer missing-file rejection", TestImporterRejectsMissingFile);
    failures += !RunTest(
        "Transactional imported resource creation",
        TestImportResourceCollisionIsTransactional
    );
    failures += !RunTest("Converted MMD GLB integration", TestConvertedMmdGlbWhenAvailable);
    failures += !RunTest("Converted MMD OBJ integration", TestConvertedMmdObjWhenAvailable);
    failures += !RunTest("Rigged GLB skin integration", TestRiggedGlbImportWhenAvailable);
    failures += !RunTest(
        "Demo PMX Physics 1 integration",
        TestDemoPmxPhysicsImportWhenAvailable
    );
    failures += !RunTest(
        "Direct PMX material integration",
        TestDirectPmxMaterialImportWhenAvailable
    );
    failures += !RunTest(
        "Direct PMX Group Morph integration",
        TestDirectPmxGroupMorphImportWhenAvailable
    );
    failures += !RunTest(
        "Saba VMD IK switch bridge",
        TestSabaIkSwitchBridgeWhenAvailable
    );
    failures += !RunTest(
        "R1 engine-owned MMD instances",
        TestR1EngineOwnedMmdInstances
    );
    failures += !RunTest(
        "R1.2A fixture physics sanity",
        TestR12AFixturePhysicsSanity
    );
    failures += !RunTest(
        "R1.2A replay from start repeatable",
        TestR12AReplayFromStartRepeatable
    );
    failures += !RunTest(
        "R1.2A seek consistency",
        TestR12ASeekConsistency
    );
    failures += !RunTest(
        "R1.2A ResetAtTarget canonical",
        TestR12AResetAtTargetCanonical
    );
    failures += !RunTest(
        "R1.2A step diagnostics",
        TestR12AStepDiagnostics
    );
    failures += !RunTest(
        "R1.2A dynamic bodies move",
        TestR12ADynamicBodiesMove
    );
    failures += !RunTest(
        "R1.2A unsupported profiles rejected",
        TestR12ARejectsUnsupportedProfiles
    );
    failures += !RunTest(
        "R1.2A out-of-range pose/physics",
        TestR12AOutOfRangeHoldsPoseAndStepsPhysics
    );
    failures += !RunTest(
        "R1.2A identity pose matches bind",
        TestR12AIdentityPoseMatchesBind
    );
    failures += !RunTest(
        "R1.2A 1000-frame substep probe",
        TestR12AFourSubstepProbeLongRun
    );
    failures += !RunTest(
        "R1.2A step state machine",
        TestR12AStepStateMachine
    );
    failures += !RunTest(
        "R1.2A live physics config binding",
        TestR12ALivePhysicsConfigBinding
    );
    failures += !RunTest(
        "R1.2A disabled physics rejected",
        TestR12APhysicsDisabledRejected
    );
    failures += !RunTest(
        "R1.2A divergent history reset convergence",
        TestR12ADifferentHistoryResetConverges
    );
    failures += !RunTest(
        "R1.2A morph override lifecycle",
        TestR12AMorphOverrideLifecycle
    );
    failures += !RunTest(
        "R1.2A QDEF invalid-bone fallback",
        TestR12AQdefInvalidBoneFallback
    );
    failures += !RunTest(
        "PMX QDEF parser weight slots",
        TestPmxQdefParserWeights
    );
    failures += !RunTest(
        "R1.2B restore round trip",
        TestR12BRestoreRoundTrip
    );
    failures += !RunTest(
        "R1.2B rotation basis round trip",
        TestR12BRestoreRotationBasisRoundTrip
    );
    failures += !RunTest(
        "R1.2B restore idempotent",
        TestR12BRestoreIdempotent
    );
    failures += !RunTest(
        "R1.2B restore diagnostics and zero step",
        TestR12BRestoreDiagnosticsAndZeroStep
    );
    failures += !RunTest(
        "R1.2B divergent history one step",
        TestR12BDivergentHistoryOneStep
    );
    failures += !RunTest(
        "R1.2B invalid snapshot rejections",
        TestR12BInvalidSnapshotRejections
    );
    failures += !RunTest(
        "R1.2B configuration fingerprint mismatch",
        TestR12BConfigurationFingerprintMismatch
    );
    failures += !RunTest(
        "R1.2B cross layout rejected",
        TestR12BCrossLayoutRejected
    );
    failures += !RunTest(
        "R1.2B animation precondition",
        TestR12BAnimationPrecondition
    );
    failures += !RunTest(
        "R1.2B no direct step after restore",
        TestR12BNoDirectStepAfterRestore
    );
    failures += !RunTest(
        "R1.2B FollowBone and Mode 2 restore",
        TestR12BFollowBoneAndMode2Restore
    );
    failures += !RunTest(
        "R1.2B poisoned fault injection",
        TestR12BPoisonedFaultInjection
    );
    failures += !RunTest(
        "R1.2B restore stress round trip",
        TestR12BRestoreStressRoundTrip
    );
    failures += !RunTest(
        "R1.2B DISABLE_DEACTIVATION history",
        TestR12BDisableDeactivationHistory
    );
    failures += !RunTest(
        "R1.2B FollowBone nonzero definition mass",
        TestR12BFollowBoneNonZeroDefinitionMass
    );
    failures += !RunTest(
        "R1.2C equivalence matrix",
        TestR12CEquivalenceMatrix
    );
    failures += !RunTest(
        "R1.2C zero-step restore",
        TestR12CZeroStepRestore
    );
    failures += !RunTest(
        "R1.2C beyond motion end",
        TestR12CBeyondMotionEnd
    );
    failures += !RunTest(
        "R1.2C create rejects non-canonical",
        TestR12CCreateRejectsNonCanonical
    );
    failures += !RunTest(
        "R1.2C checkpoint structural rejections",
        TestR12CCheckpointStructuralRejections
    );
    failures += !RunTest(
        "R1.2C cross compatibility rejected",
        TestR12CCrossCompatibilityRejected
    );
    failures += !RunTest(
        "R1.2C post-restore hash poisoned",
        TestR12CPostRestoreHashPoisoned
    );
    failures += !RunTest(
        "R1.2C checkpoint stress round trip",
        TestR12CCheckpointStressRoundTrip
    );
    failures += !RunTest(
        "R1.2C VMD equivalence",
        TestR12CVmdEquivalence
    );
    failures += !RunTest(
        "R1.2C true motion end hold",
        TestR12CTrueMotionEndHold
    );
    failures += !RunTest(
        "R1.2C morph override restore",
        TestR12CMorphOverrideRestore
    );
    failures += !RunTest(
        "R1.2C cross VMD rejected",
        TestR12CCrossVmdRejected
    );
    failures += !RunTest(
        "R1.2C IK override restore when available",
        TestR12CIkOverrideRestoreWhenAvailable
    );
    failures += !RunTest(
        "R1.3 config apply on Saba runtime",
        TestR13MmdPhysicsConfigurationRuntime
    );
    failures += !RunTest(
        "R1.3 trace canonical gate",
        TestR13TraceCanonicalGate
    );
    failures += !RunTest(
        "R1.4 frame domain guard",
        TestR14FrameDomainGuard
    );
    failures += !RunTest(
        "R1.4 runtime creation options",
        TestR14RuntimeCreationOptions
    );
    failures += !RunTest(
        "R1.4 runtime creation failure transaction",
        TestR14RuntimeCreationFailureTransaction
    );
    failures += !RunTest(
        "R1.4 checkpoint wire round trip",
        TestR14CheckpointWireRoundTrip
    );
    failures += !RunTest(
        "R1.3 trace reproducible and schema",
        TestR13TraceReproducibleAndSchema
    );
    failures += !RunTest(
        "R1.3 trace diff locates injection",
        TestR13TraceDiffLocatesInjection
    );
    failures += !RunTest(
        "R1.3 trace diff extended locators",
        TestR13TraceDiffExtendedLocators
    );
    failures += !RunTest(
        "R1.3 three presets 300 frames",
        TestR13ThreePresetsThreeHundredFrames
    );
    failures += !RunTest(
        "R1.3 linked-body A/B smoke",
        TestR13LinkedBodyAbSmoke
    );
    failures += !RunTest(
        "R1.3 Mode 2 A/B smoke",
        TestR13Mode2AbSmoke
    );
    failures += !RunTest(
        "R1.3 Mode 2 writeback pose",
        TestR13Mode2WritebackPose
    );
    failures += !RunTest(
        "R1.3 unit audit on fixture",
        TestR13UnitAuditOnFixture
    );
    failures += !RunTest(
        "R1 project MMD instance",
        TestR1ProjectMmdInstanceWhenAvailable
    );
    failures += !RunTest(
        "Saba importer morph targets",
        TestSabaImporterMorphTargets
    );
    return failures == 0 ? 0 : 1;
}
