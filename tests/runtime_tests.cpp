#include "test_support.hpp"
#include "procedural_canary.hpp"
#include "wisteria/common/png_encoder.hpp"
#include "wisteria/rendering/bmp_writer.hpp"
#include "wisteria/runtime/checkpoint_serialization.hpp"
#include "wisteria/runtime/wisteria_generic_runtime_driver.hpp"
#include "wisteria/scene/offline_frame_sequence.hpp"
#include "wisteria/vendor/stb_image.h"

#include <fstream>
#include <functional>
#include <limits>

namespace
{
void TestMeshDynamicUpload()
{
    DefaultModelData data;
    data.layout = {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT},
        {"normal", 3, FLOAT},
        {"tangent", 4, FLOAT},
        {"additionalTexCoord", 2, FLOAT, false, false, 5U},
        {"edgeScale", 1, FLOAT, false, false, 6U},
        {"boneIndices", 4, FLOAT, false, false, 7U},
        {"boneWeights", 4, FLOAT, false, false, 8U}
    };
    constexpr std::size_t VertexStride = 26U;
    data.vertices.resize(3U * VertexStride);
    for (std::size_t index = 0U; index < data.vertices.size(); ++index)
        data.vertices[index] = static_cast<float>(index);
    for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
    {
        const std::size_t base = vertex * VertexStride;
        for (std::size_t slot = 18U; slot < 26U; ++slot)
            data.vertices[base + slot] = 0.0f;
    }
    data.indices = {0U, 1U, 2U};
    const std::vector<float> originalVertices = data.vertices;
    const std::vector<Layout> originalLayout = data.layout;
    Mesh mesh(std::move(data), 2U);

    const std::array<glm::vec3, 3> positions{
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    };
    const std::array<glm::vec3, 3> normals{
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };

    Require(
        !mesh.HasDynamicVertexSource(),
        "Mesh must start without a dynamic vertex source"
    );
    mesh.UploadDynamicVertices(positions, normals);
    Require(
        mesh.HasDynamicVertexSource(),
        "Mesh did not record the dynamic vertex source"
    );

    const std::vector<float> rebuilt = Mesh::RebuildInterleavedVertices(
        originalVertices,
        originalLayout,
        positions,
        normals,
        3U
    );
    for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
    {
        const std::size_t base = vertex * VertexStride;
        Require(
            rebuilt[base + 0U] == positions[vertex].x &&
                rebuilt[base + 1U] == positions[vertex].y &&
                rebuilt[base + 2U] == positions[vertex].z,
            "Interleaved rebuild wrote position to the wrong slot"
        );
        Require(
            rebuilt[base + 8U] == normals[vertex].x &&
                rebuilt[base + 9U] == normals[vertex].y &&
                rebuilt[base + 10U] == normals[vertex].z,
            "Interleaved rebuild wrote normal to the wrong slot"
        );
        Require(
            rebuilt[base + 3U] == originalVertices[base + 3U] &&
                rebuilt[base + 5U] == originalVertices[base + 5U] &&
                rebuilt[base + 11U] == originalVertices[base + 11U] &&
                rebuilt[base + 17U] == originalVertices[base + 17U],
            "Interleaved rebuild modified unrelated vertex attributes"
        );
    }

    bool rejected = false;
    try
    {
        mesh.UploadDynamicVertices({}, {});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(
        rejected,
        "Mesh accepted a dynamic vertex upload with mismatched sizes"
    );
}

class CountingSelfSteppingInstance final : public PhysicsInstance
{
public:
    bool OwnsSimulationStep() const noexcept override
    {
        return true;
    }

    void PrepareSimulation(float) override
    {
        ++this->prepareCount;
    }

    void FinishSimulation() override
    {
        ++this->finishCount;
    }

    void ResetSimulation() override
    {
        ++this->resetCount;
    }

    std::size_t prepareCount = 0U;
    std::size_t finishCount = 0U;
    std::size_t resetCount = 0U;
};

void TestSceneOwnsSimulationStep()
{
    Scene scene;
    Entity& entity = scene.CreateEntity();
    auto instance = std::make_unique<CountingSelfSteppingInstance>();
    CountingSelfSteppingInstance* raw = instance.get();
    entity.SetPhysicsInstance(std::move(instance));

    for (int frame = 0; frame < 5; ++frame)
        scene.Update(1.0f / 60.0f);

    Require(
        raw->prepareCount == 0U &&
            raw->finishCount == 0U &&
            raw->resetCount == 0U,
        "Scene drove a self-stepping physics instance"
    );
}

void TestGenericPhysicsInstanceLifecycle()
{
    Entity entity;
    PhysicsLifecycleCounters counters;
    entity.SetPhysicsInstance(
        std::make_unique<CountingPhysicsInstance>(counters)
    );
    Require(
        entity.HasPhysicsInstance() &&
        entity.TryGetPhysicsInstance() != nullptr &&
        &entity.GetPhysicsInstance() == entity.TryGetPhysicsInstance(),
        "Entity generic physics slot did not preserve the runtime"
    );

    entity.PrePhysicsUpdate(0.25f);
    entity.PreparePhysicsSubstep(0.5f, 1.0f / 60.0f);
    entity.PostPhysicsUpdate();
    entity.ResetPhysicsToCurrentPose();
    Require(
        counters.prepareCount == 1 &&
        counters.substepCount == 1 &&
        counters.finishCount == 1 &&
        counters.resetCount == 1 &&
        NearlyEqual(counters.lastDeltaTime, 0.25f) &&
        NearlyEqual(counters.lastSubstepAlpha, 0.5f) &&
        NearlyEqual(counters.lastFixedTimeStep, 1.0f / 60.0f),
        "Entity did not route simulation lifecycle through PhysicsInstance"
    );

    bool duplicateRejected = false;
    try
    {
        entity.SetPhysicsInstance(
            std::make_unique<CountingPhysicsInstance>(counters)
        );
    }
    catch (const std::logic_error&)
    {
        duplicateRejected = true;
    }
    Require(
        duplicateRejected,
        "Entity accepted a second physics runtime without explicit removal"
    );
}

void TestCameraLightTrackSampling()
{
    CameraTrack track({
        CameraKeyframe{
            0.0f,
            {0.0f, 0.0f, 0.0f},
            glm::vec3(0.0f),
            5.0f,
            30.0f,
            true,
            {}
        },
        CameraKeyframe{
            30.0f,
            {12.0f, 0.0f, 6.0f},
            glm::vec3(0.0f),
            11.0f,
            60.0f,
            true,
            {}
        }
    });
    Require(
        NearlyEqual(track.EndTime(), 30.0f),
        "CameraTrack end time is incorrect"
    );

    CameraKeyframe sample;
    Require(track.Sample(15.0f, sample), "CameraTrack mid sampling failed");
    Require(
        NearlyEqual(sample.interest, glm::vec3(6.0f, 0.0f, 3.0f)) &&
            NearlyEqual(sample.distance, 8.0f) &&
            NearlyEqual(sample.viewAngle, 45.0f),
        "CameraTrack interpolation is incorrect"
    );
    Require(
        track.Sample(-5.0f, sample) &&
            NearlyEqual(sample.interest, glm::vec3(0.0f)),
        "CameraTrack does not clamp before the first key"
    );
    Require(
        track.Sample(99.0f, sample) &&
            NearlyEqual(sample.distance, 11.0f),
        "CameraTrack does not clamp after the last key"
    );

    LightTrack lightTrack({
        LightKeyframe{
            0.0f,
            {1.0f, 0.0f, 0.0f},
            {1.0f, 2.0f, 3.0f},
            {}
        },
        LightKeyframe{
            10.0f,
            {0.0f, 1.0f, 0.0f},
            {3.0f, 2.0f, 1.0f},
            {}
        }
    });
    LightKeyframe lightSample;
    Require(
        lightTrack.Sample(5.0f, lightSample),
        "LightTrack mid sampling failed"
    );
    Require(
        NearlyEqual(lightSample.color, glm::vec3(0.5f, 0.5f, 0.0f)) &&
            NearlyEqual(lightSample.position, glm::vec3(2.0f, 2.0f, 2.0f)),
        "LightTrack interpolation is incorrect"
    );
}

void TestRenderPartAndModelAsset()
{
    Mesh mesh(DefaultModelData{});
    Material material(MaterialData{});
    const glm::mat4 localTransform = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(1.0f, 2.0f, 3.0f)
    );

    ModelAsset model("testModel");
    std::vector<MmdRigidBodyDefinition> bodies(3U);
    for (std::size_t index = 0; index < bodies.size(); ++index)
        bodies[index].name = "body" + std::to_string(index);
    model.SetMmdPhysics(MmdPhysicsAsset(std::move(bodies), {}));
    model.AddPart(mesh, material, localTransform);

    Require(model.Name() == "testModel", "ModelAsset name was not preserved");
    Require(
        model.HasMmdPhysics() &&
        model.TryGetMmdPhysics() == &model.GetMmdPhysics() &&
        model.MmdRigidBodyCount() == 3U &&
        model.GetMmdPhysics().JointCount() == 0U,
        "ModelAsset did not preserve PMX physics metadata"
    );
    Require(model.PartCount() == 1, "ModelAsset did not store its part");
    Require(&model.Parts()[0].GetMesh() == &mesh, "ModelAsset mesh reference changed");
    Require(
        &model.Parts()[0].GetMaterial() == &material,
        "ModelAsset material reference changed"
    );
    Require(
        NearlyEqual(model.Parts()[0].LocalTransform()[3].x, 1.0f) &&
        NearlyEqual(model.Parts()[0].LocalTransform()[3].y, 2.0f) &&
        NearlyEqual(model.Parts()[0].LocalTransform()[3].z, 3.0f),
        "RenderPart local transform changed"
    );
}

