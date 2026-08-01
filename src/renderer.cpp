#include "pch.hpp"
#include "renderer.hpp"
#include "shader.hpp"
#include "environment.hpp"
#include "vao.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace
{
constexpr unsigned int IrradianceTextureUnit = 8;
constexpr unsigned int PrefilterTextureUnit = 9;
constexpr unsigned int BrdfLutTextureUnit = 10;
constexpr unsigned int SkinningTextureUnit = 11;

struct RenderCommand
{
    RenderPart* part = nullptr;
    glm::mat4 model{1.0f};
    const Pose* pose = nullptr;
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

int LightCount(std::size_t available, std::size_t capacity)
{
    const std::size_t count = std::min({
        available,
        capacity,
        static_cast<std::size_t>(std::numeric_limits<int>::max())
    });
    return static_cast<int>(count);
}
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
    if (!target.IsValid())
        throw std::logic_error("Renderer requires a valid scene framebuffer");
    // The cache only deduplicates consecutive parts during this frame. Do not
    // retain a raw Pose identity across scene mutations or frame boundaries.
    this->uploadedPose = nullptr;
    this->uploadedPoseRevision = 0;
    target.Bind();

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
            RenderCommand command{&part, model, entity.TryGetPose()};
            if (part.GetMaterial().AlphaMode() == MaterialAlphaMode::Blend)
                transparentCommands.push_back(command);
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
            0
        );
    }

    if (environment != nullptr && environment->ShouldDrawSkybox())
    {
        environment->DrawSkybox(
            view,
            projection,
            this->SkyboxVertexArrayFor(*environment)
        );
    }

    if (transparentCommands.empty())
        return;

    this->EnsureOitResources(target);
    this->BeginOitPass(target);

    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
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
                1
            );
        }
    }
    else
    {
        // OpenGL 3.3 fallback without ARB_draw_buffers_blend. Accumulation
        // and revealage use separate geometry passes because each attachment
        // requires a different blend function.
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
                2
            );
        }
    }

    this->CompositeOit(target);
}

