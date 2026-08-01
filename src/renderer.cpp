#include "pch.hpp"
#include "renderer.hpp"
#include "shader.hpp"
#include "environment.hpp"
#include <algorithm>
#include <limits>

namespace
{
constexpr unsigned int IrradianceTextureUnit = 8;
constexpr unsigned int PrefilterTextureUnit = 9;
constexpr unsigned int BrdfLutTextureUnit = 10;

int LightCount(std::size_t available, std::size_t capacity)
{
    const std::size_t count = std::min({
        available,
        capacity,
        static_cast<std::size_t>(std::numeric_limits<int>::max())
    });
    return static_cast<int>(count);
}
}

void Renderer::Render(Scene& scene, const glm::mat4& projection)
{
    const Camera& camera = scene.ActiveCamera();
    const glm::mat4 view = camera.GetView();
    EnvironmentMap* environment = scene.Environment();
    if (environment != nullptr)
        environment->Attach();

    for (const std::unique_ptr<Entity>& entityPointer : scene.Entities())
    {
        Entity& entity = *entityPointer;
        if (!entity.IsVisible())
            continue;

        const glm::mat4 entityTransform = entity.GetTransform().Matrix();
        for (RenderPart& part : entity.RenderParts())
        {
            Mesh& mesh = part.GetMesh();
            Material& material = part.GetMaterial();
            mesh.Attach();
            material.Attach();

            if (material.AlphaMode() == MaterialAlphaMode::Blend)
                glEnable(GL_BLEND);
            else
                glDisable(GL_BLEND);

            if (material.IsDoubleSided())
                glDisable(GL_CULL_FACE);
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }

            material.Bind();

            Program& program = material.GetProgram();
            const ShaderInterface& shaderInterface = material.Interface();
            const glm::mat4 model =
                entityTransform * part.LocalTransform();
            this->UploadTransforms(
                program,
                shaderInterface,
                model,
                view,
                projection
            );

            const glm::vec4& baseColor = material.BaseColorFactor();
            program.Uniform4f(
                shaderInterface.materialBaseColorFactor,
                baseColor.r,
                baseColor.g,
                baseColor.b,
                baseColor.a
            );
            program.Uniform1i(
                shaderInterface.materialAlphaMode,
                static_cast<int>(material.AlphaMode())
            );
            program.Uniform1f(
                shaderInterface.materialAlphaCutoff,
                material.AlphaCutoff()
            );
            program.Uniform1i(
                shaderInterface.hasBaseTexture,
                material.HasTexture(shaderInterface.baseColorTexture) ? 1 : 0
            );
            program.Uniform1i(
                shaderInterface.hasNormalTexture,
                material.HasTexture(shaderInterface.normalTexture) ? 1 : 0
            );
            program.Uniform1f(
                shaderInterface.materialNormalScale,
                material.NormalScale()
            );
            program.Uniform1f(
                shaderInterface.materialMetallicFactor,
                material.MetallicFactor()
            );
            program.Uniform1f(
                shaderInterface.materialRoughnessFactor,
                material.RoughnessFactor()
            );
            program.Uniform3f(
                shaderInterface.materialEmissiveFactor,
                material.EmissiveFactor().r,
                material.EmissiveFactor().g,
                material.EmissiveFactor().b
            );
            program.Uniform1f(
                shaderInterface.materialOcclusionStrength,
                material.OcclusionStrength()
            );
            program.Uniform1i(
                shaderInterface.hasMetallicRoughnessTexture,
                material.HasTexture(
                    shaderInterface.metallicRoughnessTexture
                ) ? 1 : 0
            );
            program.Uniform1i(
                shaderInterface.hasEmissiveTexture,
                material.HasTexture(shaderInterface.emissiveTexture) ? 1 : 0
            );
            program.Uniform1i(
                shaderInterface.hasOcclusionTexture,
                material.HasTexture(shaderInterface.occlusionTexture) ? 1 : 0
            );

            if (shaderInterface.lightingEnabled)
            {
                program.Uniform3f(
                    shaderInterface.cameraPosition,
                    camera.Position().x,
                    camera.Position().y,
                    camera.Position().z
                );
                this->UploadSceneUniforms(
                    program,
                    scene,
                    shaderInterface
                );
            }

            mesh.Bind();
            mesh.Draw();
            mesh.Unbind();
            material.Unbind();
        }
    }

    if (environment != nullptr)
        environment->DrawSkybox(view, projection);
}

