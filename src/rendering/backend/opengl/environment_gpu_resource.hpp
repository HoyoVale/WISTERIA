#pragma once

#include "wisteria/rendering/environment.hpp"
#include "wisteria/rendering/vao.hpp"

#include <glm/glm.hpp>
#include <memory>

namespace wisteria
{
class Program;
class Shader;

// R2.0 Phase 0C Step 5: per-device GPU realization of an EnvironmentMap CPU
// definition. Owns the IBL cubemaps, BRDF LUT, capture resources, skybox
// program and skybox geometry. Semantic data stays in EnvironmentMap.
class EnvironmentMapGpuResource
{
public:
    explicit EnvironmentMapGpuResource(EnvironmentMapData data);
    ~EnvironmentMapGpuResource();

    void Attach();
    bool IsAttached() const noexcept;

    void BindIrradiance(unsigned int unit) const;
    void BindPrefilter(unsigned int unit) const;
    void BindBrdfLut(unsigned int unit) const;
    void ConfigureSkyboxVertexArray(VAO& vertexArray) const;
    void DrawSkybox(
        const glm::mat4& view,
        const glm::mat4& projection,
        VAO& vertexArray,
        const EnvironmentMapData& liveData
    );

private:
    void CreateGeometry();
    void CreateEnvironmentCubemap();
    void CreateProceduralCubemap();
    void CreateEquirectangularCubemap();
    void CreateIrradianceMap();
    void CreatePrefilterMap();
    void CreateBrdfLut();
    void CreateSkyboxProgram();
    void RenderCube() const;
    void RenderQuad() const;
    void ReleaseCaptureResources() noexcept;
    void Release() noexcept;

    EnvironmentMapData data;
    GLuint environmentCubemap = 0;
    GLuint irradianceCubemap = 0;
    GLuint prefilterCubemap = 0;
    GLuint brdfLut = 0;
    GLuint captureFramebuffer = 0;
    GLuint captureRenderbuffer = 0;
    GLuint cubeVao = 0;
    GLuint cubeVbo = 0;
    GLuint quadVao = 0;
    GLuint quadVbo = 0;
    std::unique_ptr<Shader> skyboxShader;
    std::unique_ptr<Program> skyboxProgram;
    bool attached = false;
};
}  // namespace wisteria
