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
class EnvironmentMapGpuResource;

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
    EnvironmentMapData data;
    // R2.0 Phase 0C Step 5: GPU realization (IBL cubemaps, BRDF LUT, skybox
    // program/geometry) lives outside the semantic EnvironmentMapData.
    std::unique_ptr<EnvironmentMapGpuResource> gpu;
};
}  // namespace wisteria
