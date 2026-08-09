#pragma once

#include "wisteria/rendering/graphics_context.hpp"

#include <string_view>

namespace wisteria
{
// R1.7 Phase 0D: the abstract headless GL context interface lives in
// rendering (no EGL/GLFW types) so composition roots in rendering can own a
// provider without depending on the platform layer. The platform layer
// implements providers (EGL) and exposes the CreateHeadlessContext factory.
struct HeadlessContextOptions
{
    int major = 3;
    int minor = 3;

    // forceSoftware == true means "MUST use a software renderer"
    // (llvmpipe / softpipe). If the created context's GL_RENDERER is not a
    // software renderer, creation fails explicitly. It never silently falls
    // back to hardware, so the flag can be used for deterministic CI.
    bool forceSoftware = false;
};

// R1.7 v1 headless OpenGL context owned by WISTERIA (no window system).
class IHeadlessContext
{
public:
    virtual ~IHeadlessContext() = default;

    // Makes this context current on the calling thread and registers both
    // identities (native context + share group) with GraphicsDevice.
    virtual void MakeCurrent() = 0;
    virtual void ReleaseCurrent() = 0;

    // Identity of this native context (context-local object owner).
    virtual GraphicsContextToken ContextToken() const noexcept = 0;
    // Identity of the share group this context belongs to. Stable for the
    // lifetime of the context (and for every context sharing with it).
    virtual GraphicsShareGroupToken ShareGroupToken() const noexcept = 0;

    // Provider/platform diagnostics.
    virtual std::string_view ProviderName() const noexcept = 0;
    virtual std::string_view PlatformName() const noexcept = 0;
    virtual std::string_view EglVersion() const noexcept = 0;
    virtual std::string_view EglVendor() const noexcept = 0;
    virtual std::string_view Vendor() const noexcept = 0;
    virtual std::string_view Renderer() const noexcept = 0;
    virtual std::string_view Version() const noexcept = 0;
    virtual bool IsSoftware() const noexcept = 0;
};
}  // namespace wisteria
