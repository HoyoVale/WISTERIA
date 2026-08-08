#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/offline_render.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace wisteria
{
namespace
{
// R1.6 Phase 0D: SceneFramebuffer::Clear and some Renderer passes mutate GL
// state that Renderer's RenderStateScope does not track (clear color, front/
// back stencil write masks, etc.). This RAII guard spans the whole
// RenderOffline body and restores the caller's explicitly tracked boundary
// state on exit, so RenderOffline behaves like Renderer + ReadbackRgba8
// (state-clean). It only guarantees this listed state set, per the 0B
// discipline.
class OfflineClearStateGuard
{
public:
    OfflineClearStateGuard()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &this->drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &this->readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, this->viewport);
        this->scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glGetBooleanv(GL_COLOR_WRITEMASK, this->colorMask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &this->depthWriteMask);
        glGetIntegerv(
            GL_STENCIL_WRITEMASK,
            &this->frontStencilWriteMask
        );
        glGetIntegerv(
            GL_STENCIL_BACK_WRITEMASK,
            &this->backStencilWriteMask
        );
        glGetFloatv(GL_COLOR_CLEAR_VALUE, this->clearColor);
    }

    ~OfflineClearStateGuard()
    {
        glClearColor(
            this->clearColor[0],
            this->clearColor[1],
            this->clearColor[2],
            this->clearColor[3]
        );
        glStencilMaskSeparate(
            GL_FRONT,
            static_cast<GLuint>(this->frontStencilWriteMask)
        );
        glStencilMaskSeparate(
            GL_BACK,
            static_cast<GLuint>(this->backStencilWriteMask)
        );
        glDepthMask(this->depthWriteMask);
        glColorMask(
            this->colorMask[0],
            this->colorMask[1],
            this->colorMask[2],
            this->colorMask[3]
        );
        if (this->scissorEnabled == GL_TRUE)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
        glViewport(
            this->viewport[0],
            this->viewport[1],
            this->viewport[2],
            this->viewport[3]
        );
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(this->readFramebuffer)
        );
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>(this->drawFramebuffer)
        );
    }

    OfflineClearStateGuard(const OfflineClearStateGuard&) = delete;
    OfflineClearStateGuard& operator=(const OfflineClearStateGuard&) = delete;

private:
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLboolean scissorEnabled = GL_FALSE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depthWriteMask = GL_TRUE;
    GLint frontStencilWriteMask = 0xFF;
    GLint backStencilWriteMask = 0xFF;
    GLfloat clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};
}

Rgba8Frame RenderOffline(
    Scene& scene,
    const OfflineRenderRequest& request,
    Renderer& renderer
)
{
    if (request.width == 0U || request.height == 0U ||
        request.width >
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        request.height >
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument(
            "Offline render dimensions must be positive and fit int"
        );
    }
    // R1.6 v1 alpha policy is OpaqueOnly: background clear alpha must be
    // exactly 1.0 so the API cannot manufacture a transparent background.
    if (!std::isfinite(request.clearColor.a) ||
        request.clearColor.a != 1.0f)
    {
        throw std::invalid_argument(
            "R1.6 offline output requires opaque clear alpha"
        );
    }

    // Capture before any GL mutation; restore on normal or exceptional exit.
    OfflineClearStateGuard offlineState;

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
