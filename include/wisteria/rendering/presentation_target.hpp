#pragma once

#include "wisteria/rendering/render_target.hpp"

// R2.0 Phase 0E (Final Architecture Closure): backend-created presentation
// endpoint.
//
// A PresentationTarget is created by a RenderDevice for a PresentSurface
// (platform endpoint). It owns the backend present sequence:
//   - Present(source): blit a RenderTarget to the display target,
//   - Swap(): queue/flip the presented frame.
// OpenGL maps this to Renderer::Present + glfwSwapBuffers; Vulkan will map
// it to vkQueuePresentKHR on a swapchain.
//
// Backend-neutral (Gate A0): no glad/gl.h, no Vulkan headers.

namespace wisteria
{
class PresentationTarget
{
public:
    virtual ~PresentationTarget() = default;
    PresentationTarget(const PresentationTarget&) = delete;
    PresentationTarget& operator=(const PresentationTarget&) = delete;
    PresentationTarget(PresentationTarget&&) = delete;
    PresentationTarget& operator=(PresentationTarget&&) = delete;

    virtual void Present(const RenderTarget& source) = 0;
    virtual void Swap() = 0;

protected:
    PresentationTarget() = default;
};
}  // namespace wisteria
