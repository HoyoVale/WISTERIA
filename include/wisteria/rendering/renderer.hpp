#pragma once
#include <glm/glm.hpp>


#include "wisteria/rendering/framebuffer.hpp"
#include "wisteria/rendering/tone_mapping.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/scene/scene.hpp"
#include "wisteria/physics/physics_types.hpp"
#include <glad/gl.h>
#include <cstdint>
#include <cstddef>
#include <memory>

namespace wisteria
{
class RenderDevice;
class OpenGlRenderDevice;
class OpenGlGraphExecutor;
class RenderFramePacket;

struct FxaaSettings
{
    bool enabled = true;
    float minimumContrast = 0.0312f;
    float relativeContrast = 0.125f;
    float subpixelBlending = 0.75f;
};

// R2.0 Final Architecture Closure (P0-1): the Renderer is a thin facade.
// Frame extraction (RenderFramePacket) and graph construction stay here;
// ALL OpenGL pass execution and GL resources live in the device-owned
// OpenGlGraphExecutor. This class never owns GL execution resources.
class Renderer
{
public:
    struct Config
    {
        int shadowMapSize = 2048;
        int shadowPcfRadius = 1;
        bool shadowsEnabled = true;
        bool groundShadowEnabled = true;
        // MMD CSM depth bias (R1-08): exposed so frontends can tune
        // shadow acne vs peter-panning per scene.
        float shadowBias = 0.003f;
    };

    // R2.0 Phase 0B: the renderer consumes the backend-neutral RenderDevice.
    // Null keeps the legacy OpenGL-only compatibility path (no device).
    explicit Renderer(RenderDevice* device = nullptr);
    ~Renderer();

    void SetConfig(const Config& config) noexcept;
    const Config& GetConfig() const noexcept;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Render(
        Scene& scene,
        const Camera& camera,
        const glm::mat4& projection,
        SceneFramebuffer& target
    );
    // Packet-only path: builds the explicit RenderGraph and hands it to the
    // RenderDevice execution authority (legacy path executes directly).
    void RenderPacket(
        const RenderFramePacket& packet,
        SceneFramebuffer& target
    );
    void Present(
        const SceneFramebuffer& source,
        int destinationWidth,
        int destinationHeight
    );
    void SetFxaaSettings(const FxaaSettings& settings);
    const FxaaSettings& GetFxaaSettings() const noexcept;
    void SetToneMappingSettings(const ToneMappingSettings& settings);
    const ToneMappingSettings& GetToneMappingSettings() const noexcept;
    void Release() noexcept;

private:
    OpenGlGraphExecutor& ResolveExecutor();

    RenderDevice* renderDevice = nullptr;
    OpenGlRenderDevice* openGl = nullptr;
    // Legacy Renderer(nullptr) compatibility path: the facade owns a
    // standalone executor. Device-backed paths use device-owned executors.
    std::unique_ptr<OpenGlGraphExecutor> legacyExecutor;
    Config config;
    FxaaSettings fxaaSettings;
    ToneMappingSettings toneMappingSettings;
};
}  // namespace wisteria
