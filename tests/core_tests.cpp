#include "behaviour.hpp"
#include "entity.hpp"
#include "manager.hpp"
#include "model_asset.hpp"
#include "scene.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
constexpr float Epsilon = 0.0001f;

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
    failures += !RunTest("Model instantiation", TestModelInstantiation);
    failures += !RunTest("Frame-rate independent behaviours", TestFrameRateIndependentBehaviours);
    failures += !RunTest("ResourceManager model registry", TestResourceManagerModelRegistry);
    return failures == 0 ? 0 : 1;
}
