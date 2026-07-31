#include "pch.hpp"
#include "renderer.hpp"
#include "shader.hpp"
#include <algorithm>

void Renderer::Render(Scene& scene, const glm::mat4& projection)
{
    const Camera& camera = scene.ActiveCamera();
    const glm::mat4 view = camera.GetView();

    for (const std::unique_ptr<Entity>& entityPointer : scene.Entities())
    {
        Entity& entity = *entityPointer;
        if (!entity.IsVisible())
            continue;

        Mesh& mesh = entity.GetMesh();
        Material& material = entity.GetMaterial();
        mesh.Attach();
        material.Attach();
        material.Bind();

        Program& program = material.GetProgram();
        program.UniformMat4f("model", entity.GetTransform().Matrix());
        program.UniformMat4f("view", view);
        program.UniformMat4f("projection", projection);
        program.Uniform3f(
            "cameraPosition",
            camera.GetParam().Position.x,
            camera.GetParam().Position.y,
            camera.GetParam().Position.z
        );
        program.Uniform3f(
            "materialSpecularColor",
            material.SpecularColor().x,
            material.SpecularColor().y,
            material.SpecularColor().z
        );
        program.Uniform1f("materialShininess", material.Shininess());
        this->UploadSceneUniforms(program, scene);

        mesh.Bind();
        mesh.Draw();
        mesh.Unbind();
        material.Unbind();
    }
}

void Renderer::UploadSceneUniforms(Program& program, const Scene& scene)
{
    program.Uniform1f("ambientStrength", 0.15f);
    this->UploadPointLights(program, scene);
    this->UploadDirectionalLights(program, scene);
    this->UploadSpotLights(program, scene);
}

void Renderer::UploadPointLights(Program& program, const Scene& scene)
{
    const int count = static_cast<int>(std::min(
        scene.PointLights().size(),
        MaxPointLights
    ));
    program.Uniform1i("pointLightCount", count);

    for (int index = 0; index < count; ++index)
    {
        const PointLight& light = *scene.PointLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            "pointLights[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + "position",
            light.Position().x,
            light.Position().y,
            light.Position().z
        );
        program.Uniform3f(
            uniformPrefix + "radiance",
            radiance.x,
            radiance.y,
            radiance.z
        );
        program.Uniform1f(uniformPrefix + "range", light.Range());
        program.Uniform1f(uniformPrefix + "constant", light.Constant());
        program.Uniform1f(uniformPrefix + "linear", light.Linear());
        program.Uniform1f(uniformPrefix + "quadratic", light.Quadratic());
    }
}

void Renderer::UploadDirectionalLights(Program& program, const Scene& scene)
{
    const int count = static_cast<int>(std::min(
        scene.DirectionalLights().size(),
        MaxDirectionalLights
    ));
    program.Uniform1i("directionalLightCount", count);

    for (int index = 0; index < count; ++index)
    {
        const DirectionalLight& light = *scene.DirectionalLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            "directionalLights[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + "direction",
            light.Direction().x,
            light.Direction().y,
            light.Direction().z
        );
        program.Uniform3f(
            uniformPrefix + "radiance",
            radiance.x,
            radiance.y,
            radiance.z
        );
    }
}

void Renderer::UploadSpotLights(Program& program, const Scene& scene)
{
    const int count = static_cast<int>(std::min(
        scene.SpotLights().size(),
        MaxSpotLights
    ));
    program.Uniform1i("spotLightCount", count);

    for (int index = 0; index < count; ++index)
    {
        const SpotLight& light = *scene.SpotLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            "spotLights[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + "position",
            light.Position().x,
            light.Position().y,
            light.Position().z
        );
        program.Uniform3f(
            uniformPrefix + "direction",
            light.Direction().x,
            light.Direction().y,
            light.Direction().z
        );
        program.Uniform3f(
            uniformPrefix + "radiance",
            radiance.x,
            radiance.y,
            radiance.z
        );
        program.Uniform1f(uniformPrefix + "range", light.Range());
        program.Uniform1f(uniformPrefix + "constant", light.Constant());
        program.Uniform1f(uniformPrefix + "linear", light.Linear());
        program.Uniform1f(uniformPrefix + "quadratic", light.Quadratic());
        program.Uniform1f(
            uniformPrefix + "innerCutoff",
            light.InnerCutoffCos()
        );
        program.Uniform1f(
            uniformPrefix + "outerCutoff",
            light.OuterCutoffCos()
        );
    }
}
