#include "test_support.hpp"

#include <cstdlib>
#include <fstream>

namespace
{
void TestAnimatedModelImporter()
{
    const std::filesystem::path modelPath =
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "animated_triangle.gltf";
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
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "extended_morph.pmx";
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
    const std::filesystem::path directory =
        ProjectAssetDirectory / "models" / "mmd" / u8"凑企鹅";
    const std::filesystem::path modelPath = directory / u8"凑企鹅.pmx";
    const std::filesystem::path motionPath = directory / "penguin_walking.vmd";
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

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
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "pmx21_flip_impulse.pmx";
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
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) / "pmx_physics.pmx";
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
    const auto rejected = [](const char* fileName)
    {
        try
        {
            const std::filesystem::path path =
                std::filesystem::path(WISTERIA_TEST_DATA_DIR) / fileName;
            (void)ModelImporter().Import(path);
            return false;
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
    };

    Require(
        rejected("pmx_physics_invalid_group.pmx"),
        "PMX importer accepted collision group 16"
    );
    Require(
        rejected("pmx_physics_invalid_joint.pmx"),
        "PMX importer accepted an out-of-range joint rigid body"
    );
    Require(
        rejected("pmx_physics_softbody.pmx"),
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
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "pmx_physics.pmx";
    if (std::filesystem::is_regular_file(smallPath))
    {
        ImportedModelData small = sabaImporter.Import(smallPath);
        Require(
            small.mmdPhysics.has_value() &&
                small.mmdPhysics->RigidBodyCount() == 3U &&
                small.mmdPhysics->JointCount() == 6U,
            "Saba importer mismatched the PMX Physics 1 fixture"
        );
    }
}

void TestSabaSkinningWhenAvailable()
{
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"皮卡皮卡皮卡丘+" /
        u8"身体动作.vmd";
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

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
    runtime.UploadDynamicVertices(mesh);
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
    const std::filesystem::path mmdDirectory =
        ProjectAssetDirectory / "models" / "mmd";
    if (!std::filesystem::is_directory(mmdDirectory))
        return;

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
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"皮卡皮卡皮卡丘+" /
        u8"身体动作.vmd";
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

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
    // Phase 0/1 of the community physics adoption plan: a reproducible,
    // physics-only baseline on a frozen fixture. No motion, fixed 120 Hz
    // step, tight runaway bound; the trace export (WISTERIA_PHYSICS_TRACE)
    // feeds the cross-implementation comparison once reference traces from
    // babylon-mmd / libmmd / nanoem are available.
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
}

void TestSabaMotionCameraLightInterfaceWhenAvailable()
{
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    const std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"皮卡皮卡皮卡丘+" /
        u8"身体动作.vmd";
    const std::filesystem::path cameraPath =
        ProjectAssetDirectory / "motions" / u8"越南鼓卡点舞 镜头.vmd";
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

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

    // Camera interface: real camera VMD when available.
    if (std::filesystem::is_regular_file(cameraPath))
    {
        Require(
            runtime.LoadCameraMotion(cameraPath),
            "LoadCameraMotion rejected a camera VMD"
        );
        Camera camera;
        runtime.ApplyCameraMotion(10.0f, camera);
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
    }

    // Light interface: VMDs may not carry light frames; both outcomes must be
    // safe, and the programmatic LightTrack path must always apply.
    DirectionalLight light;
    if (runtime.LoadLightMotion(motionPath))
    {
        runtime.ApplyLightMotion(0.0f, light);
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
    runtime.ApplyLightTrack(lightTrack, 15.0f, light);
    Require(
        NearlyEqual(light.Color(), glm::vec3(0.5f, 0.5f, 0.625f)) &&
            glm::length(light.Direction()) > 0.0f,
        "LightTrack did not apply to the directional light"
    );
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

void TestNativeAbiWindowWhenAvailable()
{
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
        return;
    }

    // The renderer resolves shaders/assets relative to the current working
    // directory. CTest runs from build/, so temporarily switch to the
    // project root and restore it on every exit path.
    const std::filesystem::path previousWorkingDirectory =
        std::filesystem::current_path();
    try
    {
        std::filesystem::current_path(ProjectAssetDirectory.parent_path());

    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    Require(
        std::filesystem::is_regular_file(modelPath),
        "ABI window test model is missing"
    );
    const std::u8string modelPathU8 = modelPath.u8string();
    const std::string modelPathUtf8(
        reinterpret_cast<const char*>(modelPathU8.data()),
        modelPathU8.size()
    );
    const std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"梦的翅膀" /
        u8"梦的翅膀motion.vmd";
    const std::u8string motionPathU8 = motionPath.u8string();
    const std::string motionPathUtf8(
        reinterpret_cast<const char*>(motionPathU8.data()),
        motionPathU8.size()
    );
    const std::filesystem::path scenePath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"随便观" / u8"随便观.pmx";
    const std::u8string scenePathU8 = scenePath.u8string();
    const std::string scenePathUtf8(
        reinterpret_cast<const char*>(scenePathU8.data()),
        scenePathU8.size()
    );
    Require(
        std::filesystem::is_regular_file(motionPath) &&
            std::filesystem::is_regular_file(scenePath),
        "ABI window test motion/scene assets are missing"
    );

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
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    const std::filesystem::path motionPath =
        ProjectAssetDirectory / "motions" / u8"梦的翅膀" /
        u8"梦的翅膀motion.vmd";
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(motionPath))
    {
        return;
    }

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
    std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
    {
        modelPath = ProjectAssetDirectory / "models" / "mmd" /
            "#U53f6#U77ac#U5149_pmx" /
            "#U53f6#U77ac#U5149.pmx";
    }
    const std::filesystem::path cameraPath =
        ProjectAssetDirectory / "motions" / u8"梦的翅膀" /
        u8"梦的翅膀camera.vmd";
    if (!std::filesystem::is_regular_file(modelPath) ||
        !std::filesystem::is_regular_file(cameraPath))
    {
        return;
    }

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
            (capabilities & WISTERIA_PHYSICS_CAP_GRAVITY) != 0U,
        "ABI physics capabilities did not advertise engine-backed knobs"
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

#endif

void TestStaticModelImporter()
{
    const ImportedModelData imported = ModelImporter().Import(
        TestAssetDirectory / "models" / "embedded_triangle.gltf"
    );

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
        TestAssetDirectory / "models" / "embedded_triangle.gltf";
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
            TestAssetDirectory / "models" / "embedded_triangle.gltf"
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
        TestAssetDirectory / "models" / u8"仪玄" / u8"仪玄.glb";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
        TestAssetDirectory / "models" / u8"仪玄_obj" / u8"仪玄.obj";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "glb" /
        u8"仪玄_glb" / u8"仪玄.glb";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"叶瞬光_pmx" / u8"叶瞬光.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"仪玄_pmx" / u8"仪玄.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
        morphInstance.HasMorphState() &&
        morphInstance.GetMorphState().MorphCount() ==
            model.GetMorphSet().MorphCount(),
        "Scene did not create per-instance PMX MorphState"
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
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"爱弥斯_pmx" / u8"爱弥斯.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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
    const std::filesystem::path modelPath =
        ProjectAssetDirectory / "models" / "mmd" /
        u8"蕾米埃尔-白" / u8"蕾米埃尔-白.pmx";
    if (!std::filesystem::is_regular_file(modelPath))
        return;

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

void TestSabaImporterMorphTargets()
{
    const std::filesystem::path modelPath =
        std::filesystem::path(WISTERIA_TEST_DATA_DIR) /
        "extended_morph.pmx";
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

}

int main()
{
    int failures = 0;
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
    #if defined(WISTERIA_TEST_NATIVE_ABI)
    failures += !RunTest(
        "Native ABI lifecycle",
        TestNativeAbiLifecycle
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
        "Saba importer morph targets",
        TestSabaImporterMorphTargets
    );
    return failures == 0 ? 0 : 1;
}