void TestBuiltInCubeTangents()
{
    constexpr std::size_t VertexCount = 24;
    constexpr std::size_t VertexStride = 15;
    Require(cubeData.layout.size() == 5, "Cube tangent layout is missing");
    Require(
        cubeData.vertices.size() == VertexCount * VertexStride,
        "Cube vertex stride does not match its layout"
    );

    for (std::size_t vertex = 0; vertex < VertexCount; ++vertex)
    {
        const std::size_t offset = vertex * VertexStride;
        const glm::vec3 normal(
            cubeData.vertices[offset + 8],
            cubeData.vertices[offset + 9],
            cubeData.vertices[offset + 10]
        );
        const glm::vec3 tangent(
            cubeData.vertices[offset + 11],
            cubeData.vertices[offset + 12],
            cubeData.vertices[offset + 13]
        );
        Require(NearlyEqual(glm::length(tangent), 1.0f), "Cube tangent is not normalized");
        Require(NearlyEqual(glm::dot(normal, tangent), 0.0f), "Cube tangent is not orthogonal");
        Require(
            NearlyEqual(std::abs(cubeData.vertices[offset + 14]), 1.0f),
            "Cube tangent handedness is invalid"
        );
    }
}

void TestMeshBoundsCenter()
{
    DefaultModelData data{
        {
            -4.0f, 2.0f, -1.0f,
             2.0f, 8.0f,  5.0f,
             0.0f, 3.0f,  1.0f
        },
        {0U, 1U, 2U},
        {{"position", 3, FLOAT}}
    };
    const Mesh mesh(std::move(data));
    const glm::vec3 center = mesh.LocalBoundsCenter();
    Require(
        NearlyEqual(center.x, -1.0f) &&
        NearlyEqual(center.y, 5.0f) &&
        NearlyEqual(center.z, 2.0f),
        "Mesh local bounds center is incorrect"
    );
}

void TestModelInstantiation()
{
    Mesh firstMesh(DefaultModelData{});
    Mesh secondMesh(DefaultModelData{});
    Material firstMaterial(MaterialData{});
    Material secondMaterial(MaterialData{});

    ModelAsset model("multiPartModel");
    model.AddPart(firstMesh, firstMaterial);
    model.AddPart(
        secondMesh,
        secondMaterial,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f))
    );

    Scene scene;
    Entity& instance = scene.InstantiateModel(
        model,
        Transform(glm::vec3(5.0f, 0.0f, 0.0f))
    );

    Require(scene.EntityCount() == 1, "Scene did not create one model Entity");
    Require(instance.RenderPartCount() == 2, "Entity did not receive all model parts");
    Require(
        &instance.RenderParts()[1].GetMesh() == &secondMesh,
        "Second model part references the wrong mesh"
    );
    Require(
        NearlyEqual(instance.GetTransform().Position().x, 5.0f),
        "Model instance root transform changed"
    );
}

void TestProceduralVertexCanary()
{
    ModelAsset model("procedural-vertex-canary");
    ConfigureProceduralCanary(model);
    Require(
        model.BackendKind() == ModelBackendKind::WisteriaGeneric,
        "Procedural canary lost its explicit backend identity"
    );
    Require(
        model.HasExplicitBackendKind(),
        "Procedural canary must carry an explicit backend kind"
    );

    ModelBackendRegistry registry;
    registry.Register(std::make_unique<ProceduralTestBackend>());

    std::unique_ptr<IModelRuntimeDriver> runtime =
        registry.CreateRuntime(model);
    Require(
        runtime != nullptr,
        "Registry did not create a WisteriaGeneric runtime"
    );
    Require(
        runtime->BackendName() == "procedural-canary",
        "Procedural backend name mismatch"
    );

    ModelInstance instance(model, std::move(runtime));
    Require(instance.HasRuntime(), "ModelInstance lost its runtime");

    const ModelFrameSnapshot& before =
        instance.CaptureSnapshot(CaptureMask::All);
    Require(
        !before.metadata.valid,
        "Snapshot became valid before the first Update"
    );

    instance.Update(0.25f);
    Require(
        instance.LastFrameView().geometry.positions.size() == 3U,
        "Vertex canary did not publish three positions"
    );
    Require(
        instance.LastFrameView().pose == nullptr,
        "Vertex-only runtime fabricated a Pose"
    );

    const ModelFrameSnapshot& snapshot =
        instance.CaptureSnapshot(CaptureMask::All);
    Require(snapshot.metadata.valid, "Snapshot is not valid after Update");
    Require(!snapshot.pose.captured, "No-Pose runtime captured a Pose");
    Require(
        !snapshot.morphs.captured,
        "No-Morph runtime captured morphs"
    );
    Require(
        snapshot.geometry.captured,
        "Vertex runtime did not capture geometry"
    );
    Require(
        snapshot.geometry.positions.size() == 3U,
        "Geometry snapshot has the wrong vertex count"
    );

    const glm::vec3 firstPosition = snapshot.geometry.positions[1];
    instance.Update(0.25f);
    const ModelFrameSnapshot& second =
        instance.CaptureSnapshot(CaptureMask::All);
    Require(
        second.geometry.positions[1] != firstPosition,
        "Vertex geometry did not change with time"
    );

    ModelInstance firstInstance(model, registry.CreateRuntime(model));
    ModelInstance secondInstance(model, registry.CreateRuntime(model));
    firstInstance.Update(0.1f);
    secondInstance.Update(0.9f);
    const ModelFrameSnapshot& firstSnapshot =
        firstInstance.CaptureSnapshot(CaptureMask::All);
    const ModelFrameSnapshot& secondSnapshot =
        secondInstance.CaptureSnapshot(CaptureMask::All);
    Require(
        firstSnapshot.geometry.positions[0] !=
            secondSnapshot.geometry.positions[0],
        "Vertex canary instances share mutable state"
    );

    bool noPoseRejected = false;
    try
    {
        (void)instance.TryGetRuntime()->GetPose();
    }
    catch (const std::logic_error&)
    {
        noPoseRejected = true;
    }
    Require(
        noPoseRejected,
        "GetPose() did not reject a Pose-less runtime"
    );

    Entity entity;
    entity.SetModelInstance(
        std::make_unique<ModelInstance>(model, registry.CreateRuntime(model))
    );
    entity.Update(0.5f);
    const ModelFrameSnapshot& entitySnapshot =
        entity.GetModelInstance().CaptureSnapshot(CaptureMask::All);
    Require(
        entitySnapshot.metadata.valid,
        "Entity-chain snapshot is not valid"
    );
    Require(
        entitySnapshot.geometry.captured,
        "Entity-chain snapshot lost geometry"
    );
    Require(
        !entitySnapshot.pose.captured,
        "Entity-chain snapshot fabricated a Pose"
    );
    Require(!entity.HasPose(), "Entity reported a Pose for vertex canary");
    Require(
        entity.TryGetPose() == nullptr,
        "Entity returned a fabricated Pose"
    );
    Require(
        !entity.HasAnimator(),
        "Entity reported an Animator for vertex canary"
    );
    Require(
        !entity.HasMorphState(),
        "Entity reported morph state for vertex canary"
    );
}

void TestProceduralOneBoneCanary()
{
    ModelAsset model("procedural-one-bone-canary");
    ConfigureProceduralCanary(model);
    ModelBackendRegistry registry;
    registry.Register(std::make_unique<ProceduralTestBackend>());

    ModelInstance instance(model, registry.CreateRuntime(model));
    Require(instance.HasRuntime(), "One-bone canary has no runtime");

    instance.Update(0.25f);
    Require(
        instance.LastFrameView().pose != nullptr,
        "One-bone runtime did not publish its Pose"
    );
    Require(
        instance.LastFrameView().pose->BoneCount() == 1U,
        "One-bone runtime published the wrong bone count"
    );

    const ModelFrameSnapshot& snapshot =
        instance.CaptureSnapshot(CaptureMask::All);
    Require(snapshot.metadata.valid, "Snapshot is not valid after Update");
    Require(snapshot.pose.captured, "One-bone Pose was not captured");
    Require(
        snapshot.pose.localTransforms.size() == 1U,
        "Pose snapshot has the wrong bone count"
    );
    Require(
        !snapshot.geometry.captured,
        "No-geometry runtime captured an empty geometry channel"
    );
    Require(
        !snapshot.morphs.captured,
        "One-bone runtime captured morphs"
    );

    const glm::mat4 firstLocal = snapshot.pose.localTransforms[0];
    instance.Update(0.25f);
    const ModelFrameSnapshot& second =
        instance.CaptureSnapshot(CaptureMask::All);
    Require(
        second.pose.localTransforms[0] != firstLocal,
        "One-bone Pose did not change with time"
    );

    ModelInstance firstInstance(model, registry.CreateRuntime(model));
    ModelInstance secondInstance(model, registry.CreateRuntime(model));
    firstInstance.Update(0.1f);
    secondInstance.Update(0.9f);
    const ModelFrameSnapshot& firstSnapshot =
        firstInstance.CaptureSnapshot(CaptureMask::All);
    const ModelFrameSnapshot& secondSnapshot =
        secondInstance.CaptureSnapshot(CaptureMask::All);
    Require(
        firstSnapshot.pose.localTransforms[0] !=
            secondSnapshot.pose.localTransforms[0],
        "One-bone instances share mutable Pose state"
    );

    Entity entity;
    entity.SetModelInstance(
        std::make_unique<ModelInstance>(model, registry.CreateRuntime(model))
    );
    IModelRuntimeDriver* runtime =
        entity.TryGetModelInstance()->TryGetRuntime();
    Require(entity.HasPose(), "Entity did not forward the runtime Pose");
    Require(
        entity.HasAnimator(),
        "HasSkeleton runtime must expose an Animator"
    );
    Require(
        entity.TryGetAnimator() == runtime->TryGetAnimator() &&
            entity.TryGetAnimator() != nullptr,
        "Entity Animator forwarding returned null"
    );
    Require(
        entity.TryGetPose() == runtime->TryGetPose(),
        "Entity Pose forwarding resolved to a non-runtime owner"
    );
    Require(
        entity.TryGetMorphState() == nullptr,
        "Entity fabricated morph state"
    );
    entity.Update(0.5f);
    const ModelFrameSnapshot& entitySnapshot =
        entity.GetModelInstance().CaptureSnapshot(CaptureMask::All);
    Require(
        entitySnapshot.metadata.valid,
        "Entity-chain snapshot is not valid"
    );
    Require(
        entitySnapshot.pose.captured,
        "Entity-chain snapshot lost the Pose"
    );
    Require(
        entitySnapshot.pose.localTransforms.size() == 1U,
        "Entity-chain Pose snapshot has the wrong bone count"
    );
    Require(
        !entitySnapshot.geometry.captured,
        "Entity-chain snapshot fabricated geometry"
    );
    Require(
        entity.GetPose().BoneCount() == 1U,
        "Entity Pose forwarding broke the one-bone runtime"
    );
}

