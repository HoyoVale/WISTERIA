#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

#include <cstddef>

void CaptureRenderState(RenderState& state)
{
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer);
    glGetIntegerv(GL_DRAW_BUFFER, &state.drawBuffer);
    state.blendEnabled = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSource);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDestination);
    state.depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthWriteMask);
    state.scissorTestEnabled = glIsEnabled(GL_SCISSOR_TEST);
    state.stencilTestEnabled = glIsEnabled(GL_STENCIL_TEST);
    state.rasterizerDiscardEnabled = glIsEnabled(GL_RASTERIZER_DISCARD);
    state.cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    glGetBooleanv(GL_COLOR_WRITEMASK, state.colorWriteMask);
    for (std::size_t index = 0; index < RendererTrackedTextureUnitCount; ++index)
    {
        glActiveTexture(GL_TEXTURE0 + RendererBoundTextureUnits[index]);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.boundTextures[index]);
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
}

void RestoreRenderState(const RenderState& state)
{
    for (std::size_t index = 0; index < RendererTrackedTextureUnitCount; ++index)
    {
        glActiveTexture(GL_TEXTURE0 + RendererBoundTextureUnits[index]);
        glBindTexture(
            GL_TEXTURE_2D,
            static_cast<GLuint>(state.boundTextures[index])
        );
    }
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        static_cast<GLuint>(state.drawFramebuffer)
    );
    glBindFramebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(state.readFramebuffer)
    );
    glDrawBuffer(static_cast<GLenum>(state.drawBuffer));
    if (state.blendEnabled == GL_TRUE)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    glBlendFunc(
        static_cast<GLenum>(state.blendSource),
        static_cast<GLenum>(state.blendDestination)
    );
    if (state.depthTestEnabled == GL_TRUE)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDepthMask(state.depthWriteMask);
    if (state.scissorTestEnabled == GL_TRUE)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
    if (state.stencilTestEnabled == GL_TRUE)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
    if (state.rasterizerDiscardEnabled == GL_TRUE)
        glEnable(GL_RASTERIZER_DISCARD);
    else
        glDisable(GL_RASTERIZER_DISCARD);
    if (state.cullFaceEnabled == GL_TRUE)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    glColorMask(
        state.colorWriteMask[0],
        state.colorWriteMask[1],
        state.colorWriteMask[2],
        state.colorWriteMask[3]
    );
}
