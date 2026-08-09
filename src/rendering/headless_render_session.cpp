#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/headless_render_session.hpp"

#include "backend/opengl/open_gl_render_device.hpp"

#include <exception>
#include <utility>

namespace wisteria
{
HeadlessRenderSession::HeadlessRenderSession(
    std::unique_ptr<IHeadlessContext> nextContext
)
    : context(std::move(nextContext)),
      renderDevice(std::make_unique<OpenGlRenderDevice>()),
      renderer(this->renderDevice.get())
{
    if (this->context == nullptr)
        throw std::invalid_argument(
            "HeadlessRenderSession requires a context provider"
        );
    auto* openGl = dynamic_cast<OpenGlRenderDevice*>(
        this->renderDevice.get()
    );
    if (openGl == nullptr)
    {
        throw std::invalid_argument(
            "R2.0: only the OpenGL RenderDevice backend is available"
        );
    }
    GraphicsDevice& graphicsDevice = openGl->LegacyGraphicsDevice();
    this->resources.BindGraphicsDevice(graphicsDevice);
    graphicsDevice.SetShareGroupToken(this->context->ShareGroupToken());
}

HeadlessRenderSession::~HeadlessRenderSession()
{
    try
    {
        this->MakeCurrent();
    }
    catch (const std::exception& error)
    {
        std::fprintf(
            stderr,
            "[headless-session] FATAL: MakeCurrent during teardown failed; "
            "aborting without GL teardown: %s\n",
            error.what()
        );
        // Fail-stop: with no reliable owning context current, Renderer and
        // GraphicsDevice must never reach their glDelete* calls. A later
        // context-loss design may add AbandonGpuResources(); R1.7 does not.
        std::terminate();
    }
    this->renderer.Release();
    auto* openGl = dynamic_cast<OpenGlRenderDevice*>(
        this->renderDevice.get()
    );
    if (openGl != nullptr)
        openGl->LegacyGraphicsDevice().ReleaseAll();
    // ResourceManager::Clear is Application-private and unnecessary here:
    // member destruction order releases resources (renderer -> resources ->
    // renderDevice -> context) while the provider context is still current.
}

void HeadlessRenderSession::MakeCurrent()
{
    this->context->MakeCurrent();
    auto* openGl = dynamic_cast<OpenGlRenderDevice*>(
        this->renderDevice.get()
    );
    if (openGl == nullptr)
    {
        throw std::logic_error(
            "R2.0: missing OpenGL RenderDevice backend"
        );
    }
    openGl->LegacyGraphicsDevice().FlushPendingDeletes();
    openGl->RefreshCapabilities();
}

void HeadlessRenderSession::ReleaseCurrent()
{
    this->context->ReleaseCurrent();
}

GraphicsDevice& HeadlessRenderSession::GetGraphicsDevice() noexcept
{
    auto* openGl = dynamic_cast<OpenGlRenderDevice*>(
        this->renderDevice.get()
    );
    return openGl->LegacyGraphicsDevice();
}

RenderDevice& HeadlessRenderSession::GetRenderDevice() noexcept
{
    return *this->renderDevice;
}

ResourceManager& HeadlessRenderSession::GetResources() noexcept
{
    return this->resources;
}

Renderer& HeadlessRenderSession::GetRenderer() noexcept
{
    return this->renderer;
}

IHeadlessContext& HeadlessRenderSession::GetContext() noexcept
{
    return *this->context;
}

Rgba8Frame HeadlessRenderSession::RenderOffline(
    Scene& scene,
    const OfflineRenderRequest& request
)
{
    this->MakeCurrent();
    return ::wisteria::RenderOffline(scene, request, this->renderer);
}
}  // namespace wisteria
