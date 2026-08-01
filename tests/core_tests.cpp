#include "behaviour.hpp"
#include "Models/cube.hpp"
#include "entity.hpp"
#include "importer.hpp"
#include "input.hpp"
#include "manager.hpp"
#include "model_asset.hpp"
#include "scene.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
constexpr float Epsilon = 0.0001f;
const std::filesystem::path TestAssetDirectory = WISTERIA_TEST_ASSET_DIR;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool NearlyEqual(float left, float right)
{
    return std::abs(left - right) <= Epsilon;
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
    model.AddPart(mesh, material, localTransform);

    Require(model.Name() == "testModel", "ModelAsset name was not preserved");
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

template<typename Function>
bool RunTest(const char* name, Function&& function)
{
    try
    {
        function();
        std::cout << "[PASS] " << name << '\n';
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        return false;
    }
}
}

int main()
{
    int failures = 0;
    failures += !RunTest("RenderPart and ModelAsset", TestRenderPartAndModelAsset);
    failures += !RunTest("Built-in cube tangents", TestBuiltInCubeTangents);
    failures += !RunTest("Model instantiation", TestModelInstantiation);
    failures += !RunTest("Frame-rate independent behaviours", TestFrameRateIndependentBehaviours);
    failures += !RunTest("Input frame transitions", TestInputFrameTransitions);
    failures += !RunTest("Free camera controller", TestFreeCameraController);
    failures += !RunTest("ResourceManager model registry", TestResourceManagerModelRegistry);
    failures += !RunTest(
        "Environment resource and Scene binding",
        TestEnvironmentResourceAndSceneBinding
    );
    failures += !RunTest("Static model importer", TestStaticModelImporter);
    failures += !RunTest("Imported resource creation", TestImportedResourceCreation);
    failures += !RunTest("Importer missing-file rejection", TestImporterRejectsMissingFile);
    failures += !RunTest(
        "Transactional imported resource creation",
        TestImportResourceCollisionIsTransactional
    );
    failures += !RunTest("Converted MMD GLB integration", TestConvertedMmdGlbWhenAvailable);
    failures += !RunTest("Converted MMD OBJ integration", TestConvertedMmdObjWhenAvailable);
    return failures == 0 ? 0 : 1;
}
