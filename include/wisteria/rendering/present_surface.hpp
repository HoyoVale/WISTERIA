#pragma once

// R2.0 Phase 0E (Final Architecture Closure): backend-neutral PLATFORM
// presentation endpoint contract.
//
// A PresentSurface describes ONLY the native display endpoint (dimensions).
// It carries no Renderer, no SceneFramebuffer and no Swap: those belong to
// the backend-created PresentationTarget. This keeps the OpenGL mapping
// (GLFW default framebuffer + glfwSwapBuffers) and the future Vulkan mapping
// (VkSurfaceKHR + swapchain + vkQueuePresentKHR) on the same neutral seam
// (contract §9):
//
//   Platform Window
//        ↓
//   PresentSurface
//        ↓
//   backend creates PresentationTarget using RenderDevice + PresentSurface
//
// OffscreenRenderSession has no PresentSurface requirement.
//
// Backend-neutral (Gate A0): this header must never include glad/gl.h,
// GLFW, or any Vulkan header.

namespace wisteria
{
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

protected:
    PresentSurface() = default;
};
}  // namespace wisteria
