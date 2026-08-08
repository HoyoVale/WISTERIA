#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/offline_render.hpp"

#include <stdexcept>

namespace wisteria
{
Rgba8Frame RenderOffline(
    Scene& scene,
    const OfflineRenderRequest& request,
    Renderer& renderer
)
{
    if (request.width == 0U || request.height == 0U)
    {
        throw std::invalid_argument(
            "Offline render dimensions must be positive"
        );
    }

    SceneFramebuffer target;
    target.Resize(
        static_cast<int>(request.width),
        static_cast<int>(request.height)
    );
    target.Clear(request.clearColor);
    renderer.Render(
        scene,
        request.camera,
        request.projection,
        target
    );
    return ReadbackRgba8(target);
}
}  // namespace wisteria
