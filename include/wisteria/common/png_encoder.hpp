#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wisteria
{
// R1.6 Phase 0E: minimal PNG encoder utility. Renderer/RenderOffline never
// know PNG; this is a standalone converter from a top-left RGBA8 frame to
// PNG bytes. No engine rendering dependency.
//
// Input contract matches Rgba8Frame: tightly packed RGBA8, rows top -> bottom.
std::vector<std::uint8_t> EncodePngRgba8(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint8_t> rgba
);
}  // namespace wisteria
