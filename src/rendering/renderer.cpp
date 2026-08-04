#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/rendering/shader.hpp"
#include "wisteria/rendering/environment.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/rendering/vao.hpp"
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

struct PhysicsDebugVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
};

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

int LightCount(std::size_t available, std::size_t capacity)
{
    const std::size_t count = std::min({
        available,
        capacity,
        static_cast<std::size_t>(std::numeric_limits<int>::max())
    });
    return static_cast<int>(count);
}

MaterialMorphValues EvaluateMaterialMorphs(
    const RenderPart& part,
    const MorphState* morphState
)
{
    const Material& material = part.GetMaterial();
    MaterialMorphValues values;
    values.diffuse = material.BaseColorFactor();
    values.specular = material.SpecularColor();
    values.shininess = material.Shininess();
    values.ambient = material.AmbientColor();
    values.edgeColor = material.EdgeColor();
    values.edgeSize = material.EdgeSize();

    if (material.ShadingModel() == MaterialShadingModel::MmdToon &&
        morphState != nullptr &&
        morphState->GetMorphSet().HasKind(MorphKind::Material))
    {
        morphState->GetMorphSet().ApplyMaterialMorphs(
            part.MorphMaterialIndex().value_or(AllMaterialMorphTargets),
            morphState->EffectiveWeights(),
            values
        );
    }
    return values;
}