void TestProceduralRootMotionExactlyOnce()
{
    ModelAsset model("procedural-root-motion-canary");
    ConfigureProceduralCanary(model);
    ModelBackendRegistry registry;
    registry.Register(std::make_unique<ProceduralTestBackend>());

    ModelInstance instance(model, registry.CreateRuntime(model));
    IModelRuntimeDriver* runtime = instance.TryGetRuntime();
    const RootMotionDelta delta = instance.Update(0.5f);
    Require(
        !delta.IsIdentity(),
        "ModelInstance::Update lost the pending root motion"
    );
    Require(
        NearlyEqual(delta.translation.x, 0.25f),
        "Root motion delta has the wrong translation"
    );
    Require(
        runtime->ConsumeRootMotion().IsIdentity(),
        "Root motion was not consumed exactly once"
    );
    Require(
        instance.Update(0.0f).IsIdentity(),
        "Zero-delta update fabricated root motion"
    );

    Entity entity;
    entity.SetModelInstance(
        std::make_unique<ModelInstance>(model, registry.CreateRuntime(model))
    );
    entity.Update(0.5f);
    Require(
        NearlyEqual(entity.GetTransform().Position().x, 0.25f),
        "Entity did not apply the runtime root motion"
    );
    entity.Update(0.0f);
    Require(
        NearlyEqual(entity.GetTransform().Position().x, 0.25f),
        "Entity applied root motion more than once"
    );
}

void TestProceduralMalformedGeometryRejected()
{
    ModelAsset model("procedural-malformed-canary");
    ConfigureProceduralCanary(model);
    ModelBackendRegistry registry;
    registry.Register(std::make_unique<ProceduralTestBackend>());

    ModelInstance instance(model, registry.CreateRuntime(model));
    bool updateRejected = false;
    try
    {
        instance.Update(0.0f);
    }
    catch (const std::logic_error&)
    {
        updateRejected = true;
    }
    Require(
        updateRejected,
        "ModelInstance accepted a malformed render frame at Update"
    );
}

void TestProceduralMalformedRenderChannelsRejected()
{
    ModelBackendRegistry registry;
    registry.Register(std::make_unique<ProceduralTestBackend>());

    ModelAsset uvModel("procedural-malformed-uv-canary");
    ConfigureProceduralCanary(uvModel);
    ModelInstance uvInstance(uvModel, registry.CreateRuntime(uvModel));
    bool uvRejected = false;
    try
    {
        uvInstance.Update(0.0f);
    }
    catch (const std::logic_error&)
    {
        uvRejected = true;
    }
    Require(
        uvRejected,
        "ModelInstance accepted a malformed UV channel"
    );

    ModelAsset materialModel("procedural-malformed-materials-canary");
    ConfigureProceduralCanary(materialModel);
    ModelInstance materialInstance(
        materialModel,
        registry.CreateRuntime(materialModel)
    );
    bool materialsRejected = false;
    try
    {
        materialInstance.Update(0.0f);
    }
    catch (const std::logic_error&)
    {
        materialsRejected = true;
    }
    Require(
        materialsRejected,
        "ModelInstance accepted a malformed material slot channel"
    );
}

void TestRuntimeSuppressesLegacyState()
{
    ModelAsset model("procedural-vertex-canary");
    ConfigureProceduralCanary(model);
    ModelBackendRegistry registry;
    registry.Register(std::make_unique<ProceduralTestBackend>());

    Bone root;
    root.name = "root";
    root.parentIndex = InvalidBoneIndex;
    std::vector<Bone> bones;
    bones.push_back(root);
    Skeleton skeleton(std::move(bones));

    MorphDefinition blink;
    blink.name = "blink";
    blink.category = MorphCategory::Other;
    blink.kind = MorphKind::Vertex;
    std::vector<MorphDefinition> definitions;
    definitions.push_back(blink);
    MorphSet morphSet(std::move(definitions));

    Entity entity;
    entity.SetSkeleton(skeleton);
    entity.SetMorphSet(morphSet);
    Require(
        entity.TryGetPose() != nullptr &&
            entity.TryGetAnimator() != nullptr &&
            entity.TryGetMorphState() != nullptr,
        "Legacy state was not installed before the runtime"
    );

    entity.SetModelInstance(
        std::make_unique<ModelInstance>(model, registry.CreateRuntime(model))
    );
    Require(
        entity.TryGetPose() == nullptr,
        "Runtime-backed Entity leaked legacy Pose"
    );
    Require(
        entity.TryGetAnimator() == nullptr,
        "Runtime-backed Entity leaked legacy Animator"
    );
    Require(
        entity.TryGetMorphState() == nullptr,
        "Runtime-backed Entity leaked legacy MorphState"
    );

    bool animatorRejected = false;
    try
    {
        (void)entity.GetAnimator();
    }
    catch (const std::logic_error&)
    {
        animatorRejected = true;
    }
    Require(
        animatorRejected,
        "GetAnimator() did not reject suppressed legacy state"
    );

    bool morphRejected = false;
    try
    {
        (void)entity.GetMorphState();
    }
    catch (const std::logic_error&)
    {
        morphRejected = true;
    }
    Require(
        morphRejected,
        "GetMorphState() did not reject suppressed legacy state"
    );
}

void TestPngEncoderRoundTrip()
{
    constexpr int Width = 3;
    constexpr int Height = 2;
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(Width) * Height * 4U
    );
    for (int y = 0; y < Height; ++y)
    {
        for (int x = 0; x < Width; ++x)
        {
            const std::size_t base =
                static_cast<std::size_t>(y * Width + x) * 4U;
            rgba[base + 0U] = static_cast<std::uint8_t>(x * 80U);
            rgba[base + 1U] = static_cast<std::uint8_t>(y * 90U);
            rgba[base + 2U] = static_cast<std::uint8_t>(
                x * 40U + y * 60U
            );
            rgba[base + 3U] = 255U;
        }
    }

    const std::vector<std::uint8_t> png = EncodePngRgba8(
        static_cast<std::uint32_t>(Width),
        static_cast<std::uint32_t>(Height),
        rgba
    );
    Require(
        png.size() > 8U &&
            png[0] == 0x89U && png[1] == 0x50U &&
            png[2] == 0x4EU && png[3] == 0x47U,
        "PNG encoder did not emit a PNG signature"
    );

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        png.data(),
        static_cast<int>(png.size()),
        &decodedWidth,
        &decodedHeight,
        &decodedChannels,
        4
    );
    Require(
        decoded != nullptr,
        "PNG encoder output could not be decoded"
    );
    Require(
        decodedWidth == Width && decodedHeight == Height &&
            decodedChannels == 4,
        "PNG round trip changed dimensions or channels"
    );
    for (std::size_t index = 0U; index < rgba.size(); ++index)
    {
        Require(
            decoded[index] == rgba[index],
            "PNG round trip changed pixel data"
        );
    }
    stbi_image_free(decoded);
}

