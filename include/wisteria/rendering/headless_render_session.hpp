#pragma once

#include "wisteria/assets/manager.hpp"
#include "wisteria/rendering/headless_context.hpp"
#include "wisteria/rendering/offline_render.hpp"
#include "wisteria/rendering/renderer.hpp"

#include <memory>

namespace wisteria
{
// R1.7 Phase 0D: zero-window composition root for offline rendering.
//
//   HeadlessRenderSession
//   ├─ IHeadlessContext      (provider, owned)
//   ├─ GraphicsDevice        (share-group identity registered from provider)
//   ├─ ResourceManager       (bound to the device)
//   └─ Renderer              (bound to the device)
//
// Everything below is reused unchanged: Scene, ModelInstance, Runtime,
// SceneFramebuffer, RenderOffline and OfflineFrameSequence. The session owns
// the MakeCurrent lifecycle transaction (native current → both trackers →
// flush pending deletes); no GLFW window or Application is involved.
class HeadlessRenderSession
{
public:
    explicit HeadlessRenderSession(std::unique_ptr<IHeadlessContext> context);
    ~HeadlessRenderSession();

    HeadlessRenderSession(const HeadlessRenderSession&) = delete;
    HeadlessRenderSession& operator=(const HeadlessRenderSession&) = delete;

    // Activates the provider context and registers its identities with
    // GraphicsDevice, then flushes queued GPU deletes.
    void MakeCurrent();
    void ReleaseCurrent();

    GraphicsDevice& GetGraphicsDevice() noexcept;
    ResourceManager& GetResources() noexcept;
    Renderer& GetRenderer() noexcept;
    IHeadlessContext& GetContext() noexcept;

    // Renders one explicit frame with the session's renderer, no window.
    // Equivalent to ::wisteria::RenderOffline after MakeCurrent().
    Rgba8Frame RenderOffline(
        Scene& scene,
        const OfflineRenderRequest& request
    );

private:
    std::unique_ptr<IHeadlessContext> context;
    GraphicsDevice graphicsDevice;
    ResourceManager resources;
    Renderer renderer;
};
}  // namespace wisteria
