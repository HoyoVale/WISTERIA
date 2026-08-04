#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

namespace wisteria
{
namespace
{
bool EnvironmentFlagEnabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view(value) != "0";
}

struct RenderCommand
{
    RenderPart* part = nullptr;
    glm::mat4 model{1.0f};
    const Pose* pose = nullptr;
    const MorphState* morphState = nullptr;
};

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

    const glm::mat4 view = camera.GetView();
    EnvironmentMap* environment = scene.Environment();
    if (environment != nullptr)
        environment->Attach();

    std::vector<RenderCommand> opaqueCommands;
    std::vector<RenderCommand> transparentCommands;
    for (const std::unique_ptr<Entity>& entityPointer : scene.Entities())
    {
        Entity& entity = *entityPointer;
        if (!entity.IsVisible())
            continue;

        const glm::mat4 entityTransform = entity.GetTransform().Matrix();
        for (RenderPart& part : entity.RenderParts())
        {
            const glm::mat4 model =
                entityTransform * part.LocalTransform();
            RenderCommand command{
                &part,
                model,
                entity.TryGetPose(),
                entity.TryGetMorphState()
            };
            const MaterialMorphValues materialValues =
                EvaluateMaterialMorphs(part, command.morphState);
            if (EffectiveAlphaMode(part.GetMaterial(), materialValues) ==
                MaterialAlphaMode::Blend)
            {
                transparentCommands.push_back(command);
            }
            else
                opaqueCommands.push_back(command);
        }
    }

    ScopedDepthState depthState;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    for (const RenderCommand& command : opaqueCommands)
    {
        this->DrawPart(
            *command.part,
            command.model,
            view,
            projection,
            camera,
            scene,
            command.pose,
            command.morphState,
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
                    scene,
                    command.pose,
                    command.morphState,
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
                        scene,
                        command.pose,
                        command.morphState,
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
                        scene,
                        command.pose,
                        command.morphState,
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
                        scene,
                        command.pose,
                        command.morphState,
                        2
                    );
                }
            }

            this->CompositeOit(target);
        }
    }

    this->DrawPhysicsDebug(scene, view, projection);
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
    this->ReleaseMorphingCache();
    this->oitFramebuffer.Release();

    this->fullscreenVao = 0;
    this->physicsDebugVao = 0;
    this->physicsDebugBuffer = 0;
    this->physicsDebugCapacityBytes = 0;
    this->oitAccumulationTexture = 0;
    this->oitRevealageTexture = 0;
    this->skinningTexture = 0;
    this->skinningBuffer = 0;
    this->oitWidth = 0;
    this->oitHeight = 0;
    this->oitDepthAttachment = 0;
    this->independentBlendSupported = false;
    this->maximumSkinningMatrices = 0;
    this->uploadedPose = nullptr;
    this->uploadedPoseRevision = 0;
    this->morphingFrame = 0;
}
}  // namespace wisteria