MaterialAlphaMode EffectiveAlphaMode(
    const Material& material,
    const MaterialMorphValues& values
)
{
    if (material.ShadingModel() == MaterialShadingModel::MmdToon &&
        values.diffuse.a < 0.999f)
    {
        return MaterialAlphaMode::Blend;
    }
    return material.AlphaMode();
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
    this->BeginMorphingFrame();
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

    if (environment != nullptr && environment->ShouldDrawSkybox())
    {
        environment->DrawSkybox(
            view,
            projection,
            this->SkyboxVertexArrayFor(*environment)
        );
    }

    if (!transparentCommands.empty())
    {
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
                command.morphState,
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

    this->DrawPhysicsDebug(scene, view, projection);
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
    const MorphState* morphState,
    int oitPass
)
{
    Mesh& mesh = part.GetMesh();
    Material& material = part.GetMaterial();
    mesh.Attach();
    if (mesh.DynamicVertexProvider())
        mesh.DynamicVertexProvider()(mesh);
    VAO& vertexArray = this->VertexArrayFor(mesh);
    material.Attach();
    const MaterialMorphValues materialValues =
        EvaluateMaterialMorphs(part, morphState);
    const MaterialAlphaMode alphaMode =
        EffectiveAlphaMode(material, materialValues);

    if (alphaMode == MaterialAlphaMode::Blend)
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
    this->UploadMorphing(
        vertexArray,
        shaderInterface,
        mesh,
        morphState
    );
    this->UploadTransforms(
        program,
        shaderInterface,
        model,
        view,
        projection
    );
    this->UploadSkinning(program, shaderInterface, mesh, pose);

    const glm::vec4& baseColor = material.ShadingModel() ==
        MaterialShadingModel::MmdToon
        ? materialValues.diffuse
        : material.BaseColorFactor();
    program.Uniform4f(
        shaderInterface.materialBaseColorFactor,
        baseColor.r,
        baseColor.g,
        baseColor.b,
        baseColor.a
    );
    program.Uniform1i(
        shaderInterface.materialAlphaMode,
        static_cast<int>(alphaMode)
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
            materialValues.specular.r,
            materialValues.specular.g,
            materialValues.specular.b
        );
        program.Uniform1f(
            shaderInterface.materialShininess,
            materialValues.shininess
        );
        program.Uniform3f(
            shaderInterface.materialAmbientColor,
            materialValues.ambient.r,
            materialValues.ambient.g,
            materialValues.ambient.b
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
        const glm::vec4& edgeColor = materialValues.edgeColor;
        program.Uniform4f(
            shaderInterface.materialEdgeColor,
            edgeColor.r,
            edgeColor.g,
            edgeColor.b,
            edgeColor.a
        );
        program.Uniform1f(
            shaderInterface.materialEdgeSize,
            materialValues.edgeSize
        );
        program.Uniform4f(
            shaderInterface.materialTextureFactor,
            materialValues.textureFactor.r,
            materialValues.textureFactor.g,
            materialValues.textureFactor.b,
            materialValues.textureFactor.a
        );
        program.Uniform4f(
            shaderInterface.materialSphereTextureFactor,
            materialValues.sphereTextureFactor.r,
            materialValues.sphereTextureFactor.g,
            materialValues.sphereTextureFactor.b,
            materialValues.sphereTextureFactor.a
        );
        program.Uniform4f(
            shaderInterface.materialToonTextureFactor,
            materialValues.toonTextureFactor.r,
            materialValues.toonTextureFactor.g,
            materialValues.toonTextureFactor.b,
            materialValues.toonTextureFactor.a
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
        material.IsEdgeEnabled() && materialValues.edgeSize > 0.0f)
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

void Renderer::EnsurePhysicsDebugResources()
{
    if (this->physicsDebugProgram == nullptr)
    {
        const std::filesystem::path shaderDirectory =
            std::filesystem::current_path() / "assets" / "shaders";
        auto nextShader = std::make_unique<Shader>(
            (shaderDirectory / "physics_debug.vert").string(),
            (shaderDirectory / "physics_debug.frag").string()
        );
        auto nextProgram = std::make_unique<Program>(
            nextShader->GetShaderList()
        );
        this->physicsDebugShader = std::move(nextShader);
        this->physicsDebugProgram = std::move(nextProgram);
    }
    if (this->physicsDebugVao == 0)
        glGenVertexArrays(1, &this->physicsDebugVao);
    if (this->physicsDebugBuffer == 0)
        glGenBuffers(1, &this->physicsDebugBuffer);
    if (this->physicsDebugVao == 0 || this->physicsDebugBuffer == 0)
        throw std::runtime_error("Cannot create physics debug draw resources");

    glBindVertexArray(this->physicsDebugVao);
    glBindBuffer(GL_ARRAY_BUFFER, this->physicsDebugBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PhysicsDebugVertex),
        reinterpret_cast<const void*>(offsetof(PhysicsDebugVertex, position))
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PhysicsDebugVertex),
        reinterpret_cast<const void*>(offsetof(PhysicsDebugVertex, color))
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::DrawPhysicsDebug(
    const Scene& scene,
    const glm::mat4& view,
    const glm::mat4& projection
)
{
    std::vector<PhysicsDebugLine> lines;
    if (scene.Physics().DebugDrawEnabled())
    {
        const std::span<const PhysicsDebugLine> worldLines =
            scene.Physics().DebugLines();
        lines.insert(lines.end(), worldLines.begin(), worldLines.end());
    }
    for (const std::unique_ptr<Entity>& entity : scene.Entities())
        entity->AppendPhysicsDebugLines(lines);
    if (lines.empty())
        return;

    this->EnsurePhysicsDebugResources();
    std::vector<PhysicsDebugVertex> vertices;
    vertices.reserve(lines.size() * 2U);
    for (const PhysicsDebugLine& line : lines)
    {
        vertices.push_back(PhysicsDebugVertex{line.from, line.color});
        vertices.push_back(PhysicsDebugVertex{line.to, line.color});
    }
    const std::size_t bytes = vertices.size() * sizeof(PhysicsDebugVertex);
    glBindBuffer(GL_ARRAY_BUFFER, this->physicsDebugBuffer);
    if (bytes > this->physicsDebugCapacityBytes)
    {
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(bytes),
            vertices.data(),
            GL_DYNAMIC_DRAW
        );
        this->physicsDebugCapacityBytes = bytes;
    }
    else
    {
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>(bytes),
            vertices.data()
        );
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    this->physicsDebugProgram->Use();
    this->physicsDebugProgram->UniformMat4f("view", view);
    this->physicsDebugProgram->UniformMat4f("projection", projection);
    glBindVertexArray(this->physicsDebugVao);
    glDrawArrays(
        GL_LINES,
        0,
        static_cast<GLsizei>(vertices.size())
    );
    glBindVertexArray(0);
    this->physicsDebugProgram->unUse();
    glDepthMask(GL_TRUE);
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

void Renderer::BeginMorphingFrame()
{
    if (this->morphingFrame == std::numeric_limits<std::uint64_t>::max())
    {
        this->ReleaseMorphingCache();
        this->morphingFrame = 1U;
        return;
    }
    ++this->morphingFrame;

    for (auto stateIterator = this->morphingCache.begin();
         stateIterator != this->morphingCache.end();)
    {
        auto& meshes = stateIterator->second;
        for (auto meshIterator = meshes.begin();
             meshIterator != meshes.end();)
        {
            MorphCacheEntry& entry = meshIterator->second;
            if (entry.lastUsedFrame + 1U < this->morphingFrame)
            {
                if (entry.buffer != 0)
                    glDeleteBuffers(1, &entry.buffer);
                meshIterator = meshes.erase(meshIterator);
            }
            else
            {
                ++meshIterator;
            }
        }
        if (meshes.empty())
            stateIterator = this->morphingCache.erase(stateIterator);
        else
            ++stateIterator;
    }
}

void Renderer::ReleaseMorphingCache() noexcept
{
    for (auto& [morphState, meshes] : this->morphingCache)
    {
        (void)morphState;
        for (auto& [mesh, entry] : meshes)
        {
            (void)mesh;
            if (entry.buffer != 0)
                glDeleteBuffers(1, &entry.buffer);
        }
    }
    this->morphingCache.clear();
}

void Renderer::UploadMorphing(
    VAO& vertexArray,
    const ShaderInterface& shaderInterface,
    const Mesh& mesh,
    const MorphState* morphState
)
{
    constexpr GLuint PositionLocation = 9U;
    constexpr GLuint FirstUvLocation = 10U;
    const auto disableAttributes = [&vertexArray]()
    {
        vertexArray.Bind();
        glDisableVertexAttribArray(PositionLocation);
        glVertexAttrib3f(PositionLocation, 0.0f, 0.0f, 0.0f);
        for (std::size_t channel = 0U;
             channel < MmdUvChannelCount;
             ++channel)
        {
            const GLuint location = FirstUvLocation +
                static_cast<GLuint>(channel);
            glDisableVertexAttribArray(location);
            glVertexAttrib4f(location, 0.0f, 0.0f, 0.0f, 0.0f);
        }
        vertexArray.unBind();
    };

    if (!shaderInterface.morphingSupported ||
        !mesh.HasMorphTargets() || morphState == nullptr)
    {
        disableAttributes();
        return;
    }

    MorphCacheEntry& entry = this->morphingCache[morphState][&mesh];
    entry.lastUsedFrame = this->morphingFrame;
    if (!entry.initialized || entry.revision != morphState->Revision())
    {
        entry.active = mesh.CalculateMorphDeltas(
            morphState->EffectiveWeights(),
            entry.offsets
        );
        if (entry.active)
        {
            if (entry.offsets.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<GLsizeiptr>::max()
                ) / sizeof(MorphVertexDelta))
            {
                throw std::overflow_error("Morph vertex buffer is too large");
            }
            if (entry.buffer == 0)
            {
                glGenBuffers(1, &entry.buffer);
                if (entry.buffer == 0)
                {
                    throw std::runtime_error(
                        "Cannot create dynamic morph vertex buffer"
                    );
                }
            }

            const std::size_t byteCount =
                entry.offsets.size() * sizeof(MorphVertexDelta);
            glBindBuffer(GL_ARRAY_BUFFER, entry.buffer);
            if (byteCount > entry.capacityBytes)
            {
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(byteCount),
                    entry.offsets.data(),
                    GL_DYNAMIC_DRAW
                );
                entry.capacityBytes = byteCount;
            }
            else
            {
                glBufferSubData(
                    GL_ARRAY_BUFFER,
                    0,
                    static_cast<GLsizeiptr>(byteCount),
                    entry.offsets.data()
                );
            }
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        entry.revision = morphState->Revision();
        entry.initialized = true;
    }

    if (!entry.active)
    {
        disableAttributes();
        return;
    }

    vertexArray.Bind();
    glBindBuffer(GL_ARRAY_BUFFER, entry.buffer);
    glEnableVertexAttribArray(PositionLocation);
    glVertexAttribPointer(
        PositionLocation,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(MorphVertexDelta)),
        reinterpret_cast<const void*>(offsetof(MorphVertexDelta, position))
    );
    for (std::size_t channel = 0U;
         channel < MmdUvChannelCount;
         ++channel)
    {
        const GLuint location = FirstUvLocation +
            static_cast<GLuint>(channel);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            4,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(MorphVertexDelta)),
            reinterpret_cast<const void*>(
                offsetof(MorphVertexDelta, uv) +
                channel * sizeof(glm::vec4)
            )
        );
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vertexArray.unBind();
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
    // Allocate one identity texel up front. The vertex shader statically
    // samples boneMatrixPalette, and Mesa validates the sampler's buffer
    // texture at draw time even when GPU skinning is disabled, so the
    // texture buffer must never be empty.
    const glm::mat4 identity(1.0f);
    glBufferData(
        GL_TEXTURE_BUFFER,
        sizeof(glm::mat4),
        &identity[0][0],
        GL_DYNAMIC_DRAW
    );
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

    static int skinningLogCounter = 0;
    if ((skinningLogCounter++ % 20000) == 0)
    {
        std::cout << "[SKINNING] dynamic="
                  << (mesh.HasDynamicVertexSource() ? "1" : "0")
                  << " skinned=" << (mesh.IsSkinned() ? "1" : "0")
                  << " bones=" << mesh.RequiredBoneCount()
                  << std::endl;
    }

    // Vertices uploaded by Saba are already CPU-skinned; the GPU skinning
    // pass must be disabled or they would be transformed a second time.
    const bool enabled = mesh.IsSkinned() && pose != nullptr &&
        !mesh.HasDynamicVertexSource();
    program.Uniform1i(shaderInterface.skinningEnabled, enabled ? 1 : 0);
    if (!mesh.IsSkinned())
        return;

    // The vertex shader statically samples boneMatrixPalette, so Mesa
    // validates the sampler's texture unit at draw time even when GPU
    // skinning is disabled. Keep a valid buffer texture bound on the
    // skinning unit for every skinned mesh.
    this->EnsureSkinningResources();
    glActiveTexture(GL_TEXTURE0 + SkinningTextureUnit);
    glBindTexture(GL_TEXTURE_BUFFER, this->skinningTexture);
    program.UniformTex(
        shaderInterface.boneMatrixPalette,
        SkinningTextureUnit
    );

    if (!enabled)
        return;
    if (pose->BoneCount() < mesh.RequiredBoneCount())
    {
        throw std::runtime_error(
            "Entity Pose does not contain every bone required by its Mesh"
        );
    }

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
