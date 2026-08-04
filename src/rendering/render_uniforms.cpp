#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

namespace
{
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
