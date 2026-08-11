#pragma once

// R2.0 Phase 0E (Final Architecture Closure): backend-neutral render target
// contract.
//
// SceneColor output targets (offline scene framebuffers) implement this
// interface. It carries no GL/Vulkan types; backends map it to their own
// attachment/target objects. OffscreenRenderSession output is a
// RenderTarget; window presentation consumes a RenderTarget through
// PresentationTarget.
//
// Backend-neutral (Gate A0): no glad/gl.h, no Vulkan headers.

namespace wisteria
{
class RenderTarget
{
public:
    virtual ~RenderTarget() = default;
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&&) = delete;
    RenderTarget& operator=(RenderTarget&&) = delete;

    virtual int Width() const noexcept = 0;
    virtual int Height() const noexcept = 0;

protected:
    RenderTarget() = default;
};
}  // namespace wisteria
