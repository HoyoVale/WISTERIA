#include "wisteria/common/pch.hpp"

#include "backend/opengl/open_gl_graph_executor.hpp"

namespace wisteria
{
void OpenGlGraphExecutor::Present(
    const SceneFramebuffer& source,
    int destinationWidth,
    int destinationHeight
)
{
    // Present mutates draw-buffer, blend, depth, scissor and texture-unit
    // state. Restore everything so the next window or frame starts from a
    // clean baseline.
    RenderStateScope presentState;
    if (!source.IsValid())
        throw std::logic_error("Cannot present an invalid scene framebuffer");
    if (destinationWidth <= 0 || destinationHeight <= 0)
        return;

    this->EnsurePresentResources();
    // Presentation only needs the default draw framebuffer. Preserve the read
    // framebuffer so diagnostics can inspect the scene target independently.
    Framebuffer::BindDefault(GL_DRAW_FRAMEBUFFER);
    GLboolean doubleBuffered = GL_TRUE;
    glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
    glDrawBuffer(doubleBuffered == GL_TRUE ? GL_BACK : GL_FRONT);
    glViewport(0, 0, destinationWidth, destinationHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    this->presentProgram->Use();
    source.BindColorTexture(ScenePresentTextureUnit);
    this->presentProgram->UniformTex(
        "sceneColorTexture",
        ScenePresentTextureUnit
    );
    this->presentProgram->Uniform1i(
        "fxaaEnabled",
        this->fxaaSettings.enabled ? 1 : 0
    );
    this->presentProgram->Uniform2f(
        "inverseScreenSize",
        1.0f / static_cast<float>(source.Width()),
        1.0f / static_cast<float>(source.Height())
    );
    this->presentProgram->Uniform1f(
        "minimumContrast",
        this->fxaaSettings.minimumContrast
    );
    this->presentProgram->Uniform1f(
        "relativeContrast",
        this->fxaaSettings.relativeContrast
    );
    this->presentProgram->Uniform1f(
        "subpixelBlending",
        this->fxaaSettings.subpixelBlending
    );
    glBindVertexArray(this->fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    this->presentProgram->unUse();
    glActiveTexture(GL_TEXTURE0 + ScenePresentTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
}

void OpenGlGraphExecutor::EnsurePresentResources()
{
    if (this->presentProgram == nullptr)
    {
        auto nextShader = std::make_unique<Shader>(
            wisteria::assets::Shader("present.vert"),
            wisteria::assets::Shader("present.frag")
        );
        auto nextProgram = std::make_unique<Program>(
            nextShader->GetShaderList()
        );
        this->presentShader = std::move(nextShader);
        this->presentProgram = std::move(nextProgram);
    }

    if (this->fullscreenVao == 0)
    {
        glGenVertexArrays(1, &this->fullscreenVao);
        if (this->fullscreenVao == 0)
            throw std::runtime_error("Cannot create fullscreen vertex array");
    }
}
}  // namespace wisteria