void TestBmpWriterOrientation()
{
    // Canonical top-left 2x2 frame:
    //   red    | green
    //   blue   | white
    constexpr int Width = 2;
    constexpr int Height = 2;
    const std::array<std::uint8_t, Width * Height * 4> rgba{
        255U, 0U, 0U, 255U,    0U, 255U, 0U, 255U,
        0U, 0U, 255U, 255U,    255U, 255U, 255U, 255U
    };
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "wisteria_bmp_orientation_test.bmp";
    WriteBmp24(path, Width, Height, rgba);

    std::ifstream stream(path, std::ios::binary);
    Require(stream.is_open(), "BMP test file is unreadable");
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    };
    const auto read32 = [&bytes](std::size_t offset)
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    };
    const auto read16 = [&bytes](std::size_t offset)
    {
        return static_cast<std::uint16_t>(bytes[offset]) |
            (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
    };
    Require(
        bytes.size() >= 54U &&
            bytes[0] == 'B' && bytes[1] == 'M',
        "BMP header is invalid"
    );
    Require(
        static_cast<int>(read32(18U)) == Width &&
            static_cast<int>(read32(22U)) == Height &&
            read16(28U) == 24U,
        "BMP header dimensions or bit depth changed"
    );
    const std::size_t pixelOffset = read32(10U);
    const std::size_t rowSize = ((Width * 3 + 3) / 4) * 4;
    const auto pixelAt = [&](int x, int y)
    {
        // BMP rows are bottom-up: file row y == image row Height-1-y.
        const std::size_t row = static_cast<std::size_t>(Height - 1 - y);
        const std::size_t base =
            pixelOffset + row * rowSize + static_cast<std::size_t>(x) * 3U;
        return std::array<std::uint8_t, 3>{
            bytes[base + 2U],
            bytes[base + 1U],
            bytes[base]
        };
    };
    const auto expectPixel = [&](int x, int y, std::uint8_t r,
        std::uint8_t g, std::uint8_t b)
    {
        const std::array<std::uint8_t, 3> pixel = pixelAt(x, y);
        Require(
            pixel[0] == r && pixel[1] == g && pixel[2] == b,
            "BMP orientation mismatch"
        );
    };
    expectPixel(0, 0, 255U, 0U, 0U);
    expectPixel(1, 0, 0U, 255U, 0U);
    expectPixel(0, 1, 0U, 0U, 255U);
    expectPixel(1, 1, 255U, 255U, 255U);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void TestFrameRateIndependentBehaviours()
{
    Mesh mesh(DefaultModelData{});
    Material material(MaterialData{});
    Entity entity(mesh, material);

    entity.AddBehaviour<MoveBehaviour>(glm::vec3(2.0f, 0.0f, 0.0f));
    entity.AddBehaviour<RotateBehaviour>(glm::vec3(0.0f, 90.0f, 0.0f));
    entity.AddBehaviour<ScaleBehaviour>(glm::vec3(2.0f, 1.0f, 0.5f));

    entity.UpdateBehaviours(0.5f);
    entity.UpdateBehaviours(0.5f);

    Require(NearlyEqual(entity.GetTransform().Position().x, 2.0f), "MoveBehaviour is frame dependent");
    Require(NearlyEqual(entity.GetTransform().Rotation().y, 90.0f), "RotateBehaviour is frame dependent");
    Require(NearlyEqual(entity.GetTransform().Scale().x, 2.0f), "ScaleBehaviour X result is incorrect");
    Require(NearlyEqual(entity.GetTransform().Scale().z, 0.5f), "ScaleBehaviour Z result is incorrect");
}

void TestInputFrameTransitions()
{
    Input input;

    input.BeginFrame();
    input.HandleKey(InputKey::W, true);
    Require(input.IsKeyDown(InputKey::W), "Pressed key was not held");
    Require(input.WasKeyPressed(InputKey::W), "Key press transition was lost");
    Require(!input.WasKeyReleased(InputKey::W), "Pressed key was reported released");

    input.BeginFrame();
    Require(input.IsKeyDown(InputKey::W), "BeginFrame cleared held key state");
    Require(!input.WasKeyPressed(InputKey::W), "Key press leaked into the next frame");

    input.HandleKey(InputKey::W, false);
    Require(!input.IsKeyDown(InputKey::W), "Released key remained held");
    Require(input.WasKeyReleased(InputKey::W), "Key release transition was lost");

    input.HandleKey(InputKey::Right, true);
    input.HandleKey(InputKey::Space, true);
    Require(
        input.WasKeyPressed(InputKey::Right) &&
        input.WasKeyPressed(InputKey::Space),
        "Morph Lab navigation keys were not tracked"
    );

    input.HandleCursorPosition(10.0, 20.0);
    input.HandleCursorPosition(14.0, 17.0);
    input.HandleScroll(2.0);
    Require(NearlyEqual(static_cast<float>(input.CursorDelta().x), 4.0f), "Mouse X delta is incorrect");
    Require(NearlyEqual(static_cast<float>(input.CursorDelta().y), -3.0f), "Mouse Y delta is incorrect");
    Require(NearlyEqual(static_cast<float>(input.ScrollDeltaY()), 2.0f), "Scroll delta is incorrect");

    input.BeginFrame();
    Require(NearlyEqual(static_cast<float>(input.CursorDelta().x), 0.0f), "Mouse delta was not cleared");
    Require(NearlyEqual(static_cast<float>(input.ScrollDeltaY()), 0.0f), "Scroll delta was not cleared");
}

void TestFreeCameraController()
{
    Camera camera(CameraParam{
        .Position = {0.0f, 0.0f, 3.0f},
        .Target = {0.0f, 0.0f, 0.0f},
        .Up = {0.0f, 1.0f, 0.0f},
        .VerticalFovDegrees = 45.0f
    });
    Input input;
    FreeCameraControllerBehaviour controller(
        camera,
        input,
        FreeCameraControllerSettings{
            .moveSpeed = 2.0f,
            .sprintMultiplier = 2.0f,
            .mouseSensitivity = 1.0f,
            .scrollSensitivity = 5.0f
        }
    );

    input.BeginFrame();
    input.HandleKey(InputKey::W, true);
    controller.Update(0.5f);
    Require(NearlyEqual(camera.Position().z, 2.0f), "Free camera forward movement is incorrect");

    input.BeginFrame();
    input.HandleKey(InputKey::LeftShift, true);
    controller.Update(0.5f);
    Require(NearlyEqual(camera.Position().z, 0.0f), "Free camera sprint movement is incorrect");
    input.HandleKey(InputKey::W, false);
    input.HandleKey(InputKey::LeftShift, false);

    input.BeginFrame();
    input.HandleScroll(2.0);
    controller.Update(0.0f);
    Require(NearlyEqual(camera.VerticalFovDegrees(), 35.0f), "Free camera zoom is incorrect");

    input.BeginFrame();
    input.HandleMouseButton(InputMouseButton::Right, true);
    controller.Update(0.0f);
    Require(input.IsCursorCaptured(), "Right mouse button did not capture the cursor");

    input.BeginFrame();
    input.HandleMouseButton(InputMouseButton::Right, false);
    input.HandleCursorPosition(100.0, 100.0);
    input.HandleCursorPosition(110.0, 100.0);
    controller.Update(0.0f);
    Require(camera.Target().x > camera.Position().x, "Mouse movement did not rotate the camera");

    input.BeginFrame();
    input.HandleKey(InputKey::Escape, true);
    controller.Update(0.0f);
    Require(!input.IsCursorCaptured(), "Escape did not release the cursor");

    input.BeginFrame();
    input.HandleKey(InputKey::R, true);
    controller.Update(0.0f);
    Require(NearlyEqual(camera.Position().z, 3.0f), "Camera reset did not restore position");
    Require(NearlyEqual(camera.Target().x, 0.0f), "Camera reset did not restore direction");
    Require(NearlyEqual(camera.VerticalFovDegrees(), 45.0f), "Camera reset did not restore FOV");
}

void TestResourceManagerModelRegistry()
{
    ResourceManager resources;
    ModelAsset& model = resources.CreateModel("registeredModel");

    Require(resources.FindModel("registeredModel") == &model, "FindModel failed");
    Require(&resources.GetModel("registeredModel") == &model, "GetModel failed");

    bool duplicateRejected = false;
    try
    {
        resources.CreateModel("registeredModel");
    }
    catch (const std::invalid_argument&)
    {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "Duplicate model name was accepted");
}

void TestEnvironmentResourceAndSceneBinding()
{
    EnvironmentMapData data = EnvironmentMapData::ProceduralSky();
    data.environmentResolution = 64;
    data.irradianceResolution = 16;
    data.prefilterResolution = 64;
    data.prefilterMipLevels = 4;
    data.brdfResolution = 64;
    data.intensity = 1.5f;

    ResourceManager resources;
    EnvironmentMap& environment = resources.CreateEnvironment(
        "testEnvironment",
        data
    );
    Scene scene;
    scene.SetEnvironment(&environment);

    Require(
        scene.Environment() == &environment,
        "Scene did not preserve its environment reference"
    );
    Require(
        resources.FindEnvironment("testEnvironment") == &environment &&
        &resources.GetEnvironment("testEnvironment") == &environment,
        "ResourceManager environment lookup failed"
    );
    Require(
        resources.EnvironmentCount() == 1,
        "ResourceManager environment count is incorrect"
    );
    Require(!environment.IsAttached(), "CPU test unexpectedly created OpenGL IBL resources");
    Require(NearlyEqual(environment.Intensity(), 1.5f), "Environment intensity changed");
    Require(NearlyEqual(environment.MaxReflectionLod(), 3.0f), "Environment mip range changed");

    environment.SetIntensity(0.75f);
    environment.SetDrawSkybox(false);
    Require(NearlyEqual(environment.Intensity(), 0.75f), "Environment intensity setter failed");
    Require(!environment.ShouldDrawSkybox(), "Environment skybox setter failed");

    scene.ClearEnvironment();
    Require(scene.Environment() == nullptr, "Scene environment was not cleared");

    bool duplicateRejected = false;
    try
    {
        resources.CreateEnvironment("testEnvironment", data);
    }
    catch (const std::invalid_argument&)
    {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "Duplicate environment name was accepted");

    bool invalidMipCountRejected = false;
    try
    {
        EnvironmentMapData invalid = data;
        invalid.prefilterMipLevels = 8;
        EnvironmentMap invalidEnvironment(invalid);
    }
    catch (const std::invalid_argument&)
    {
        invalidMipCountRejected = true;
    }
    Require(invalidMipCountRejected, "Invalid environment mip count was accepted");
}

}