void Renderer::Present(
    const SceneFramebuffer& source,
    int destinationWidth,
    int destinationHeight
)
{
    if (!source.IsValid())
        throw std::logic_error("Cannot present an invalid scene framebuffer");
    if (destinationWidth <= 0 || destinationHeight <= 0)
        return;

    this->EnsurePresentResources();
    Framebuffer::BindDefault();
    glViewport(0, 0, destinationWidth, destinationHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    this->presentProgram->Use();
    source.BindColorTexture(0);
    this->presentProgram->UniformTex("sceneColorTexture", 0);
    this->presentProgram->Uniform1i(
        "fxaaEnabled",
        this->fxaaSettings.enabled ? 1 : 0
    );
    this->presentProgram->Uniform2f(
        "inverseScreenSize",
        1.0f / static_cast<float>(source.Width()),
        1.0f / static_cast<float>(source.Height())
    );
    this->presentProgram->Uniform1f(
        "minimumContrast",
        this->fxaaSettings.minimumContrast
    );
    this->presentProgram->Uniform1f(
        "relativeContrast",
        this->fxaaSettings.relativeContrast
    );
    this->presentProgram->Uniform1f(
        "subpixelBlending",
        this->fxaaSettings.subpixelBlending
    );
    glBindVertexArray(this->fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    this->presentProgram->unUse();
    glDepthMask(GL_TRUE);
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

void Renderer::DrawPart(
    RenderPart& part,
    const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection,
    const Camera& camera,
    const Scene& scene,
    const Pose* pose,
    int oitPass
)
{
    Mesh& mesh = part.GetMesh();
    Material& material = part.GetMaterial();
    mesh.Attach();
    VAO& vertexArray = this->VertexArrayFor(mesh);
    material.Attach();

    if (material.AlphaMode() == MaterialAlphaMode::Blend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);

    if (material.IsDoubleSided())
        glDisable(GL_CULL_FACE);
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }

    material.Bind();
    Program& program = material.GetProgram();
    const ShaderInterface& shaderInterface = material.Interface();
    this->UploadTransforms(
        program,
        shaderInterface,
        model,
        view,
        projection
    );
    this->UploadSkinning(program, shaderInterface, mesh, pose);

    const glm::vec4& baseColor = material.BaseColorFactor();
    program.Uniform4f(
        shaderInterface.materialBaseColorFactor,
        baseColor.r,
        baseColor.g,
        baseColor.b,
        baseColor.a
    );
    program.Uniform1i(
        shaderInterface.materialAlphaMode,
        static_cast<int>(material.AlphaMode())
    );
    program.Uniform1f(
        shaderInterface.materialAlphaCutoff,
        material.AlphaCutoff()
    );
    program.Uniform1i(shaderInterface.oitPass, oitPass);
    program.Uniform1i(
        shaderInterface.hasBaseTexture,
        material.HasTexture(shaderInterface.baseColorTexture) ? 1 : 0
    );
    if (material.ShadingModel() ==
        MaterialShadingModel::PbrMetallicRoughness)
    {
        program.Uniform1i(
            shaderInterface.hasNormalTexture,
            material.HasTexture(shaderInterface.normalTexture) ? 1 : 0
        );
        program.Uniform1f(
            shaderInterface.materialNormalScale,
            material.NormalScale()
        );
        program.Uniform1f(
            shaderInterface.materialMetallicFactor,
            material.MetallicFactor()
        );
        program.Uniform1f(
            shaderInterface.materialRoughnessFactor,
            material.RoughnessFactor()
        );
        program.Uniform3f(
            shaderInterface.materialEmissiveFactor,
            material.EmissiveFactor().r,
            material.EmissiveFactor().g,
            material.EmissiveFactor().b
        );
        program.Uniform1f(
            shaderInterface.materialOcclusionStrength,
            material.OcclusionStrength()
        );
        program.Uniform1i(
            shaderInterface.hasMetallicRoughnessTexture,
            material.HasTexture(
                shaderInterface.metallicRoughnessTexture
            ) ? 1 : 0
        );
        program.Uniform1i(
            shaderInterface.hasEmissiveTexture,
            material.HasTexture(shaderInterface.emissiveTexture) ? 1 : 0
        );
        program.Uniform1i(
            shaderInterface.hasOcclusionTexture,
            material.HasTexture(shaderInterface.occlusionTexture) ? 1 : 0
        );
    }
    else
    {
        program.Uniform3f(
            shaderInterface.materialSpecularColor,
            material.SpecularColor().r,
            material.SpecularColor().g,
            material.SpecularColor().b
        );
        program.Uniform1f(
            shaderInterface.materialShininess,
            material.Shininess()
        );
        program.Uniform3f(
            shaderInterface.materialAmbientColor,
            material.AmbientColor().r,
            material.AmbientColor().g,
            material.AmbientColor().b
        );
        program.Uniform1i(
            shaderInterface.hasSphereTexture,
            material.HasTexture(shaderInterface.sphereTexture) ? 1 : 0
        );
        program.Uniform1i(
            shaderInterface.sphereMapMode,
            static_cast<int>(material.SphereMapMode())
        );
        program.Uniform1i(
            shaderInterface.hasToonTexture,
            material.HasTexture(shaderInterface.toonTexture) ? 1 : 0
        );
        const glm::vec4& edgeColor = material.EdgeColor();
        program.Uniform4f(
            shaderInterface.materialEdgeColor,
            edgeColor.r,
            edgeColor.g,
            edgeColor.b,
            edgeColor.a
        );
        program.Uniform1f(
            shaderInterface.materialEdgeSize,
            material.EdgeSize()
        );
        program.Uniform1i(shaderInterface.outlinePass, 0);
    }

    if (shaderInterface.lightingEnabled)
    {
        program.Uniform3f(
            shaderInterface.cameraPosition,
            camera.Position().x,
            camera.Position().y,
            camera.Position().z
        );
        this->UploadSceneUniforms(
            program,
            scene,
            shaderInterface
        );
    }

    vertexArray.Bind();
    if (material.ShadingModel() == MaterialShadingModel::MmdToon &&
        material.IsEdgeEnabled() && material.EdgeSize() > 0.0f)
    {
        program.Uniform1i(shaderInterface.outlinePass, 1);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        mesh.Draw();
        program.Uniform1i(shaderInterface.outlinePass, 0);
        if (material.IsDoubleSided())
            glDisable(GL_CULL_FACE);
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
    }
    mesh.Draw();
    vertexArray.unBind();
    material.Unbind();
}

VAO& Renderer::VertexArrayFor(Mesh& mesh)
{
    const auto cached = this->meshVertexArrays.find(&mesh);
    if (cached != this->meshVertexArrays.end())
        return *cached->second;

    auto vertexArray = std::make_unique<VAO>();
    mesh.ConfigureVertexArray(*vertexArray);
    VAO& result = *vertexArray;
    this->meshVertexArrays.emplace(&mesh, std::move(vertexArray));
    return result;
}

VAO& Renderer::SkyboxVertexArrayFor(EnvironmentMap& environment)
{
    const auto cached = this->skyboxVertexArrays.find(&environment);
    if (cached != this->skyboxVertexArrays.end())
        return *cached->second;

    auto vertexArray = std::make_unique<VAO>();
    environment.ConfigureSkyboxVertexArray(*vertexArray);
    VAO& result = *vertexArray;
    this->skyboxVertexArrays.emplace(&environment, std::move(vertexArray));
    return result;
}

void Renderer::EnsureOitResources(const SceneFramebuffer& target)
{
    const int width = target.Width();
    const int height = target.Height();
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("OIT framebuffer dimensions must be positive");

    if (this->oitCompositeProgram == nullptr)
    {
        try
        {
            GLint maxDrawBuffers = 0;
            GLint maxColorAttachments = 0;
            glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
            glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
            if (maxDrawBuffers < 2 || maxColorAttachments < 2)
            {
                throw std::runtime_error(
                    "Weighted OIT requires at least two framebuffer color attachments"
                );
            }

            const std::filesystem::path shaderDirectory =
                std::filesystem::current_path() / "assets" / "shaders";
            auto nextShader = std::make_unique<Shader>(
                (shaderDirectory / "oit_composite.vert").string(),
                (shaderDirectory / "oit_composite.frag").string()
            );
            auto nextProgram = std::make_unique<Program>(
                nextShader->GetShaderList()
            );

            this->oitFramebuffer.Create();
            glGenTextures(1, &this->oitAccumulationTexture);
            glGenTextures(1, &this->oitRevealageTexture);
            if (this->oitAccumulationTexture == 0 ||
                this->oitRevealageTexture == 0 ||
                target.DepthRenderbuffer() == 0)
            {
                throw std::runtime_error("Cannot create Weighted OIT resources");
            }

            this->independentBlendSupported =
                GLAD_GL_ARB_draw_buffers_blend != 0 &&
                glad_glBlendFunciARB != nullptr;
            this->oitCompositeShader = std::move(nextShader);
            this->oitCompositeProgram = std::move(nextProgram);
        }
        catch (...)
        {
            this->Release();
            throw;
        }
    }

    if (this->oitWidth == width &&
        this->oitHeight == height &&
        this->oitDepthAttachment == target.DepthRenderbuffer())
        return;

    this->oitFramebuffer.Bind();

    glBindTexture(GL_TEXTURE_2D, this->oitAccumulationTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA16F,
        width,
        height,
        0,
        GL_RGBA,
        GL_HALF_FLOAT,
        nullptr
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    this->oitFramebuffer.AttachTexture2D(
        GL_COLOR_ATTACHMENT0,
        this->oitAccumulationTexture,
        0
    );

    glBindTexture(GL_TEXTURE_2D, this->oitRevealageTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R16F,
        width,
        height,
        0,
        GL_RED,
        GL_HALF_FLOAT,
        nullptr
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    this->oitFramebuffer.AttachTexture2D(
        GL_COLOR_ATTACHMENT1,
        this->oitRevealageTexture,
        0
    );

    this->oitFramebuffer.AttachRenderbuffer(
        GL_DEPTH_ATTACHMENT,
        target.DepthRenderbuffer()
    );

    const GLenum attachments[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1
    };
    glDrawBuffers(2, attachments);
    try
    {
        this->oitFramebuffer.RequireComplete();
    }
    catch (...)
    {
        target.Bind();
        throw;
    }

    target.Bind();
    this->oitWidth = width;
    this->oitHeight = height;
    this->oitDepthAttachment = target.DepthRenderbuffer();
}

void Renderer::EnsurePresentResources()
{
    if (this->presentProgram == nullptr)
    {
        const std::filesystem::path shaderDirectory =
            std::filesystem::current_path() / "assets" / "shaders";
        auto nextShader = std::make_unique<Shader>(
            (shaderDirectory / "present.vert").string(),
            (shaderDirectory / "present.frag").string()
        );
        auto nextProgram = std::make_unique<Program>(
            nextShader->GetShaderList()
        );
        this->presentShader = std::move(nextShader);
        this->presentProgram = std::move(nextProgram);
    }

    if (this->fullscreenVao == 0)
    {
        glGenVertexArrays(1, &this->fullscreenVao);
        if (this->fullscreenVao == 0)
            throw std::runtime_error("Cannot create fullscreen vertex array");
    }
}

void Renderer::BeginOitPass(const SceneFramebuffer& target)
{
    this->oitFramebuffer.Bind();
    glViewport(0, 0, target.Width(), target.Height());

    const GLenum attachments[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1
    };
    glDrawBuffers(2, attachments);
    const GLfloat clearAccumulation[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const GLfloat clearRevealage[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glClearBufferfv(GL_COLOR, 0, clearAccumulation);
    glClearBufferfv(GL_COLOR, 1, clearRevealage);
}

void Renderer::CompositeOit(const SceneFramebuffer& target)
{
    this->EnsurePresentResources();
    target.Bind();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    this->oitCompositeProgram->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->oitAccumulationTexture);
    this->oitCompositeProgram->UniformTex("accumulationTexture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, this->oitRevealageTexture);
    this->oitCompositeProgram->UniformTex("revealageTexture", 1);

    glBindVertexArray(this->fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    this->oitCompositeProgram->unUse();
}

void Renderer::Release() noexcept
{
    this->skyboxVertexArrays.clear();
    this->meshVertexArrays.clear();
    this->presentProgram.reset();
    this->presentShader.reset();
    this->oitCompositeProgram.reset();
    this->oitCompositeShader.reset();
    if (this->fullscreenVao != 0)
        glDeleteVertexArrays(1, &this->fullscreenVao);
    if (this->oitAccumulationTexture != 0)
        glDeleteTextures(1, &this->oitAccumulationTexture);
    if (this->oitRevealageTexture != 0)
        glDeleteTextures(1, &this->oitRevealageTexture);
    if (this->skinningTexture != 0)
        glDeleteTextures(1, &this->skinningTexture);
    if (this->skinningBuffer != 0)
        glDeleteBuffers(1, &this->skinningBuffer);
    this->oitFramebuffer.Release();

    this->fullscreenVao = 0;
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
}

void Renderer::EnsureSkinningResources()
{
    if (this->skinningBuffer != 0 && this->skinningTexture != 0)
        return;

    GLint vertexTextureUnits = 0;
    GLint combinedTextureUnits = 0;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vertexTextureUnits);
    glGetIntegerv(
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        &combinedTextureUnits
    );
    if (vertexTextureUnits < 1 ||
        combinedTextureUnits <= static_cast<GLint>(SkinningTextureUnit))
    {
        throw std::runtime_error(
            "OpenGL does not provide the texture unit required for GPU skinning"
        );
    }

    GLuint nextBuffer = 0;
    GLuint nextTexture = 0;
    glGenBuffers(1, &nextBuffer);
    glGenTextures(1, &nextTexture);
    if (nextBuffer == 0 || nextTexture == 0)
    {
        if (nextTexture != 0)
            glDeleteTextures(1, &nextTexture);
        if (nextBuffer != 0)
            glDeleteBuffers(1, &nextBuffer);
        throw std::runtime_error("Cannot create GPU skinning matrix palette");
    }

    glBindBuffer(GL_TEXTURE_BUFFER, nextBuffer);
    glBindTexture(GL_TEXTURE_BUFFER, nextTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, nextBuffer);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    GLint maximumTexels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximumTexels);
    if (maximumTexels < 4)
    {
        glDeleteTextures(1, &nextTexture);
        glDeleteBuffers(1, &nextBuffer);
        throw std::runtime_error("OpenGL texture buffers cannot store one bone matrix");
    }
    this->skinningBuffer = nextBuffer;
    this->skinningTexture = nextTexture;
    this->maximumSkinningMatrices =
        static_cast<std::size_t>(maximumTexels) / 4U;
}

void Renderer::UploadSkinning(
    Program& program,
    const ShaderInterface& shaderInterface,
    const Mesh& mesh,
    const Pose* pose
)
{
    if (!shaderInterface.skinningSupported)
        return;

    const bool enabled = mesh.IsSkinned() && pose != nullptr;
    program.Uniform1i(shaderInterface.skinningEnabled, enabled ? 1 : 0);
    if (!enabled)
        return;
    if (pose->BoneCount() < mesh.RequiredBoneCount())
    {
        throw std::runtime_error(
            "Entity Pose does not contain every bone required by its Mesh"
        );
    }

    this->EnsureSkinningResources();
    const std::span<const glm::mat4> matrices = pose->SkinningMatrices();
    if (matrices.size() > this->maximumSkinningMatrices)
    {
        throw std::runtime_error(
            "Skeleton exceeds GL_MAX_TEXTURE_BUFFER_SIZE"
        );
    }

    if (this->uploadedPose != pose ||
        this->uploadedPoseRevision != pose->Revision())
    {
        if (matrices.size() >
            static_cast<std::size_t>(
                std::numeric_limits<GLsizeiptr>::max()
            ) / sizeof(glm::mat4))
        {
            throw std::overflow_error("Skinning palette is too large");
        }
        glBindBuffer(GL_TEXTURE_BUFFER, this->skinningBuffer);
        glBufferData(
            GL_TEXTURE_BUFFER,
            static_cast<GLsizeiptr>(matrices.size() * sizeof(glm::mat4)),
            matrices.data(),
            GL_DYNAMIC_DRAW
        );
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
        this->uploadedPose = pose;
        this->uploadedPoseRevision = pose->Revision();
    }

    glActiveTexture(GL_TEXTURE0 + SkinningTextureUnit);
    glBindTexture(GL_TEXTURE_BUFFER, this->skinningTexture);
    program.UniformTex(
        shaderInterface.boneMatrixPalette,
        SkinningTextureUnit
    );
}

void Renderer::UploadTransforms(
    Program& program,
    const ShaderInterface& shaderInterface,
    const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection
)
{
    if (shaderInterface.transformMode ==
        TransformUniformMode::CombinedTransform)
    {
        program.UniformMat4f(
            shaderInterface.combinedTransform,
            projection * view * model
        );
        return;
    }

    program.UniformMat4f(shaderInterface.model, model);
    program.UniformMat4f(shaderInterface.view, view);
    program.UniformMat4f(shaderInterface.projection, projection);
}

void Renderer::UploadSceneUniforms(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    program.Uniform1f(shaderInterface.ambientStrength, 0.15f);
    this->UploadEnvironment(program, scene, shaderInterface);
    this->UploadPointLights(program, scene, shaderInterface);
    this->UploadDirectionalLights(program, scene, shaderInterface);
    this->UploadSpotLights(program, scene, shaderInterface);
}

void Renderer::UploadEnvironment(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    if (!shaderInterface.imageBasedLightingEnabled)
        return;

    const EnvironmentMap* environment = scene.Environment();
    program.Uniform1i(
        shaderInterface.hasEnvironment,
        environment != nullptr ? 1 : 0
    );
    program.UniformTex(
        shaderInterface.irradianceMap,
        IrradianceTextureUnit
    );
    program.UniformTex(
        shaderInterface.prefilterMap,
        PrefilterTextureUnit
    );
    program.UniformTex(
        shaderInterface.brdfLut,
        BrdfLutTextureUnit
    );

    if (environment == nullptr)
    {
        program.Uniform1f(shaderInterface.environmentIntensity, 0.0f);
        program.Uniform1f(shaderInterface.maxReflectionLod, 0.0f);
        return;
    }

    environment->BindIrradiance(IrradianceTextureUnit);
    environment->BindPrefilter(PrefilterTextureUnit);
    environment->BindBrdfLut(BrdfLutTextureUnit);
    program.Uniform1f(
        shaderInterface.environmentIntensity,
        environment->Intensity()
    );
    program.Uniform1f(
        shaderInterface.maxReflectionLod,
        environment->MaxReflectionLod()
    );
}

void Renderer::UploadPointLights(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        scene.PointLights().size(),
        shaderInterface.maxPointLights
    );
    program.Uniform1i(shaderInterface.pointLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const PointLight& light = *scene.PointLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            shaderInterface.pointLights +
            "[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + shaderInterface.lightPositionField,
            light.Position().x,
            light.Position().y,
            light.Position().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightRadianceField,
            radiance.x,
            radiance.y,
            radiance.z
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightRangeField,
            light.Range()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightConstantField,
            light.Constant()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightLinearField,
            light.Linear()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightQuadraticField,
            light.Quadratic()
        );
    }
}

void Renderer::UploadDirectionalLights(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        scene.DirectionalLights().size(),
        shaderInterface.maxDirectionalLights
    );
    program.Uniform1i(shaderInterface.directionalLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const DirectionalLight& light = *scene.DirectionalLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            shaderInterface.directionalLights +
            "[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + shaderInterface.lightDirectionField,
            light.Direction().x,
            light.Direction().y,
            light.Direction().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightRadianceField,
            radiance.x,
            radiance.y,
            radiance.z
        );
    }
}

void Renderer::UploadSpotLights(
    Program& program,
    const Scene& scene,
    const ShaderInterface& shaderInterface
)
{
    const int count = LightCount(
        scene.SpotLights().size(),
        shaderInterface.maxSpotLights
    );
    program.Uniform1i(shaderInterface.spotLightCount, count);

    for (int index = 0; index < count; ++index)
    {
        const SpotLight& light = *scene.SpotLights()[index];
        const glm::vec3 radiance = light.Radiance();
        const std::string uniformPrefix =
            shaderInterface.spotLights +
            "[" + std::to_string(index) + "].";

        program.Uniform3f(
            uniformPrefix + shaderInterface.lightPositionField,
            light.Position().x,
            light.Position().y,
            light.Position().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightDirectionField,
            light.Direction().x,
            light.Direction().y,
            light.Direction().z
        );
        program.Uniform3f(
            uniformPrefix + shaderInterface.lightRadianceField,
            radiance.x,
            radiance.y,
            radiance.z
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightRangeField,
            light.Range()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightConstantField,
            light.Constant()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightLinearField,
            light.Linear()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.lightQuadraticField,
            light.Quadratic()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.spotInnerCutoffField,
            light.InnerCutoffCos()
        );
        program.Uniform1f(
            uniformPrefix + shaderInterface.spotOuterCutoffField,
            light.OuterCutoffCos()
        );
    }
}
