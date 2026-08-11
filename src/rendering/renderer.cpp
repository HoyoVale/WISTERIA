#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/renderer.hpp"

#include "backend/opengl/open_gl_graph_executor.hpp"
#include "backend/opengl/open_gl_render_device.hpp"
#include "wisteria/rendering/render_frame_packet.hpp"
#include "wisteria/rendering/render_graph.hpp"

#include <cstdlib>
#include <stdexcept>

namespace wisteria
{
namespace
{
bool EnvironmentFlagEnabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view(value) != "0";
}
}  // namespace

Renderer::Renderer(RenderDevice* renderDevice)
    : renderDevice(renderDevice),
      openGl(dynamic_cast<OpenGlRenderDevice*>(renderDevice))
{
    if (renderDevice != nullptr && this->openGl == nullptr)
    {
        throw std::invalid_argument(
            "R2.0: only the OpenGL RenderDevice backend is available"
        );
    }
}

Renderer::~Renderer()
{
    this->Release();
}

OpenGlGraphExecutor& Renderer::ResolveExecutor()
{
    if (this->openGl != nullptr)
        return this->openGl->GraphExecutorForCurrentContext();
    if (this->legacyExecutor == nullptr)
    {
        this->legacyExecutor =
            std::make_unique<OpenGlGraphExecutor>(nullptr);
    }
    return *this->legacyExecutor;
}

void Renderer::SetConfig(const Config& nextConfig) noexcept
{
    this->config = nextConfig;
}

const Renderer::Config& Renderer::GetConfig() const noexcept
{
    return this->config;
}

void Renderer::Render(
    Scene& scene,
    const Camera& camera,
    const glm::mat4& projection,
    SceneFramebuffer& target
)
{
    // 0D Stage 1: CPU extraction first, then a packet-only GL path.
    RenderFramePacket packet = BuildRenderFramePacket(
        scene,
        camera,
        projection
    );
    this->RenderPacket(packet, target);
}

void Renderer::RenderPacket(
    const RenderFramePacket& packet,
    SceneFramebuffer& target
)
{
    OpenGlGraphExecutor& executor = this->ResolveExecutor();
    executor.SetConfig(this->config);
    executor.SetFxaaSettings(this->fxaaSettings);

    // Build the explicit frame DAG from the packet and the real runtime
    // capability options; the builder prunes passes that do not execute
    // this frame.
    const RenderGraphBuildOptions graphOptions{
        this->config.shadowsEnabled &&
            !EnvironmentFlagEnabled("WISTERIA_DISABLE_SHADOWS"),
        this->config.groundShadowEnabled &&
            !EnvironmentFlagEnabled("WISTERIA_DISABLE_GROUND_SHADOW"),
        !EnvironmentFlagEnabled("WISTERIA_DISABLE_SKYBOX"),
        !EnvironmentFlagEnabled("WISTERIA_DISABLE_OIT"),
    };
    RenderGraph graph = BuildCurrentRenderGraph(packet, graphOptions);
    const RenderGraphExecutionContext executionContext{packet, target};

    // Execution authority: device-backed path goes through the RenderDevice
    // (device-owned OpenGL executor); the legacy path executes the
    // facade-owned executor directly.
    if (this->openGl != nullptr)
        this->openGl->ExecuteGraph(graph, executionContext);
    else
        executor.Execute(graph, executionContext);
}

void Renderer::Present(
    const SceneFramebuffer& source,
    int destinationWidth,
    int destinationHeight
)
{
    OpenGlGraphExecutor& executor = this->ResolveExecutor();
    executor.SetFxaaSettings(this->fxaaSettings);
    executor.Present(source, destinationWidth, destinationHeight);
}

void Renderer::SetFxaaSettings(const FxaaSettings& settings)
{
    // Validate eagerly like the executor does, so invalid settings fail at
    // the API boundary instead of at the next frame.
    const bool valid =
        std::isfinite(settings.minimumContrast) &&
        std::isfinite(settings.relativeContrast) &&
        std::isfinite(settings.subpixelBlending) &&
        settings.minimumContrast >= 0.0f &&
        settings.minimumContrast <= 1.0f &&
        settings.relativeContrast >= 0.0f &&
        settings.relativeContrast <= 1.0f &&
        settings.subpixelBlending >= 0.0f &&
        settings.subpixelBlending <= 1.0f;
    if (!valid)
    {
        throw std::invalid_argument(
            "FXAA contrast and subpixel settings must be finite values in [0, 1]"
        );
    }
    this->fxaaSettings = settings;
}

const FxaaSettings& Renderer::GetFxaaSettings() const noexcept
{
    return this->fxaaSettings;
}

void Renderer::Release() noexcept
{
    if (this->openGl != nullptr)
        this->openGl->ReleaseGraphExecutorForCurrentContext();
    if (this->legacyExecutor != nullptr)
    {
        this->legacyExecutor->Release();
        this->legacyExecutor.reset();
    }
}
}  // namespace wisteria
