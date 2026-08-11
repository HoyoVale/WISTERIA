#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"
#include "backend/opengl/open_gl_render_device.hpp"

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
      renderCache(
          renderDevice != nullptr
              ? &dynamic_cast<OpenGlRenderDevice*>(renderDevice)->RenderCache()
              : nullptr
      ),
      oitFramebuffer(this->device),
      shadowFramebuffer(this->device)
{
    if (renderDevice != nullptr && this->device == nullptr)
    {
        throw std::invalid_argument(
            "R2.0: only the OpenGL RenderDevice backend is available"
        );
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
    const bool shadowsEnabled = this->config.shadowsEnabled &&
        !EnvironmentFlagEnabled("WISTERIA_DISABLE_SHADOWS");
    if (shadowsEnabled &&
        !packet.directionalLights.empty() &&
        !opaqueCommands.empty())
    {
        const DirectionalLight& mainLight =
            *packet.directionalLights.front();
        const glm::vec3 lightDirection = glm::normalize(
            mainLight.Direction()
        );
        const glm::vec3 lightPosition = -lightDirection * 60.0f;
        const glm::mat4 lightView = glm::lookAt(
            lightPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        // Practical split scheme: blend logarithmic and linear cascade
        // boundaries so near cascades get more resolution.
        const float nearClip = camera.NearClip();
        const float farClip = camera.FarClip();
        for (std::size_t index = 0U; index <= ShadowCascadeCount; ++index)
        {
            const float t = static_cast<float>(index) /
                static_cast<float>(ShadowCascadeCount);
            const float logarithmic =
                nearClip * std::pow(farClip / nearClip, t);
            const float linear = nearClip + (farClip - nearClip) * t;
            this->shadowSplitPositions[index] =
                glm::mix(logarithmic, linear, 0.5f);
        }

        const glm::mat4 inverseViewProjection =
            glm::inverse(projection * view);
        std::array<glm::mat4, 4> lightViews;
        std::array<glm::mat4, 4> lightProjections;
        for (std::size_t cascade = 0U;
             cascade < ShadowCascadeCount;
             ++cascade)
        {
            // Frustum slice corners: transform NDC corners at the split
            // depths back to world space through the inverse view-projection.
            glm::vec3 minimumLight(
                std::numeric_limits<float>::max()
            );
            glm::vec3 maximumLight(
                -std::numeric_limits<float>::max()
            );
            for (int cornerX : {-1, 1})
            {
                for (int cornerY : {-1, 1})
                {
                    for (const float splitDepth :
                         {this->shadowSplitPositions[cascade],
                          this->shadowSplitPositions[cascade + 1]})
                    {
                        const float ndcZ =
                            (projection[2][2] * -splitDepth +
                             projection[3][2]) /
                            splitDepth;
                        glm::vec4 world = inverseViewProjection *
                            glm::vec4(
                                static_cast<float>(cornerX),
                                static_cast<float>(cornerY),
                                ndcZ,
                                1.0f
                            );
                        world /= world.w;
                        const glm::vec3 lightSpace = glm::vec3(
                            lightView * world
                        );
                        minimumLight = glm::min(
                            minimumLight,
                            lightSpace
                        );
                        maximumLight = glm::max(
                            maximumLight,
                            lightSpace
                        );
                    }
                }
            }

            // Pad the light-space box so near-plane clamping cannot clip
            // geometry that should cast into the cascade.
            const float padding = 1.0f;
            minimumLight -= glm::vec3(padding);
            maximumLight += glm::vec3(padding);
            const glm::mat4 lightProjection = glm::ortho(
                minimumLight.x,
                maximumLight.x,
                minimumLight.y,
                maximumLight.y,
                -maximumLight.z,
                -minimumLight.z
            );
            lightViews[cascade] = lightView;
            lightProjections[cascade] = lightProjection;
            this->shadowLightViewProjections[cascade] =
                lightProjection * lightView;
        }
        this->RenderShadowPass(
            opaqueCommands,
            lightViews,
            lightProjections
        );
        this->shadowStateEnabled = true;

        // Keep the shadow texture bound on its dedicated unit for the main
        // pass, then restore the scene target the shadow pass replaced.
        glActiveTexture(GL_TEXTURE0 + ShadowMapTextureUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, this->shadowDepthTexture);
        glActiveTexture(GL_TEXTURE0);
        target.Bind();
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0, 0, target.Width(), target.Height());
    }

    ScopedDepthState depthState;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // Ground planes first: the MMD ground shadow pass depth-tests against
    // the floor, and every remaining opaque part is drawn afterwards so the
    // character correctly occludes the flattened shadow instead of being
    // overpainted by a coplanar depth bias.
    // Push the ground's depth a few depth-buffer steps away from the camera
    // so the exact-depth shadow overlay wins the LEQUAL test deterministically
    // and the character (drawn afterwards) still passes at its y=0 feet.
    // Without this margin, moving geometry toggles the shadow/feet boundary
    // every frame, which reads as flicker and a clipped shadow.
    glDisable(GL_POLYGON_OFFSET_FILL);
    for (const RenderCommand& command : opaqueCommands)
    {
        const Material& material = command.part->GetMaterial();
        if (!material.IsGroundPlane() && !material.ReceivesGroundShadow())
            continue;
        // Only true ground planes get the depth margin; shadow receivers
        // such as an imported stage floor keep their exact depth.
        if (material.IsGroundPlane())
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
        }
        else
            glDisable(GL_POLYGON_OFFSET_FILL);
        this->DrawPart(
            *command.part,
            command.model,
            view,
            projection,
            camera,
            packet,
            command.pose,
            command.morphState,
            command.material,
            0
        );
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    // MMD ground shadow: flatten ground-shadow materials onto the y=0 plane
    // along the main light direction. The shadow uses LEQUAL against the
    // ground's depth, so the coplanar overlay lands exactly on the floor;
    // characters drawn afterwards win the depth test and hide the shadow
    // where they occlude it.
    if (shadowsEnabled && !packet.directionalLights.empty() &&
        this->config.groundShadowEnabled &&
        !EnvironmentFlagEnabled("WISTERIA_DISABLE_GROUND_SHADOW"))
    {
        this->RenderGroundShadowPass(
            opaqueCommands,
            view,
            projection,
            glm::normalize(packet.directionalLights.front()->Direction()),
            0.0f
        );
    }

    for (const RenderCommand& command : opaqueCommands)
    {
        if (command.part->GetMaterial().IsGroundPlane())
            continue;
        this->DrawPart(
            *command.part,
            command.model,
            view,
            projection,
            camera,
            packet,
            command.pose,
            command.morphState,
            command.material,
            0
        );
    }

    if (environment != nullptr && environment->ShouldDrawSkybox() &&
        !EnvironmentFlagEnabled("WISTERIA_DISABLE_SKYBOX"))
    {
        environment->DrawSkybox(
            view,
            projection,
            this->SkyboxVertexArrayFor(*environment)
        );
    }

    if (!transparentCommands.empty())
    {
        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);

        if (EnvironmentFlagEnabled("WISTERIA_DISABLE_OIT"))
        {
            // Diagnostic and compatibility fallback: render transparent parts
            // directly into the scene target using conventional alpha blend.
            // This intentionally bypasses both OIT attachments and composite.
            target.Bind();
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            for (const RenderCommand& command : transparentCommands)
            {
                this->DrawPart(
                    *command.part,
                    command.model,
                    view,
                    projection,
                    camera,
                    packet,
                    command.pose,
                    command.morphState,
                    command.material,
                    0
                );
            }
        }
        else
        {
            this->EnsureOitResources(target);
            this->BeginOitPass(target);

            if (this->independentBlendSupported)
            {
                const GLenum attachments[] = {
                    GL_COLOR_ATTACHMENT0,
                    GL_COLOR_ATTACHMENT1
                };
                glDrawBuffers(2, attachments);
                glBlendFunciARB(0, GL_ONE, GL_ONE);
                glBlendFunciARB(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
                for (const RenderCommand& command : transparentCommands)
                {
                    this->DrawPart(
                        *command.part,
                        command.model,
                        view,
                        projection,
                        camera,
                        packet,
                        command.pose,
                        command.morphState,
                        command.material,
                        1
                    );
                }
            }
            else
            {
                // OpenGL 3.3 fallback without ARB_draw_buffers_blend.
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                glBlendFunc(GL_ONE, GL_ONE);
                for (const RenderCommand& command : transparentCommands)
                {
                    this->DrawPart(
                        *command.part,
                        command.model,
                        view,
                        projection,
                        camera,
                        packet,
                        command.pose,
                        command.morphState,
                        command.material,
                        1
                    );
                }

                glDrawBuffer(GL_COLOR_ATTACHMENT1);
                glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
                for (const RenderCommand& command : transparentCommands)
                {
                    this->DrawPart(
                        *command.part,
                        command.model,
                        view,
                        projection,
                        camera,
                        packet,
                        command.pose,
                        command.morphState,
                        command.material,
                        2
                    );
                }
            }

            this->CompositeOit(target);
        }
    }

    this->DrawPhysicsDebug(packet.debugLines, view, projection);
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
