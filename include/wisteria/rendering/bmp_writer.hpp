#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace wisteria
{
// Writes a 24-bit BMP file from a canonical TOP-LEFT RGBA8 frame.
//
// BMP files with a positive height store rows bottom-up, so this helper
// flips the input internally; callers always pass the same top-left order
// used by Rgba8Frame / ReadbackRgba8. Throws std::invalid_argument on bad
// dimensions or a buffer-size mismatch.
void WriteBmp24(
    const std::filesystem::path& path,
    int width,
    int height,
    std::span<const std::uint8_t> rgbaTopDown
);
}  // namespace wisteria
