#include "wisteria/rendering/environment.hpp"
#include <cmath>
#include <utility>
#include "backend/opengl/environment_gpu_resource.hpp"
#include "backend/opengl/render_resource_cache.hpp"

namespace wisteria
{
namespace
{
bool IsPowerOfTwo(unsigned int value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

unsigned int MaximumMipLevels(unsigned int resolution)
{
    unsigned int levels = 0;
    while (resolution != 0)
    {
        ++levels;
        resolution >>= 1U;
    }
    return levels;
}

EnvironmentMapData PrepareEnvironmentData(EnvironmentMapData data)
{
    // CPU preparation: file IO + HDR decode happen here, never in the
    // OpenGL backend. The decoded image is shared through the data so the
    // GPU realization receives width/height/RGB floats only.
    if (data.equirectangularImage == nullptr &&
        !data.equirectangularPath.empty())
    {
        data.equirectangularImage =
            DecodeEquirectangularHdr(data.equirectangularPath);
    }
    return data;
}
}  // namespace

EnvironmentMapData EnvironmentMapData::ProceduralSky()
{
    return EnvironmentMapData{};
}

EnvironmentMapData EnvironmentMapData::FromEquirectangular(
    std::filesystem::path filePath
)
{
    if (filePath.empty())
        throw std::invalid_argument("Environment image path must not be empty");

    EnvironmentMapData result;
    result.equirectangularPath = std::move(filePath);
    return result;
}

EnvironmentMap::EnvironmentMap(
    EnvironmentMapData data,
    RenderResourceCache* cache
)
    : data(PrepareEnvironmentData(std::move(data)))
{
    if (!IsPowerOfTwo(this->data.environmentResolution) ||
        !IsPowerOfTwo(this->data.irradianceResolution) ||
        !IsPowerOfTwo(this->data.prefilterResolution) ||
        !IsPowerOfTwo(this->data.brdfResolution))
    {
        throw std::invalid_argument(
            "Environment texture resolutions must be non-zero powers of two"
        );
    }
    if (this->data.prefilterMipLevels == 0 ||
        this->data.prefilterMipLevels >
            MaximumMipLevels(this->data.prefilterResolution))
    {
        throw std::invalid_argument(
            "Environment prefilter mip count is invalid for its resolution"
        );
    }
    if (!std::isfinite(this->data.intensity) || this->data.intensity < 0.0f)
        throw std::invalid_argument("Environment intensity must be finite and non-negative");

    // Validate BEFORE touching the cache: a rejected object must never
    // pollute the shared realization table (6B P0-2).
    if (cache != nullptr)
    {
        this->gpu = cache->AcquireEnvironment(this->data);
    }
    else
    {
        this->gpu = std::make_shared<EnvironmentMapGpuResource>(
            this->data,
            nullptr
        );
    }
}

EnvironmentMap::~EnvironmentMap() = default;

void EnvironmentMap::Attach()
{
    this->gpu->Attach();
}

bool EnvironmentMap::IsAttached() const noexcept
{
    return this->gpu->IsAttached();
}

void EnvironmentMap::BindIrradiance(unsigned int unit) const
{
    this->gpu->BindIrradiance(unit);
}

void EnvironmentMap::BindPrefilter(unsigned int unit) const
{
    this->gpu->BindPrefilter(unit);
}

void EnvironmentMap::BindBrdfLut(unsigned int unit) const
{
    this->gpu->BindBrdfLut(unit);
}

void EnvironmentMap::ConfigureSkyboxVertexArray(VAO& vertexArray) const
{
    this->gpu->ConfigureSkyboxVertexArray(vertexArray);
}

void EnvironmentMap::DrawSkybox(
    const glm::mat4& view,
    const glm::mat4& projection,
    VAO& vertexArray
)
{
    this->gpu->DrawSkybox(view, projection, vertexArray, this->data);
}

float EnvironmentMap::Intensity() const noexcept
{
    return this->data.intensity;
}

void EnvironmentMap::SetIntensity(float intensity)
{
    if (!std::isfinite(intensity) || intensity < 0.0f)
        throw std::invalid_argument("Environment intensity must be finite and non-negative");
    this->data.intensity = intensity;
}

bool EnvironmentMap::ShouldDrawSkybox() const noexcept
{
    return this->data.drawSkybox;
}

void EnvironmentMap::SetDrawSkybox(bool enabled) noexcept
{
    this->data.drawSkybox = enabled;
}

float EnvironmentMap::MaxReflectionLod() const noexcept
{
    return static_cast<float>(this->data.prefilterMipLevels - 1U);
}

const EnvironmentMapData& EnvironmentMap::Data() const noexcept
{
    return this->data;
}
}  // namespace wisteria
