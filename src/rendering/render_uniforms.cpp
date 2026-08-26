#include "wisteria/common/pch.hpp"

#include "backend/opengl/open_gl_graph_executor.hpp"

namespace wisteria
{
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

void OpenGlGraphExecutor::UploadSceneUniforms(
    Program& program,
    const RenderFramePacket& packet,
    const ShaderInterface& shaderInterface
)
{
    program.Uniform1f(shaderInterface.ambientStrength, 0.15f);
    program.Uniform1i(
        shaderInterface.toneMappingMode,
        static_cast<int>(this->toneMappingSettings.mode)
    );
    program.Uniform1f(
        shaderInterface.exposure,
        this->toneMappingSettings.exposure
    );
    this->UploadEnvironment(program, packet, shaderInterface);
    this->UploadPointLights(program, packet, shaderInterface);
    this->UploadDirectionalLights(program, packet, shaderInterface);
    this->UploadSpotLights(program, packet, shaderInterface);
}

void OpenGlGraphExecutor::UploadEnvironment(
    Program& program,
    const RenderFramePacket& packet,
    const ShaderInterface& shaderInterface
)
{
    if (!shaderInterface.imageBasedLightingEnabled)
        return;

    const EnvironmentMap* environment = packet.environment;
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

void OpenGlGraphExecutor::UploadPointLights(
    Program& program,
    const RenderFramePacket& packet,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        packet.pointLights.size(),
        shaderInterface.maxPointLights
    );
    program.Uniform1i(shaderInterface.pointLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const PointLight& light = *packet.pointLights[index];
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

void OpenGlGraphExecutor::UploadDirectionalLights(
    Program& program,
    const RenderFramePacket& packet,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        packet.directionalLights.size(),
        shaderInterface.maxDirectionalLights
    );
    program.Uniform1i(shaderInterface.directionalLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const DirectionalLight& light =
            *packet.directionalLights[index];
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

void OpenGlGraphExecutor::UploadSpotLights(
    Program& program,
    const RenderFramePacket& packet,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        packet.spotLights.size(),
        shaderInterface.maxSpotLights
    );
    program.Uniform1i(shaderInterface.spotLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const SpotLight& light = *packet.spotLights[index];
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
}  // namespace wisteria