// R1.8 Phase 0B helpers.
void ConfigureR18GenericTimelineAsset(
    ModelAsset& model,
    float rootEndX = 2.0f
)
{
    Bone root;
    root.name = "root";
    root.parentIndex = InvalidBoneIndex;
    root.bindLocalMatrix = glm::mat4(1.0f);
    root.inverseBindMatrix = glm::mat4(1.0f);
    std::vector<Bone> bones;
    bones.push_back(root);

    model.SetBackendKind(ModelBackendKind::WisteriaGeneric);
    model.SetSkeleton(Skeleton(std::move(bones)));

    MorphDefinition blinkMorph;
    blinkMorph.name = "blink";
    blinkMorph.category = MorphCategory::Other;
    blinkMorph.kind = MorphKind::Vertex;
    model.SetMorphs(std::vector<MorphDefinition>{blinkMorph});

    // Root bone translates x: 0 at t=0 -> 2 at t=1 (clip duration 1s).
    std::vector<VectorKeyframe> translationKeys;
    translationKeys.push_back(VectorKeyframe{
        .time = 0.0f,
        .value = glm::vec3(0.0f, 0.0f, 0.0f)
    });
    translationKeys.push_back(VectorKeyframe{
        .time = 1.0f,
        .value = glm::vec3(rootEndX, 0.0f, 0.0f)
    });
    std::vector<AnimationTrack> tracks;
    tracks.emplace_back(0U, translationKeys);
    model.AddAnimationClip(AnimationClip("walk", 1.0f, std::move(tracks)));

    // Second clip used by subset-rejection tests (crossfade destination).
    std::vector<AnimationTrack> secondTracks;
    secondTracks.emplace_back(0U, translationKeys);
    model.AddAnimationClip(
        AnimationClip("walk2", 1.0f, std::move(secondTracks))
    );
}

std::unique_ptr<WisteriaGenericRuntimeDriver> CreateR18GenericRuntime(
    const ModelAsset& model
)
{
    auto runtime = std::make_unique<WisteriaGenericRuntimeDriver>(model);
    Require(
        runtime->Initialize(),
        "R1.8 generic runtime initialize failed"
    );
    return runtime;
}

void TestR18GenericPrepareFrameZeroAndExactStep()
{
    ModelAsset model("r18-generic-deterministic");
    ConfigureR18GenericTimelineAsset(model);
    auto runtimeA = CreateR18GenericRuntime(model);
    auto runtimeB = CreateR18GenericRuntime(model);

    const ReplayConfig config{30U, 120U, 0U, false};
    Require(
        runtimeA->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 PrepareFrameZero failed (A)"
    );
    Require(
        runtimeB->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 PrepareFrameZero failed (B)"
    );
    Require(
        runtimeA->TryGetPose()->LocalMatrix(0U) ==
            runtimeB->TryGetPose()->LocalMatrix(0U),
        "R1.8 frame 0 pose mismatch between instances"
    );

    for (MotionFrameIndex frame = 1U; frame <= 60U; ++frame)
    {
        Require(
            runtimeA->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "R1.8 exact step failed (A)"
        );
        Require(
            runtimeB->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "R1.8 exact step failed (B)"
        );
    }
    Require(
        runtimeA->TryGetPose()->LocalMatrix(0U) ==
            runtimeB->TryGetPose()->LocalMatrix(0U),
        "R1.8 frame 60 pose mismatch between instances"
    );
    // Non-looping: 60/30 = 2s clamps to clip duration 1s -> x = 2.
    Require(
        NearlyEqual(
            runtimeA->TryGetPose()->LocalMatrix(0U)[3].x,
            2.0f
        ),
        "R1.8 non-looping clamp did not reach the clip end"
    );

    // State machine rejections.
    auto fresh = CreateR18GenericRuntime(model);
    Require(
        fresh->StepMotionFrameExact(1U, config) ==
            TimelineStatus::InvalidState,
        "R1.8 exact step before PrepareFrameZero was accepted"
    );
    Require(
        fresh->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 fresh PrepareFrameZero failed"
    );
    Require(
        fresh->StepMotionFrameExact(2U, config) ==
            TimelineStatus::NonSequentialFrame,
        "R1.8 non-sequential frame was accepted"
    );
    ReplayConfig drifted = config;
    drifted.motionFps = 60U;
    Require(
        fresh->StepMotionFrameExact(1U, drifted) ==
            TimelineStatus::DeterminismViolation,
        "R1.8 config drift was accepted"
    );
    ReplayConfig loopDrift = config;
    loopDrift.loopMotion = true;
    Require(
        fresh->StepMotionFrameExact(1U, loopDrift) ==
            TimelineStatus::DeterminismViolation,
        "R1.8 loopMotion drift (false -> true) was accepted"
    );

    auto loopPrepared = CreateR18GenericRuntime(model);
    ReplayConfig loopingConfig{30U, 120U, 0U, true};
    Require(
        loopPrepared->PrepareFrameZero(loopingConfig) ==
            TimelineStatus::Ok,
        "R1.8 looping prepare failed"
    );
    Require(
        loopPrepared->StepMotionFrameExact(1U, config) ==
            TimelineStatus::DeterminismViolation,
        "R1.8 loopMotion drift (true -> false) was accepted"
    );
}

void TestR18GenericLoopAndClampSemantics()
{
    ModelAsset model("r18-generic-loop");
    ConfigureR18GenericTimelineAsset(model);

    // Looping: frame 30 is exactly one clip duration -> wraps to t=0.
    auto loopRuntime = CreateR18GenericRuntime(model);
    const ReplayConfig loopConfig{30U, 120U, 0U, true};
    Require(
        loopRuntime->PrepareFrameZero(loopConfig) == TimelineStatus::Ok,
        "R1.8 loop PrepareFrameZero failed"
    );
    for (MotionFrameIndex frame = 1U; frame <= 30U; ++frame)
    {
        Require(
            loopRuntime->StepMotionFrameExact(frame, loopConfig) ==
                TimelineStatus::Ok,
            "R1.8 loop exact step failed"
        );
    }
    Require(
        NearlyEqual(loopRuntime->TryGetAnimator()->Time(), 0.0f) &&
            NearlyEqual(
                loopRuntime->TryGetPose()->LocalMatrix(0U)[3].x,
                0.0f
            ),
        "R1.8 loop did not wrap at the clip boundary"
    );
    Require(
        loopRuntime->StepMotionFrameExact(31U, loopConfig) ==
            TimelineStatus::Ok,
        "R1.8 loop frame 31 failed"
    );
    Require(
        NearlyEqual(
            loopRuntime->TryGetPose()->LocalMatrix(0U)[3].x,
            2.0f / 30.0f
        ),
        "R1.8 loop did not continue after wrap"
    );

    // Non-looping clamp: after the clip end, time and pose stay fixed.
    auto clampRuntime = CreateR18GenericRuntime(model);
    const ReplayConfig clampConfig{30U, 120U, 0U, false};
    Require(
        clampRuntime->PrepareFrameZero(clampConfig) == TimelineStatus::Ok,
        "R1.8 clamp PrepareFrameZero failed"
    );
    for (MotionFrameIndex frame = 1U; frame <= 31U; ++frame)
    {
        Require(
            clampRuntime->StepMotionFrameExact(frame, clampConfig) ==
                TimelineStatus::Ok,
            "R1.8 clamp exact step failed"
        );
    }
    Require(
        NearlyEqual(clampRuntime->TryGetAnimator()->Time(), 1.0f) &&
            NearlyEqual(
                clampRuntime->TryGetPose()->LocalMatrix(0U)[3].x,
                2.0f
            ),
        "R1.8 non-looping clamp moved after the clip end"
    );
}

void TestR18GenericRootMotionCanonicalDelta()
{
    ModelAsset model("r18-generic-root-motion");
    ConfigureR18GenericTimelineAsset(model);
    auto runtime = CreateR18GenericRuntime(model);
    runtime->TryGetAnimator()->SetRootMotionBone(0U);
    runtime->TryGetAnimator()->SetRootMotionEnabled(true);

    const ReplayConfig config{30U, 120U, 0U, false};
    Require(
        runtime->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 root-motion PrepareFrameZero failed"
    );
    const RootMotionDelta frameZero = runtime->ConsumeRootMotion();
    Require(
        NearlyEqual(frameZero.translation.x, 0.0f) &&
            NearlyEqual(frameZero.translation.y, 0.0f) &&
            NearlyEqual(frameZero.translation.z, 0.0f),
        "R1.8 frame 0 root motion must be identity"
    );

    Require(
        runtime->StepMotionFrameExact(1U, config) == TimelineStatus::Ok,
        "R1.8 root-motion frame 1 failed"
    );
    const RootMotionDelta first = runtime->ConsumeRootMotion();
    Require(
        NearlyEqual(first.translation.x, 2.0f / 30.0f),
        "R1.8 canonical interval delta [0, 1/30] is wrong"
    );
    const RootMotionDelta secondConsume = runtime->ConsumeRootMotion();
    Require(
        NearlyEqual(secondConsume.translation.x, 0.0f),
        "R1.8 root motion must be consumed exactly once"
    );

    Require(
        runtime->StepMotionFrameExact(2U, config) == TimelineStatus::Ok,
        "R1.8 root-motion frame 2 failed"
    );
    const RootMotionDelta second = runtime->ConsumeRootMotion();
    Require(
        NearlyEqual(second.translation.x, 2.0f / 30.0f),
        "R1.8 canonical interval delta [1/30, 2/30] is wrong"
    );
}

