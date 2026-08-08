#pragma once

#include <memory>
#include <string_view>

namespace wisteria
{
// R1.7 Phase 0B: opaque identity of one OpenGL share group.
//
// A share group is the unit of GPU-object ownership, not a single native
// context handle. Multiple native contexts (EGLContext / GLFWwindow /
// future WGL contexts) that share resources must map to the same token.
// Phase 0B freezes this type and semantics; GraphicsDevice migration happens
// in Phase 0C.
using GraphicsShareGroupToken = const void*;

struct HeadlessContextOptions
{
    int major = 3;
    int minor = 3;

    // forceSoftware == true means "MUST use a software renderer"
    // (llvmpipe / softpipe). If no software EGL device exists, context
    // creation fails explicitly. It never silently falls back to hardware,
    // so the flag can be used for deterministic CI matrices.
    bool forceSoftware = false;
};

// R1.7 v1 headless OpenGL context owned by WISTERIA (no window system).
// Linux implementation: EGL surfaceless primary, EGL_EXT_platform_device
// (hardware then software) fallback. Windows keeps the GLFW hidden-window
// path; CreateHeadlessContext returns nullptr when no EGL provider exists.
class IHeadlessContext
{
public:
    virtual ~IHeadlessContext() = default;

    // Makes this context current on the calling thread. After this call the
    // provider's share group is the current share group; GPU work and
    // pending-delete flushing are valid on this thread.
    virtual void MakeCurrent() = 0;
    virtual void ReleaseCurrent() = 0;

    // Identity of the share group this context belongs to. Stable for the
    // lifetime of the context (and for every context sharing with it).
    virtual GraphicsShareGroupToken ShareGroupToken() const noexcept = 0;

    // Provider/platform diagnostics (R1.7 Phase 0B item 8).
    virtual std::string_view ProviderName() const noexcept = 0;
    virtual std::string_view PlatformName() const noexcept = 0;
    virtual std::string_view EglVersion() const noexcept = 0;
    virtual std::string_view EglVendor() const noexcept = 0;
    virtual std::string_view Vendor() const noexcept = 0;
    virtual std::string_view Renderer() const noexcept = 0;
    virtual std::string_view Version() const noexcept = 0;
    virtual bool IsSoftware() const noexcept = 0;
};

// Provider factory. Returns nullptr (with a diagnostic on stderr) when the
// provider cannot be built or initialized; callers may then decide to fall
// back to a reference provider.
std::unique_ptr<IHeadlessContext> CreateHeadlessContext(
    const HeadlessContextOptions& options = {}
);
}  // namespace wisteria
