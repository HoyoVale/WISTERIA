// R2.0 Phase 0C Step 6A Gate: CPU asset headers must be backend-neutral.
//
// This translation unit includes ONLY the CPU asset contracts and links
// nothing (std only). It must never include glad/gl.h or any Vulkan header,
// and no included header may expose GLuint/GLenum/GraphicsDevice.
//
// This test is intentionally strict: if any asset header leaks GL, building
// this target fails.

#include "wisteria/rendering/model.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/rendering/texture.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/environment.hpp"

namespace
{
using wisteria::VertexFormat;
using wisteria::VertexLayout;

static_assert(
    VertexFormat::Float32 != VertexFormat::Uint32
);
}  // namespace

int main()
{
    // Smoke: neutral vertex layout types are usable without any GL.
    wisteria::VertexLayout layout;
    layout.push_back(wisteria::VertexAttribute{
        "position",
        3U,
        wisteria::VertexFormat::Float32,
        false,
        false,
        wisteria::AutomaticAttributeLocation,
        wisteria::VertexSemantic::Position
    });
    if (layout.empty() || layout[0].size != 3U)
        return 1;

    wisteria::ModelData<float, unsigned int> data;
    data.layout = layout;
    if (data.layout.size() != 1U)
        return 1;
    return 0;
}