void TestR18GenericUnsupportedDeterministicState()
{
    ModelAsset model("r18-generic-subset-gate");
    ConfigureR18GenericTimelineAsset(model);
    const ReplayConfig config{30U, 120U, 0U, false};

    const auto expectRejected =
        [&](const std::function<void(WisteriaGenericRuntimeDriver&)>& mutate)
    {
        auto runtime = CreateR18GenericRuntime(model);
        Require(
            runtime->PrepareFrameZero(config) == TimelineStatus::Ok,
            "R1.8 subset-gate prepare failed"
        );
        mutate(*runtime);
        Require(
            runtime->StepMotionFrameExact(1U, config) ==
                TimelineStatus::UnsupportedDeterministicState,
            "R1.8 out-of-subset animator state was accepted"
        );
    };

    expectRejected([](WisteriaGenericRuntimeDriver& runtime)
    {
        runtime.TryGetAnimator()->Pause();
    });
    expectRejected([](WisteriaGenericRuntimeDriver& runtime)
    {
        runtime.TryGetAnimator()->SetSpeed(2.0f);
    });
    expectRejected([](WisteriaGenericRuntimeDriver& runtime)
    {
        runtime.TryGetAnimator()->SetFloat("param", 1.0f);
    });
    expectRejected([](WisteriaGenericRuntimeDriver& runtime)
    {
        runtime.TryGetAnimator()->SetTrigger("trigger");
    });
    expectRejected([&model](WisteriaGenericRuntimeDriver& runtime)
    {
        runtime.TryGetAnimator()->GetStateMachine().AddState(
            AnimationState{
                .name = "state",
                .clip = &model.AnimationClipAt(0U),
                .speed = 1.0f,
                .looping = true
            }
        );
    });
    expectRejected([&model](WisteriaGenericRuntimeDriver& runtime)
    {
        runtime.TryGetAnimator()->CrossFade(
            model.AnimationClipAt(1U),
            0.5f
        );
    });
}

void TestR18GenericCapabilityAdvertisement()
{
    ModelAsset model("r18-generic-capabilities");
    ConfigureR18GenericTimelineAsset(model);
    auto runtime = CreateR18GenericRuntime(model);
    const ModelRuntimeCapabilities capabilities = runtime->Capabilities();
    Require(
        capabilities.deterministic.supportsExactFrameStepping,
        "R1.8 generic timeline must advertise exact stepping"
    );
    Require(
        capabilities.deterministic.supportsCheckpointCapture &&
            capabilities.deterministic.supportsCheckpointRestore &&
            capabilities.deterministic.supportsReplayFromCheckpoint,
        "R1.8 Phase 0C generic checkpoint capabilities must be open"
    );
    Require(
        capabilities.checkpoint.supportsCheckpointCapture ==
                capabilities.deterministic.supportsCheckpointCapture &&
            capabilities.checkpoint.supportsCheckpointRestore ==
                capabilities.deterministic.supportsCheckpointRestore &&
            capabilities.checkpoint.supportsReplayFromCheckpoint ==
                capabilities.deterministic.supportsReplayFromCheckpoint,
        "R1.8 checkpoint mirror diverged from deterministic source"
    );

    ModelAsset plain("r18-generic-plain");
    plain.SetBackendKind(ModelBackendKind::WisteriaGeneric);
    WisteriaGenericRuntimeDriver plainRuntime(plain);
    Require(
        !plainRuntime.Initialize(),
        "R1.8 plain generic asset initialized"
    );
    Require(
        !plainRuntime.Capabilities().deterministic.supportsExactFrameStepping,
        "R1.8 no-timeline generic advertised exact stepping"
    );
}

void TestR18GenericCheckpointRoundTripAndReplay()
{
    ModelAsset model("r18-generic-checkpoint");
    ConfigureR18GenericTimelineAsset(model);
    const ReplayConfig config{30U, 120U, 0U, false};

    // Source: root motion + morph override, prepare, step to frame 15.
    auto source = CreateR18GenericRuntime(model);
    source->TryGetAnimator()->SetRootMotionBone(0U);
    source->TryGetAnimator()->SetRootMotionEnabled(true);
    Require(
        source->SetMorphOverride("blink", 0.5f),
        "R1.8 source morph override was rejected"
    );
    Require(
        source->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 checkpoint source prepare failed"
    );
    for (MotionFrameIndex frame = 1U; frame <= 15U; ++frame)
    {
        Require(
            source->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "R1.8 checkpoint source step failed"
        );
    }

    GenericRuntimeCheckpoint checkpoint;
    Require(
        source->CreateCheckpoint(checkpoint) == TimelineStatus::Ok,
        "R1.8 CreateCheckpoint failed"
    );
    Require(
        checkpoint.frame == 15U &&
            checkpoint.activeClipIndex.has_value() &&
            *checkpoint.activeClipIndex == 0U &&
            checkpoint.rootMotionEnabled &&
            checkpoint.rootMotionBoneIndex.has_value() &&
            *checkpoint.rootMotionBoneIndex == 0U &&
            checkpoint.morphOverrides.size() == 1U &&
            checkpoint.morphOverrides[0].first == "blink" &&
            NearlyEqual(checkpoint.morphOverrides[0].second, 0.5f) &&
            checkpoint.assetFingerprint != 0U,
        "R1.8 checkpoint payload fields are incomplete"
    );

    // Wire round trip.
    const std::vector<std::uint8_t> wire =
        SerializeGenericCheckpoint(checkpoint);
    GenericRuntimeCheckpoint decoded;
    Require(
        DeserializeGenericCheckpoint(
            wire.data(),
            wire.size(),
            {},
            decoded
        ) == TimelineStatus::Ok,
        "R1.8 generic checkpoint wire round trip failed"
    );
    Require(
        decoded.frame == checkpoint.frame &&
            NearlyEqual(
                decoded.canonicalTime,
                checkpoint.canonicalTime
            ) &&
            decoded.morphOverrides == checkpoint.morphOverrides &&
            decoded.assetFingerprint == checkpoint.assetFingerprint,
        "R1.8 decoded checkpoint diverged from source"
    );

    // Reference: from-start to frame 30.
    auto reference = CreateR18GenericRuntime(model);
    reference->TryGetAnimator()->SetRootMotionBone(0U);
    reference->TryGetAnimator()->SetRootMotionEnabled(true);
    Require(
        reference->SetMorphOverride("blink", 0.5f),
        "R1.8 reference morph override was rejected"
    );
    Require(
        reference->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 reference prepare failed"
    );
    for (MotionFrameIndex frame = 1U; frame <= 30U; ++frame)
    {
        Require(
            reference->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "R1.8 reference step failed"
        );
    }

    // Restore + continue.
    auto restored = CreateR18GenericRuntime(model);
    Require(
        restored->RestoreCheckpoint(decoded) == TimelineStatus::Ok,
        "R1.8 RestoreCheckpoint failed"
    );
    for (MotionFrameIndex frame = 16U; frame <= 30U; ++frame)
    {
        Require(
            restored->StepMotionFrameExact(frame, config) ==
                TimelineStatus::Ok,
            "R1.8 restored step failed"
        );
    }
    Require(
        restored->TryGetPose()->LocalMatrix(0U) ==
            reference->TryGetPose()->LocalMatrix(0U),
        "R1.8 restore+continue pose diverged from from-start"
    );
    Require(
        NearlyEqual(
            restored->TryGetAnimator()->Time(),
            reference->TryGetAnimator()->Time()
        ) &&
            NearlyEqual(
                restored->MorphWeight("blink").value_or(-1.0f),
                reference->MorphWeight("blink").value_or(-2.0f)
            ),
        "R1.8 restore+continue time/morph diverged"
    );
    const RootMotionDelta restoredDelta = restored->ConsumeRootMotion();
    const RootMotionDelta referenceDelta = reference->ConsumeRootMotion();
    Require(
        NearlyEqual(
            restoredDelta.translation.x,
            referenceDelta.translation.x
        ) &&
            NearlyEqual(
                restoredDelta.translation.y,
                referenceDelta.translation.y
            ) &&
            NearlyEqual(
                restoredDelta.translation.z,
                referenceDelta.translation.z
            ),
        "R1.8 restore+continue root delta diverged"
    );

    // ReplayFromCheckpoint to the same target.
    auto replayed = CreateR18GenericRuntime(model);
    Require(
        replayed->ReplayFromCheckpoint(decoded, 30U) == TimelineStatus::Ok,
        "R1.8 ReplayFromCheckpoint failed"
    );
    Require(
        replayed->TryGetPose()->LocalMatrix(0U) ==
            reference->TryGetPose()->LocalMatrix(0U),
        "R1.8 replay pose diverged from from-start"
    );

    // Capture requires a prepared deterministic state.
    auto unprepared = CreateR18GenericRuntime(model);
    GenericRuntimeCheckpoint unused;
    Require(
        unprepared->CreateCheckpoint(unused) ==
            TimelineStatus::InvalidState,
        "R1.8 checkpoint capture before prepare was accepted"
    );
}

