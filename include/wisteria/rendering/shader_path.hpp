#pragma once

// R2.0 Phase 0C Step 6A: neutral shader file-path pair. No GL types; the
// OpenGL backend interprets these paths when building programs.

#include "wisteria/core/asset_paths.hpp"

#include <string>

namespace wisteria
{
struct Path
{
    std::string VertexPath = wisteria::assets::Shader("basicTex.vert");
    std::string FragmentPath = wisteria::assets::Shader("basicTex.frag");
};
}  // namespace wisteria
