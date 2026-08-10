#pragma once

#include <filesystem>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace wisteria
{
class Program;
class Shader;
class VAO;
class EnvironmentMapGpuResource;
class RenderResourceCache;

// R2.0 Phase 0C: backend-neutral decoded HDR source. Produced by CPU
// preparation (environment_decode.cpp); the OpenGL realization only uploads
// it. rgb is tightly packed width*height*3 floats.
struct EnvironmentHdrImage
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<float> rgb;
};

// CPU preparation: file IO + stb_image HDR decode. Never called from the
// OpenGL backend. Throws std::runtime_error on missing/corrupt input.
std::shared_ptr<const EnvironmentHdrImage> DecodeEquirectangularHdr(
    const std::filesystem::path& filePath
);

struct EnvironmentMapData
{
    // An empty path creates a small procedural HDR sky. A file path is
    // interpreted as an equirectangular HDR/LDR image. After CPU
    // preparation, equirectangularImage carries the decoded pixels; the
    // path remains as provenance/diagnostic metadata only.
    std::filesystem::path equirectangularPath;
    std::shared_ptr<const EnvironmentHdrImage> equirectangularImage;
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
    explicit EnvironmentMap(
        EnvironmentMapData data = {},
        RenderResourceCache* cache = nullptr
    );
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