void Renderer::UploadTransforms(
    Program& program,
    const ShaderInterface& shaderInterface,
    const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection
)
{
    if (shaderInterface.transformMode ==
        TransformUniformMode::CombinedTransform)
    {
        program.UniformMat4f(
            shaderInterface.combinedTransform,
            projection * view * model
        );
        return;
    }

    program.UniformMat4f(shaderInterface.model, model);
    program.UniformMat4f(shaderInterface.view, view);
    program.UniformMat4f(shaderInterface.projection, projection);
}

void Renderer::UploadSceneUniforms(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    program.Uniform1f(shaderInterface.ambientStrength, 0.15f);
    this->UploadEnvironment(program, scene, shaderInterface);
    this->UploadPointLights(program, scene, shaderInterface);
    this->UploadDirectionalLights(program, scene, shaderInterface);
    this->UploadSpotLights(program, scene, shaderInterface);
}

void Renderer::UploadEnvironment(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    if (!shaderInterface.imageBasedLightingEnabled)
        return;

    const EnvironmentMap* environment = scene.Environment();
    program.Uniform1i(
        shaderInterface.hasEnvironment,
        environment != nullptr ? 1 : 0
    );
    program.UniformTex(
        shaderInterface.irradianceMap,
        IrradianceTextureUnit
    );
    program.UniformTex(
        shaderInterface.prefilterMap,
        PrefilterTextureUnit
    );
    program.UniformTex(
        shaderInterface.brdfLut,
        BrdfLutTextureUnit
    );

    if (environment == nullptr)
    {
        program.Uniform1f(shaderInterface.environmentIntensity, 0.0f);
        program.Uniform1f(shaderInterface.maxReflectionLod, 0.0f);
        return;
    }

    environment->BindIrradiance(IrradianceTextureUnit);
    environment->BindPrefilter(PrefilterTextureUnit);
    environment->BindBrdfLut(BrdfLutTextureUnit);
    program.Uniform1f(
        shaderInterface.environmentIntensity,
        environment->Intensity()
    );
    program.Uniform1f(
        shaderInterface.maxReflectionLod,
        environment->MaxReflectionLod()
    );
}

void Renderer::UploadPointLights(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        scene.PointLights().size(),
        shaderInterface.maxPointLights
    );
    program.Uniform1i(shaderInterface.pointLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const PointLight& light = *scene.PointLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            shaderInterface.pointLights +
            "[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + shaderInterface.lightPositionField,
            light.Position().x,
            light.Position().y,
            light.Position().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightRadianceField,
            radiance.x,
            radiance.y,
            radiance.z
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightRangeField,
            light.Range()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightConstantField,
            light.Constant()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightLinearField,
            light.Linear()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightQuadraticField,
            light.Quadratic()
        );
    }
}

void Renderer::UploadDirectionalLights(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        scene.DirectionalLights().size(),
        shaderInterface.maxDirectionalLights
    );
    program.Uniform1i(shaderInterface.directionalLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const DirectionalLight& light = *scene.DirectionalLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            shaderInterface.directionalLights +
            "[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + shaderInterface.lightDirectionField,
            light.Direction().x,
            light.Direction().y,
            light.Direction().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightRadianceField,
            radiance.x,
            radiance.y,
            radiance.z
        );
    }
}

void Renderer::UploadSpotLights(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        scene.SpotLights().size(),
        shaderInterface.maxSpotLights
    );
    program.Uniform1i(shaderInterface.spotLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const SpotLight& light = *scene.SpotLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            shaderInterface.spotLights +
            "[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + shaderInterface.lightPositionField,
            light.Position().x,
            light.Position().y,
            light.Position().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightDirectionField,
            light.Direction().x,
            light.Direction().y,
            light.Direction().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightRadianceField,
            radiance.x,
            radiance.y,
            radiance.z
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightRangeField,
            light.Range()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightConstantField,
            light.Constant()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightLinearField,
            light.Linear()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightQuadraticField,
            light.Quadratic()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.spotInnerCutoffField,
            light.InnerCutoffCos()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.spotOuterCutoffField,
            light.OuterCutoffCos()
        );
    }
}