void TestR18GenericCheckpointRejections()
{
    ModelAsset model("r18-generic-checkpoint-reject");
    ConfigureR18GenericTimelineAsset(model);
    const ReplayConfig config{30U, 120U, 0U, false};

    auto runtime = CreateR18GenericRuntime(model);
    Require(
        runtime->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 rejection prepare failed"
    );
    runtime->StepMotionFrameExact(1U, config);
    GenericRuntimeCheckpoint checkpoint;
    Require(
        runtime->CreateCheckpoint(checkpoint) == TimelineStatus::Ok,
        "R1.8 rejection checkpoint capture failed"
    );

    // Wire tampering: flip one payload byte.
    std::vector<std::uint8_t> wire = SerializeGenericCheckpoint(checkpoint);
    wire[CheckpointWireHeaderSize] ^= 0xFFU;
    GenericRuntimeCheckpoint output;
    Require(
        DeserializeGenericCheckpoint(
            wire.data(),
            wire.size(),
            {},
            output
        ) == TimelineStatus::InvalidCheckpoint,
        "R1.8 tampered wire was accepted"
    );

    // Truncation.
    Require(
        DeserializeGenericCheckpoint(
            wire.data(),
            wire.size() - 1U,
            {},
            output
        ) == TimelineStatus::InvalidCheckpoint,
        "R1.8 truncated wire was accepted"
    );

    // Build compatibility mismatch.
    CheckpointSerializationOptions writerOptions;
    writerOptions.buildCompatibilityIdOverride = 12345U;
    const std::vector<std::uint8_t> foreignWire =
        SerializeGenericCheckpoint(checkpoint, writerOptions);
    Require(
        DeserializeGenericCheckpoint(
            foreignWire.data(),
            foreignWire.size(),
            {},
            output
        ) == TimelineStatus::InvalidCheckpoint,
        "R1.8 foreign build identity was accepted"
    );

    // Semantic restore rejections.
    GenericRuntimeCheckpoint invalid = checkpoint;
    invalid.activeClipIndex = 99U;
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 invalid clip index was accepted"
    );
    invalid = checkpoint;
    invalid.assetFingerprint = 7U;
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 wrong asset fingerprint was accepted"
    );
    invalid = checkpoint;
    invalid.frame = 1U << 21;
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 out-of-domain frame was accepted"
    );
    invalid = checkpoint;
    invalid.canonicalTime += 0.5f;
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 mismatched canonical time was accepted"
    );
    invalid = checkpoint;
    invalid.morphOverrides = {{"unknown", 0.5f}};
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 unknown morph override was accepted"
    );
    invalid = checkpoint;
    invalid.canonicalTime = std::numeric_limits<float>::quiet_NaN();
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 NaN canonical time was accepted in-memory"
    );
    invalid = checkpoint;
    invalid.playing = false;
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 paused (playing=false) checkpoint was accepted"
    );
    invalid = checkpoint;
    invalid.clipClamped = !invalid.clipClamped;
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 inconsistent clipClamped state was accepted"
    );
    invalid = checkpoint;
    invalid.pendingRootMotion.translation.x =
        std::numeric_limits<float>::quiet_NaN();
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 NaN pending root delta was accepted in-memory"
    );
    invalid = checkpoint;
    invalid.pendingRootMotion.rotation.x = 5.0f;
    Require(
        runtime->RestoreCheckpoint(invalid) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 degenerate root rotation was accepted"
    );

    // Root bone selection must survive restore even when root motion is
    // currently disabled.
    auto boneRuntime = CreateR18GenericRuntime(model);
    GenericRuntimeCheckpoint boneConfig = checkpoint;
    boneConfig.rootMotionEnabled = false;
    boneConfig.rootMotionBoneIndex = 0U;
    Require(
        boneRuntime->RestoreCheckpoint(boneConfig) == TimelineStatus::Ok,
        "R1.8 disabled-root-motion checkpoint restore failed"
    );
    Require(
        boneRuntime->TryGetAnimator()->RootMotionBone().has_value() &&
            *boneRuntime->TryGetAnimator()->RootMotionBone() == 0U &&
            !boneRuntime->TryGetAnimator()->IsRootMotionEnabled(),
        "R1.8 root bone selection was lost by restore"
    );

    // Capture while out of subset must fail explicitly.
    auto transient = CreateR18GenericRuntime(model);
    Require(
        transient->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 transient prepare failed"
    );
    transient->TryGetAnimator()->Pause();
    Require(
        transient->CreateCheckpoint(output) ==
            TimelineStatus::UnsupportedDeterministicState,
        "R1.8 out-of-subset capture was accepted"
    );
}

void TestR18GenericFingerprintSemantics()
{
    // Two assets with identical metadata (name / duration / track count)
    // but different animation key data must have different fingerprints.
    ModelAsset modelA("r18-fingerprint-a");
    ConfigureR18GenericTimelineAsset(modelA, 2.0f);
    ModelAsset modelB("r18-fingerprint-b");
    ConfigureR18GenericTimelineAsset(modelB, 1.0f);

    auto runtimeA = CreateR18GenericRuntime(modelA);
    const ReplayConfig config{30U, 120U, 0U, false};
    Require(
        runtimeA->PrepareFrameZero(config) == TimelineStatus::Ok,
        "R1.8 fingerprint prepare failed"
    );
    Require(
        runtimeA->StepMotionFrameExact(1U, config) == TimelineStatus::Ok,
        "R1.8 fingerprint step failed"
    );
    GenericRuntimeCheckpoint checkpoint;
    Require(
        runtimeA->CreateCheckpoint(checkpoint) == TimelineStatus::Ok,
        "R1.8 fingerprint capture failed"
    );

    auto runtimeB = CreateR18GenericRuntime(modelB);
    Require(
        runtimeB->RestoreCheckpoint(checkpoint) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 fingerprint did not distinguish animation key data"
    );

    // Same skeleton/clip/keys, different base mesh geometry.
    const auto makePartData = [](std::size_t vertexCount)
    {
        DefaultModelData data;
        data.layout = {{"position", 3, FLOAT}};
        for (std::size_t index = 0U; index < vertexCount; ++index)
        {
            data.vertices.push_back(
                static_cast<float>(index) * 0.5f
            );
            data.vertices.push_back(0.0f);
            data.vertices.push_back(0.0f);
        }
        for (std::size_t index = 0U; index + 2U < vertexCount; ++index)
        {
            data.indices.push_back(
                static_cast<std::uint32_t>(index)
            );
            data.indices.push_back(
                static_cast<std::uint32_t>(index + 1U)
            );
            data.indices.push_back(
                static_cast<std::uint32_t>(index + 2U)
            );
        }
        return data;
    };

    ModelAsset meshModelA("r18-fp-mesh-a");
    ConfigureR18GenericTimelineAsset(meshModelA);
    Mesh triangleMesh(makePartData(3U));
    Material sharedMaterial(MaterialData{});
    meshModelA.AddPart(triangleMesh, sharedMaterial);

    ModelAsset meshModelB("r18-fp-mesh-b");
    ConfigureR18GenericTimelineAsset(meshModelB);
    Mesh quadMesh(makePartData(4U));
    meshModelB.AddPart(quadMesh, sharedMaterial);

    auto meshRuntimeA = CreateR18GenericRuntime(meshModelA);
    Require(
        meshRuntimeA->PrepareFrameZero(config) == TimelineStatus::Ok &&
            meshRuntimeA->StepMotionFrameExact(1U, config) ==
                TimelineStatus::Ok,
        "R1.8 mesh-fingerprint prepare/step failed"
    );
    GenericRuntimeCheckpoint meshCheckpoint;
    Require(
        meshRuntimeA->CreateCheckpoint(meshCheckpoint) ==
            TimelineStatus::Ok,
        "R1.8 mesh-fingerprint capture failed"
    );
    auto meshRuntimeB = CreateR18GenericRuntime(meshModelB);
    Require(
        meshRuntimeB->RestoreCheckpoint(meshCheckpoint) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 fingerprint did not distinguish base mesh geometry"
    );

    // Same everything except vertex/UV morph offsets.
    MeshMorphTarget vertexTargetA;
    vertexTargetA.morphIndex = 0U;
    vertexTargetA.offsets.push_back(VertexMorphOffset{
        .vertexIndex = 0U,
        .offset = glm::vec3(1.0f, 0.0f, 0.0f)
    });
    MeshMorphTarget vertexTargetB = vertexTargetA;
    vertexTargetB.offsets[0].offset = glm::vec3(2.0f, 0.0f, 0.0f);

    ModelAsset morphModelA("r18-fp-morph-a");
    ConfigureR18GenericTimelineAsset(morphModelA);
    Mesh morphMeshA(makePartData(3U), 0U, {vertexTargetA});
    morphModelA.AddPart(morphMeshA, sharedMaterial);

    ModelAsset morphModelB("r18-fp-morph-b");
    ConfigureR18GenericTimelineAsset(morphModelB);
    Mesh morphMeshB(makePartData(3U), 0U, {vertexTargetB});
    morphModelB.AddPart(morphMeshB, sharedMaterial);

    auto morphRuntimeA = CreateR18GenericRuntime(morphModelA);
    Require(
        morphRuntimeA->PrepareFrameZero(config) == TimelineStatus::Ok &&
            morphRuntimeA->StepMotionFrameExact(1U, config) ==
                TimelineStatus::Ok,
        "R1.8 morph-fingerprint prepare/step failed"
    );
    GenericRuntimeCheckpoint morphCheckpoint;
    Require(
        morphRuntimeA->CreateCheckpoint(morphCheckpoint) ==
            TimelineStatus::Ok,
        "R1.8 morph-fingerprint capture failed"
    );
    auto morphRuntimeB = CreateR18GenericRuntime(morphModelB);
    Require(
        morphRuntimeB->RestoreCheckpoint(morphCheckpoint) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 fingerprint did not distinguish vertex morph offsets"
    );

    MeshMorphTarget uvTargetA;
    uvTargetA.morphIndex = 0U;
    uvTargetA.uvOffsets.push_back(UvMorphOffset{
        .vertexIndex = 0U,
        .channel = 0U,
        .offset = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)
    });
    MeshMorphTarget uvTargetB = uvTargetA;
    uvTargetB.uvOffsets[0].offset =
        glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);

    ModelAsset uvModelA("r18-fp-uv-a");
    ConfigureR18GenericTimelineAsset(uvModelA);
    Mesh uvMeshA(makePartData(3U), 0U, {uvTargetA});
    uvModelA.AddPart(uvMeshA, sharedMaterial);

    ModelAsset uvModelB("r18-fp-uv-b");
    ConfigureR18GenericTimelineAsset(uvModelB);
    Mesh uvMeshB(makePartData(3U), 0U, {uvTargetB});
    uvModelB.AddPart(uvMeshB, sharedMaterial);

    auto uvRuntimeA = CreateR18GenericRuntime(uvModelA);
    Require(
        uvRuntimeA->PrepareFrameZero(config) == TimelineStatus::Ok &&
            uvRuntimeA->StepMotionFrameExact(1U, config) ==
                TimelineStatus::Ok,
        "R1.8 uv-fingerprint prepare/step failed"
    );
    GenericRuntimeCheckpoint uvCheckpoint;
    Require(
        uvRuntimeA->CreateCheckpoint(uvCheckpoint) ==
            TimelineStatus::Ok,
        "R1.8 uv-fingerprint capture failed"
    );
    auto uvRuntimeB = CreateR18GenericRuntime(uvModelB);
    Require(
        uvRuntimeB->RestoreCheckpoint(uvCheckpoint) ==
            TimelineStatus::InvalidCheckpoint,
        "R1.8 fingerprint did not distinguish UV morph offsets"
    );
}

