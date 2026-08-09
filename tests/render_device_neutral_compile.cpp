// R2.0 Phase 0B Gate A0: backend-neutral public headers must compile
// without any OpenGL/Vulkan dependency.
//
// This translation unit includes ONLY wisteria/rendering/render_device.hpp
// and links nothing (std only). It must never include glad/gl.h or any
// Vulkan header. Building and running this target proves the neutral
// contract has zero GL leakage by construction.

#include "wisteria/rendering/render_device.hpp"

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace
{
using wisteria::RenderDevice;
using wisteria::RenderDeviceCapabilities;

static_assert(std::is_abstract_v<RenderDevice>);
static_assert(
    std::is_default_constructible_v<RenderDeviceCapabilities>
);

static_assert(
    !wisteria::BufferHandle().IsValid() &&
    !wisteria::TextureHandle().IsValid() &&
    !wisteria::SamplerHandle().IsValid() &&
    !wisteria::PipelineHandle().IsValid()
);
}  // namespace

int main()
{
    // Runtime smoke: default handles are invalid, descriptors have sane
    // defaults, and the abstract interface exists.
    RenderDeviceCapabilities capabilities;
    if (capabilities.independentBlend ||
        capabilities.maxSkinningMatrices != 0U)
    {
        return 1;
    }
    wisteria::BufferDesc buffer;
    wisteria::TextureDesc texture;
    wisteria::SamplerDesc sampler;
    wisteria::GraphicsPipelineDesc pipeline;
    if (buffer.size != 0U || texture.width != 0U ||
        sampler.minFilter != wisteria::TextureFilter::Linear ||
        !pipeline.stages.empty())
    {
        return 1;
    }
    return 0;
}
