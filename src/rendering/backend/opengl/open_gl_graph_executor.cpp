#include "wisteria/common/pch.hpp"

#include "open_gl_graph_executor.hpp"

#include "open_gl_render_device.hpp"

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

class ScopedDepthState
{
public:
    ScopedDepthState()
    {
        this->depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &this->previous);
    }

    ~ScopedDepthState()
    {
        glDepthMask(this->previous);
        if (this->depthTestEnabled)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
    }

private:
    GLboolean depthTestEnabled = GL_FALSE;
    GLboolean previous = GL_TRUE;
};
}  // namespace

OpenGlGraphExecutor::OpenGlGraphExecutor(OpenGlRenderDevice* nextOpenGl)
    : openGl(nextOpenGl),
      device(
          nextOpenGl != nullptr
              ? &nextOpenGl->LegacyGraphicsDevice()
              : nullptr
      ),
      renderCache(
          nextOpenGl != nullptr ? &nextOpenGl->RenderCache() : nullptr
      ),
      oitFramebuffer(this->device),
      shadowFramebuffer(this->device)
{
}

OpenGlGraphExecutor::~OpenGlGraphExecutor()
{
    this->Release();
}

void OpenGlGraphExecutor::Execute(
    const RenderGraph& graph,
    const RenderGraphExecutionContext& context
)
{
    // Backend provenance: an OpenGL executor never touches a non-OpenGL
    // render target (clean wrong-backend rejection, not an unchecked cast).
    if (context.target.BackendId() != RenderBackendId::OpenGL)
    {
        throw std::invalid_argument(
            "OpenGL graph executor requires an OpenGL render target"
        );
    }
    const SceneFramebuffer& target =
        static_cast<const SceneFramebuffer&>(context.target);
    const RenderFramePacket& packet = context.packet;

    // Frame boundary: capture the OpenGL state the executor touches and
    // restore it on exit. R0.2 showed that a texture left bound on a unit
    // that becomes a draw attachment in the next frame can black out
    // Mesa/D3D12 without any GL error.
    RenderStateScope frameState;
    if (!target.IsValid())
        throw std::logic_error("Renderer requires a valid scene framebuffer");
    // The cache only deduplicates consecutive parts during this frame. Do
    // not retain a raw Pose identity across scene mutations or frame
    // boundaries.
    this->uploadedPose = nullptr;
    this->uploadedPoseRevision = 0;
    this->BeginMorphingFrame();

    // A texture must not remain bound for sampling when it becomes a draw
    // attachment in the next frame. Windows drivers often tolerate this
    // accidental feedback loop, while Mesa/D3D12 can return black without a
    // GL error. Break the previous frame's post-process bindings explicitly.
    UnbindTexture2DFromUnit(
        ScenePresentTextureUnit,
        target.ColorTexture()
    );
    target.Bind();
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    // The packet is the sole frame-data authority from here on.
    const Camera& camera = packet.camera;
    const glm::mat4& projection = packet.projection;
    const glm::mat4 view = packet.camera.GetView();
    EnvironmentMap* environment = packet.environment;
    if (environment != nullptr)
        environment->Attach();

    // Cascaded shadow mapping configuration resolution (CPU only).
    this->shadowMapSize = this->config.shadowMapSize;
    if (const char* sizeValue = std::getenv("WISTERIA_SHADOW_MAP_SIZE"))
    {
        const int parsed = std::atoi(sizeValue);
        if (parsed >= 256 && parsed <= 4096)
            this->shadowMapSize = parsed;
    }
    this->shadowPcfRadius = this->config.shadowPcfRadius;
    if (const char* radiusValue = std::getenv("WISTERIA_SHADOW_PCF_RADIUS"))
    {
        const int parsed = std::atoi(radiusValue);
        if (parsed >= 1 && parsed <= 3)
            this->shadowPcfRadius = parsed;
    }
    this->shadowBias = this->config.shadowBias;
    this->shadowStateEnabled = false;

    // Frame-level depth guard: no single pass owns (or leaks) the main-pass
    // depth contract.
    ScopedDepthState depthState;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Graph-driven dispatch: the graph is the pass-existence and ordering
    // authority. The executor reads graph data (OrderedPasses + OitComposite
    // presence) instead of maintaining a second scheduler.
    const bool oitEnabled = graph.HasPass(RenderPassId::OitComposite);
    for (const RenderPassId id : graph.OrderedPasses())
    {
        switch (id)
        {
        case RenderPassId::ShadowDepth:
            this->ExecuteShadowDepth(
                packet,
                target,
                camera,
                view,
                projection,
                packet.opaqueDraws
            );
            break;
        case RenderPassId::GroundReceivers:
            this->ExecuteGroundReceivers(
                packet,
                packet.opaqueDraws,
                camera,
                view,
                projection
            );
            break;
        case RenderPassId::MmdGroundShadow:
            this->ExecuteMmdGroundShadow(
                packet,
                packet.opaqueDraws,
                view,
                projection
            );
            break;
        case RenderPassId::Opaque:
            this->ExecuteOpaque(
                packet,
                packet.opaqueDraws,
                camera,
                view,
                projection
            );
            break;
        case RenderPassId::Skybox:
            this->ExecuteSkybox(*environment, view, projection);
            break;
        case RenderPassId::Transparent:
            this->ExecuteTransparent(
                packet,
                target,
                packet.transparentDraws,
                camera,
                view,
                projection,
                oitEnabled
            );
            break;
        case RenderPassId::OitComposite:
            this->ExecuteOitComposite(target);
            break;
        case RenderPassId::PhysicsDebug:
            this->ExecutePhysicsDebug(
                packet.debugLines,
                target,
                view,
                projection
            );
            break;
        }
    }
}

