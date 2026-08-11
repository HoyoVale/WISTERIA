#pragma once

// R2.0 Phase 0E: backend-neutral presentation endpoint contract.
//
// A PresentSurface separates the display path from:
//   - Window: the platform native window / context,
//   - Renderer: the SceneColor producer,
//   - SceneFramebuffer: the offline scene target.
// The Renderer still blits SceneColor to the presentation target; the
// surface owns the present -> swap sequence. OffscreenRenderSession has no
// PresentSurface requirement (contract §9).
//
// Backend-neutral (Gate A0): this header must never include glad/gl.h,
// GLFW, or any Vulkan header.

namespace wisteria
{
class Renderer;
class SceneFramebuffer;

class PresentSurface
{
public:
    virtual ~PresentSurface() = default;
    PresentSurface(const PresentSurface&) = delete;
    PresentSurface& operator=(const PresentSurface&) = delete;
    PresentSurface(PresentSurface&&) = delete;
    PresentSurface& operator=(PresentSurface&&) = delete;

    virtual int Width() const noexcept = 0;
    virtual int Height() const noexcept = 0;
    // Blit SceneColor to the presentation target (no swap yet).
    virtual void Present(
        Renderer& renderer,
        const SceneFramebuffer& scene
    ) = 0;
    // Present the rendered target (GLFW swap / future queue present).
    virtual void Swap() = 0;

protected:
    PresentSurface() = default;
};
}  // namespace wisteria
