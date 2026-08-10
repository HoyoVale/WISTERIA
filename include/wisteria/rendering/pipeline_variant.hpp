#pragma once

// R2.0 Phase 0C Step 7: engine-semantic built-in pipeline variant key.
// 0D RenderGraph/pipeline realization selects backend pipelines from this
// key instead of shipping GLSL through the neutral layer. The CPU asset
// layer (MaterialData) depends on this contract, never on RenderDevice.

#include <cstdint>

namespace wisteria
{
enum class PipelineVariant : std::uint8_t
{
    // Legacy custom GLSL path (OpenGL compatibility facade). Shader file
    // paths are read from the material/descriptor directly.
    Custom,
    PbrMetallicRoughness,
    MmdToon,
    ShadowDepth,
    GroundShadow,
    Skybox,
    OitComposite,
    Present
};

struct PipelineVariantKey
{
    PipelineVariant variant = PipelineVariant::PbrMetallicRoughness;
    // Reserved for future semantic flags (alpha mode, skinning, morph).
    std::uint32_t flags = 0U;
};
}  // namespace wisteria
