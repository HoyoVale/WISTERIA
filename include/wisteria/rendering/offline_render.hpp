#pragma once

#include "wisteria/rendering/camera.hpp"
#include "wisteria/rendering/frame_readback.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/scene/scene.hpp"

#include <cstdint>
#include <glm/glm.hpp>

namespace wisteria
{
// R1.6 Phase 0D: explicit offline render request. Presentation state is
// fully explicit (camera + projection); Scene lights are authoritative at
// render time. Alpha policy v1 is opaque: clearColor defaults to opaque
// black and the readback preserves the actual alpha byte.
struct OfflineRenderRequest
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    Camera camera;
    glm::mat4 projection{1.0f};
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
};

// Renders one explicit frame into an internal SceneFramebuffer and returns
// the canonical top-left RGBA8 CPU frame (pre-Present / pre-FXAA SceneColor).
// Requires the owning GL context to be current. The renderer is reused; no
// window, present or swap is involved.
Rgba8Frame RenderOffline(
    Scene& scene,
    const OfflineRenderRequest& request,
    Renderer& renderer
);
}  // namespace wisteria
