#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/headless_render_session.hpp"

#include <utility>

namespace wisteria
{
HeadlessRenderSession::HeadlessRenderSession(
    std::unique_ptr<IHeadlessContext> nextContext
)
    : context(std::move(nextContext)),
      renderer(&this->graphicsDevice)
{
    if (this->context == nullptr)
        throw std::invalid_argument(
            "HeadlessRenderSession requires a context provider"
        );
    this->resources.BindGraphicsDevice(this->graphicsDevice);
    this->graphicsDevice.SetShareGroupToken(this->context->ShareGroupToken());
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
            "[headless-session] MakeCurrent during teardown failed: %s\n",
            error.what()
        );
    }
    this->renderer.Release();
    this->graphicsDevice.ReleaseAll();
    // ResourceManager::Clear is Application-private and unnecessary here:
    // member destruction order releases resources (renderer -> resources ->
    // graphicsDevice -> context) while the provider context is still current.
}

void HeadlessRenderSession::MakeCurrent()
{
    this->context->MakeCurrent();
    this->graphicsDevice.FlushPendingDeletes();
}

void HeadlessRenderSession::ReleaseCurrent()
{
    this->context->ReleaseCurrent();
}

GraphicsDevice& HeadlessRenderSession::GetGraphicsDevice() noexcept
{
    return this->graphicsDevice;
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