void OpenGlGraphExecutor::SetConfig(const Renderer::Config& nextConfig) noexcept
{
    this->config = nextConfig;
}

const Renderer::Config& OpenGlGraphExecutor::GetConfig() const noexcept
{
    return this->config;
}

void OpenGlGraphExecutor::SetFxaaSettings(const FxaaSettings& settings)
{
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

const FxaaSettings& OpenGlGraphExecutor::GetFxaaSettings() const noexcept
{
    return this->fxaaSettings;
}

void OpenGlGraphExecutor::Release() noexcept
{
    this->skyboxVertexArrays.clear();
    this->meshVertexArrays.clear();
    this->presentProgram.reset();
    this->presentShader.reset();
    this->physicsDebugProgram.reset();
    this->physicsDebugShader.reset();
    this->oitCompositeProgram.reset();
    this->oitCompositeShader.reset();
    this->shadowProgram.reset();
    this->shadowShader.reset();
    this->groundShadowProgram.reset();
    this->groundShadowShader.reset();
    if (this->fullscreenVao != 0)
        glDeleteVertexArrays(1, &this->fullscreenVao);
    if (this->physicsDebugVao != 0)
        glDeleteVertexArrays(1, &this->physicsDebugVao);
    if (this->physicsDebugBuffer != 0)
        glDeleteBuffers(1, &this->physicsDebugBuffer);
    if (this->oitAccumulationTexture != 0)
        glDeleteTextures(1, &this->oitAccumulationTexture);
    if (this->oitRevealageTexture != 0)
        glDeleteTextures(1, &this->oitRevealageTexture);
    if (this->skinningTexture != 0)
        glDeleteTextures(1, &this->skinningTexture);
    if (this->skinningBuffer != 0)
        glDeleteBuffers(1, &this->skinningBuffer);
    if (this->shadowDepthTexture != 0)
        glDeleteTextures(1, &this->shadowDepthTexture);
    this->ReleaseMorphingCache();
    this->oitFramebuffer.Release();
    this->shadowFramebuffer.Release();

    this->fullscreenVao = 0;
    this->physicsDebugVao = 0;
    this->physicsDebugBuffer = 0;
    this->physicsDebugCapacityBytes = 0;
    this->oitAccumulationTexture = 0;
    this->oitRevealageTexture = 0;
    this->skinningTexture = 0;
    this->skinningBuffer = 0;
    this->shadowDepthTexture = 0;
    this->oitWidth = 0;
    this->oitHeight = 0;
    this->oitDepthAttachment = 0;
    this->independentBlendSupported = false;
    this->shadowStateEnabled = false;
    this->maximumSkinningMatrices = 0U;
    this->uploadedPose = nullptr;
    this->uploadedPoseRevision = 0U;
    this->morphingFrame = 0U;
}
}  // namespace wisteria