void TestR18SequenceRootMotionBoundary()
{
    ModelAsset model("r18-sequence-root-motion");
    ConfigureR18GenericTimelineAsset(model);
    Scene scene;
    Renderer renderer;
    OfflineFrameSequenceConfig config;
    config.outputDirectory =
        std::filesystem::temp_directory_path() /
        "wisteria_r18_root_motion";
    config.writePng = true;
    config.writeRaw = false;

    // Enabled at construction: ctor must reject.
    auto enabledRuntime = CreateR18GenericRuntime(model);
    enabledRuntime->TryGetAnimator()->SetRootMotionBone(0U);
    enabledRuntime->TryGetAnimator()->SetRootMotionEnabled(true);
    ModelInstance enabledInstance(model, std::move(enabledRuntime));
    bool ctorRejected = false;
    try
    {
        OfflineFrameSequence sequence(
            scene,
            renderer,
            *enabledInstance.TryGetRuntime(),
            enabledInstance,
            config
        );
    }
    catch (const std::invalid_argument&)
    {
        ctorRejected = true;
    }
    Require(
        ctorRejected,
        "R1.8 sequence accepted enabled root motion at construction"
    );

    // Enabled after construction: RenderRange must fail-stop.
    auto lateRuntime = CreateR18GenericRuntime(model);
    ModelInstance lateInstance(model, std::move(lateRuntime));
    OfflineFrameSequence sequence(
        scene,
        renderer,
        *lateInstance.TryGetRuntime(),
        lateInstance,
        config
    );
    auto* generic = dynamic_cast<WisteriaGenericRuntimeDriver*>(
        lateInstance.TryGetRuntime()
    );
    generic->TryGetAnimator()->SetRootMotionBone(0U);
    generic->TryGetAnimator()->SetRootMotionEnabled(true);
    bool rangeRejected = false;
    try
    {
        sequence.RenderRange(0U, 2U);
    }
    catch (const std::runtime_error&)
    {
        rangeRejected = true;
    }
    Require(
        rangeRejected,
        "R1.8 sequence accepted root motion during RenderRange"
    );
}

void TestR18SequenceBackendNeutralGate()
{
    // R1.8 Phase 0D: OfflineFrameSequence accepts IModelRuntimeDriver and
    // gates on deterministic capability + checkpoint surface.
    ModelAsset timelineModel("r18-sequence-neutral");
    ConfigureR18GenericTimelineAsset(timelineModel);
    auto timelineRuntime = CreateR18GenericRuntime(timelineModel);
    ModelInstance timelineInstance(
        timelineModel,
        std::move(timelineRuntime)
    );
    Scene scene;
    Renderer renderer;
    OfflineFrameSequenceConfig config;
    config.outputDirectory =
        std::filesystem::temp_directory_path() / "wisteria_r18_gate";
    config.writePng = true;
    config.writeRaw = false;
    bool accepted = true;
    try
    {
        OfflineFrameSequence sequence(
            scene,
            renderer,
            *timelineInstance.TryGetRuntime(),
            timelineInstance,
            config
        );
    }
    catch (const std::exception&)
    {
        accepted = false;
    }
    Require(
        accepted,
        "R1.8 generic deterministic runtime was rejected by the sequence"
    );

    ModelAsset plainModel("r18-sequence-plain");
    plainModel.SetBackendKind(ModelBackendKind::WisteriaGeneric);
    auto plainRuntime =
        std::make_unique<WisteriaGenericRuntimeDriver>(plainModel);
    (void)plainRuntime->Initialize();
    ModelInstance plainInstance(plainModel, std::move(plainRuntime));
    bool rejected = false;
    try
    {
        OfflineFrameSequence sequence(
            scene,
            renderer,
            *plainInstance.TryGetRuntime(),
            plainInstance,
            config
        );
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(
        rejected,
        "R1.8 non-deterministic runtime passed the sequence capability gate"
    );
}

int main()
{
    int failures = 0;
    failures += !RunTest(
        "Generic PhysicsInstance lifecycle",
        TestGenericPhysicsInstanceLifecycle
    );
    failures += !RunTest(
        "Mesh dynamic vertex upload",
        TestMeshDynamicUpload
    );
    failures += !RunTest(
        "Scene skips self-stepping physics",
        TestSceneOwnsSimulationStep
    );
    failures += !RunTest(
        "Camera/Light track sampling",
        TestCameraLightTrackSampling
    );
    failures += !RunTest("RenderPart and ModelAsset", TestRenderPartAndModelAsset);
    failures += !RunTest("Built-in cube tangents", TestBuiltInCubeTangents);
    failures += !RunTest("Mesh bounds center", TestMeshBoundsCenter);
    failures += !RunTest("Model instantiation", TestModelInstantiation);
    failures += !RunTest(
        "R1.5 procedural vertex canary",
        TestProceduralVertexCanary
    );
    failures += !RunTest(
        "R1.5 procedural one-bone canary",
        TestProceduralOneBoneCanary
    );
    failures += !RunTest(
        "R1.5 procedural root motion exactly once",
        TestProceduralRootMotionExactlyOnce
    );
    failures += !RunTest(
        "R1.5 procedural malformed geometry rejected",
        TestProceduralMalformedGeometryRejected
    );
    failures += !RunTest(
        "R1.5 procedural malformed render channels rejected",
        TestProceduralMalformedRenderChannelsRejected
    );
    failures += !RunTest(
        "R1.5 runtime suppresses legacy state",
        TestRuntimeSuppressesLegacyState
    );
    failures += !RunTest(
        "R1.6 PNG encoder round trip",
        TestPngEncoderRoundTrip
    );
    failures += !RunTest(
        "R1.6 BMP writer orientation",
        TestBmpWriterOrientation
    );
    failures += !RunTest("Frame-rate independent behaviours", TestFrameRateIndependentBehaviours);
    failures += !RunTest("Input frame transitions", TestInputFrameTransitions);
    failures += !RunTest("Free camera controller", TestFreeCameraController);
    failures += !RunTest("ResourceManager model registry", TestResourceManagerModelRegistry);
    failures += !RunTest(
        "Environment resource and Scene binding",
        TestEnvironmentResourceAndSceneBinding
    );
    failures += !RunTest(
        "R1.8 generic PrepareFrameZero + exact step",
        TestR18GenericPrepareFrameZeroAndExactStep
    );
    failures += !RunTest(
        "R1.8 generic loop and clamp semantics",
        TestR18GenericLoopAndClampSemantics
    );
    failures += !RunTest(
        "R1.8 generic root-motion canonical delta",
        TestR18GenericRootMotionCanonicalDelta
    );
    failures += !RunTest(
        "R1.8 generic unsupported deterministic state",
        TestR18GenericUnsupportedDeterministicState
    );
    failures += !RunTest(
        "R1.8 generic capability advertisement",
        TestR18GenericCapabilityAdvertisement
    );
    failures += !RunTest(
        "R1.8 generic checkpoint round trip + replay",
        TestR18GenericCheckpointRoundTripAndReplay
    );
    failures += !RunTest(
        "R1.8 generic checkpoint rejections",
        TestR18GenericCheckpointRejections
    );
    failures += !RunTest(
        "R1.8 sequence backend-neutral gate",
        TestR18SequenceBackendNeutralGate
    );
    failures += !RunTest(
        "R1.8 generic fingerprint semantics",
        TestR18GenericFingerprintSemantics
    );
    failures += !RunTest(
        "R1.8 sequence root-motion boundary",
        TestR18SequenceRootMotionBoundary
    );
    return failures == 0 ? 0 : 1;
}
