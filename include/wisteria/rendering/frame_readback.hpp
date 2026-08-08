#pragma once

#include "wisteria/rendering/framebuffer.hpp"

#include <cstdint>
#include <vector>

namespace wisteria
{
// R1.6 Phase 0B: canonical CPU-side RGBA8 frame.
//
//   channels = R, G, B, A (uint8)
//   stride   = width * 4 bytes, tightly packed
//   origin   = top-left, rows top -> bottom
//   color    = renderer-native RGBA8_UNORM (only GL float->UNORM8
//              conversion, no gamma/tone-map)
struct Rgba8Frame
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> pixels;
};

// Reads the whole SceneFramebuffer color attachment as RGBA8 and returns a
// canonical top-left frame (internally flips the OpenGL bottom-left rows).
// Requires the owning GL context to be current. Preserves the read
// framebuffer, read buffer, pack alignment, PBO binding and pack row/skip
// state.
Rgba8Frame ReadbackRgba8(const SceneFramebuffer& target);
}  // namespace wisteria
