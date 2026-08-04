#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

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

            auto nextShader = std::make_unique<Shader>(
                wisteria::assets::Shader("oit_composite.vert"),
                wisteria::assets::Shader("oit_composite.frag")
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

void Renderer::BeginOitPass(const SceneFramebuffer& target)
{
    // The OIT textures were sampled by the previous composite pass. Unbind
    // them before attaching the same objects for drawing again.
    UnbindTexture2DFromUnit(
        OitAccumulationTextureUnit,
        this->oitAccumulationTexture
    );
    UnbindTexture2DFromUnit(
        OitRevealageTextureUnit,
        this->oitRevealageTexture
    );
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
    // Composite is the last step of the transparent pass; restore the GL
    // state it changes so the following physics-debug draw starts clean.
    RenderStateScope compositeState;
    this->EnsurePresentResources();
    target.Bind();
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    this->oitCompositeProgram->Use();
    glActiveTexture(GL_TEXTURE0 + OitAccumulationTextureUnit);
    glBindTexture(GL_TEXTURE_2D, this->oitAccumulationTexture);
    this->oitCompositeProgram->UniformTex(
        "accumulationTexture",
        OitAccumulationTextureUnit
    );
    glActiveTexture(GL_TEXTURE0 + OitRevealageTextureUnit);
    glBindTexture(GL_TEXTURE_2D, this->oitRevealageTexture);
    this->oitCompositeProgram->UniformTex(
        "revealageTexture",
        OitRevealageTextureUnit
    );

    glBindVertexArray(this->fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    this->oitCompositeProgram->unUse();

    glActiveTexture(GL_TEXTURE0 + OitAccumulationTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + OitRevealageTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}
