#pragma once

#include <filesystem>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <memory>

namespace wisteria
{
class Program;
class Shader;
class VAO;

struct EnvironmentMapData
{
    // An empty path creates a small procedural HDR sky. A file path is
    // interpreted as an equirectangular HDR/LDR image.
    std::filesystem::path equirectangularPath;
    unsigned int environmentResolution = 128;
    unsigned int irradianceResolution = 32;
    unsigned int prefilterResolution = 128;
    unsigned int prefilterMipLevels = 5;
    unsigned int brdfResolution = 256;
    float intensity = 1.0f;
    bool drawSkybox = true;

    static EnvironmentMapData ProceduralSky();
    static EnvironmentMapData FromEquirectangular(
        std::filesystem::path filePath
    );
};

// Owns the scene-level image-based-lighting textures and skybox resources.
// Construction is CPU-only; Attach performs all OpenGL work lazily.
class EnvironmentMap
{
public:
    explicit EnvironmentMap(EnvironmentMapData data = {});
    ~EnvironmentMap();

    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;
    EnvironmentMap(EnvironmentMap&&) = delete;
    EnvironmentMap& operator=(EnvironmentMap&&) = delete;

    void Attach();
    bool IsAttached() const noexcept;

    void BindIrradiance(unsigned int unit) const;
    void BindPrefilter(unsigned int unit) const;
    void BindBrdfLut(unsigned int unit) const;
    void ConfigureSkyboxVertexArray(VAO& vertexArray) const;
    void DrawSkybox(
        const glm::mat4& view,
        const glm::mat4& projection,
        VAO& vertexArray
    );

    float Intensity() const noexcept;
    void SetIntensity(float intensity);
    bool ShouldDrawSkybox() const noexcept;
    void SetDrawSkybox(bool enabled) noexcept;
    float MaxReflectionLod() const noexcept;
    const EnvironmentMapData& Data() const noexcept;

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

private:
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
