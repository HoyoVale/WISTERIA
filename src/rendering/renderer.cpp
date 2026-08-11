#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"
#include "backend/opengl/open_gl_render_device.hpp"
#include "wisteria/rendering/render_graph.hpp"

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
}

Renderer::Renderer(RenderDevice* renderDevice)
    : device(OpenGlRenderDevice::GraphicsDeviceFrom(renderDevice)),
      renderDevice(renderDevice),
      renderCache(nullptr),
      oitFramebuffer(this->device),
      shadowFramebuffer(this->device)
{
    if (renderDevice != nullptr)
    {
        auto* openGl = dynamic_cast<OpenGlRenderDevice*>(renderDevice);
        if (openGl == nullptr)
        {
            throw std::invalid_argument(
                "R2.0: only the OpenGL RenderDevice backend is available"
            );
        }
        this->renderCache = &openGl->RenderCache();
    }
}

void Renderer::SetConfig(const Config& nextConfig) noexcept
{
    this->config = nextConfig;
}

const Renderer::Config& Renderer::GetConfig() const noexcept
{
    return this->config;
}

Renderer::~Renderer()
{
    this->Release();
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
    // Frame boundary: capture the OpenGL state the renderer touches and
    // restore it on exit. R0.2 showed that a texture left bound on a unit
    // that becomes a draw attachment in the next frame can black out
    // Mesa/D3D12 without any GL error.
    RenderStateScope frameState;
    if (!target.IsValid())
        throw std::logic_error("Renderer requires a valid scene framebuffer");
    // The cache only deduplicates consecutive parts during this frame. Do not
    // retain a raw Pose identity across scene mutations or frame boundaries.
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
    const std::vector<RenderCommand>& opaqueCommands = packet.opaqueDraws;
    const std::vector<RenderCommand>& transparentCommands =
        packet.transparentDraws;

    // Cascaded shadow mapping: four light-space depth slices fitted to the
    // camera frustum, rendered into a depth texture array. MMD toon
    // materials select the cascade by camera-space depth in the main pass.
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

    // Frame-level depth guard: the DAG callbacks below are the explicit
    // execution authority, but the depth state lifecycle stays frame-scoped
    // so no single pass owns (or leaks) the main-pass depth contract.
    ScopedDepthState depthState;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Stage 2B Part 2: build the explicit frame DAG from the packet and the
    // real runtime capability options, then execute the existing OpenGL pass
    // bodies as pass callbacks. The builder prunes passes that do not
    // execute this frame; callbacks are wired exactly for registered passes.
    const RenderGraphBuildOptions graphOptions{
        this->config.shadowsEnabled &&
            !EnvironmentFlagEnabled("WISTERIA_DISABLE_SHADOWS"),
        this->config.groundShadowEnabled &&
            !EnvironmentFlagEnabled("WISTERIA_DISABLE_GROUND_SHADOW"),
        !EnvironmentFlagEnabled("WISTERIA_DISABLE_SKYBOX"),
        !EnvironmentFlagEnabled("WISTERIA_DISABLE_OIT"),
    };
    RenderGraph graph = BuildCurrentRenderGraph(packet, graphOptions);

    if (graph.HasPass(RenderPassId::ShadowDepth))
    {
        // Cascaded shadow mapping: four light-space depth slices fitted to
        // the camera frustum, rendered into a depth texture array. MMD toon
        // materials select the cascade by camera-space depth in the main
        // pass.
        graph.SetPassCallback(
            RenderPassId::ShadowDepth,
            [this, &packet, &target, &camera, &view, &projection,
             &opaqueCommands]()
            {
                this->ExecuteShadowDepth(
                    packet,
                    target,
                    camera,
                    view,
                    projection,
                    opaqueCommands
                );
            }
        );
    }

    if (graph.HasPass(RenderPassId::GroundReceivers))
    {
        // Ground planes first: the MMD ground shadow pass depth-tests
        // against the floor, and every remaining opaque part is drawn
        // afterwards so the character correctly occludes the flattened
        // shadow instead of being overpainted by a coplanar depth bias.
        // Push the ground's depth a few depth-buffer steps away from the
        // camera so the exact-depth shadow overlay wins the LEQUAL test
        // deterministically and the character (drawn afterwards) still
        // passes at its y=0 feet. Without this margin, moving geometry
        // toggles the shadow/feet boundary every frame, which reads as
        // flicker and a clipped shadow.
        graph.SetPassCallback(
            RenderPassId::GroundReceivers,
            [this, &packet, &opaqueCommands, &view, &projection, &camera]()
            {
                this->ExecuteGroundReceivers(
                    packet,
                    opaqueCommands,
                    camera,
                    view,
                    projection
                );
            }
        );
    }

    if (graph.HasPass(RenderPassId::MmdGroundShadow))
    {
        // MMD ground shadow: flatten ground-shadow materials onto the y=0
        // plane along the main light direction. The shadow uses LEQUAL
        // against the ground's depth, so the coplanar overlay lands exactly
        // on the floor; characters drawn afterwards win the depth test and
        // hide the shadow where they occlude it.
        graph.SetPassCallback(
            RenderPassId::MmdGroundShadow,
            [this, &packet, &opaqueCommands, &view, &projection]()
            {
                this->ExecuteMmdGroundShadow(
                    packet,
                    opaqueCommands,
                    view,
                    projection
                );
            }
        );
    }

    if (graph.HasPass(RenderPassId::Opaque))
    {
        graph.SetPassCallback(
            RenderPassId::Opaque,
            [this, &packet, &opaqueCommands, &view, &projection, &camera]()
            {
                this->ExecuteOpaque(
                    packet,
                    opaqueCommands,
                    camera,
                    view,
                    projection
                );
            }
        );
    }

    if (graph.HasPass(RenderPassId::Skybox))
    {
        graph.SetPassCallback(
            RenderPassId::Skybox,
            [this, environment, &view, &projection]()
            {
                this->ExecuteSkybox(*environment, view, projection);
            }
        );
    }

    if (graph.HasPass(RenderPassId::Transparent))
    {
        graph.SetPassCallback(
            RenderPassId::Transparent,
            [this, &packet, &target, &transparentCommands, &view,
             &projection, &camera, oitEnabled = graphOptions.oitEnabled]()
            {
                this->ExecuteTransparent(
                    packet,
                    target,
                    transparentCommands,
                    camera,
                    view,
                    projection,
                    oitEnabled
                );
            }
        );
    }

    if (graph.HasPass(RenderPassId::OitComposite))
    {
        graph.SetPassCallback(
            RenderPassId::OitComposite,
            [this, &target]()
            {
                this->ExecuteOitComposite(target);
            }
        );
    }

    if (graph.HasPass(RenderPassId::PhysicsDebug))
    {
        graph.SetPassCallback(
            RenderPassId::PhysicsDebug,
            [this, &packet, &target, &view, &projection]()
            {
                this->ExecutePhysicsDebug(
                    packet.debugLines,
                    target,
                    view,
                    projection
                );
            }
        );
    }

    // Explicit DAG execution through the backend execution authority:
    // preflight requires every registered pass to have a callback, so a
    // wiring error fails before any GL pass runs. The legacy
    // Renderer(nullptr) OpenGL compatibility path executes directly.
    if (this->renderDevice != nullptr)
        this->renderDevice->ExecuteGraph(graph);
    else
        graph.Execute();
}

void Renderer::SetFxaaSettings(const FxaaSettings& settings)
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

const FxaaSettings& Renderer::GetFxaaSettings() const noexcept
{
    return this->fxaaSettings;
}

void Renderer::Release() noexcept
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
    this->maximumSkinningMatrices = 0;
    this->uploadedPose = nullptr;
    this->uploadedPoseRevision = 0;
    this->morphingFrame = 0;
}
}  // namespace wisteria
