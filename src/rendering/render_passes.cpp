#include "wisteria/common/pch.hpp"

#include "renderer_internal.hpp"

namespace wisteria
{
void Renderer::DrawPart(
    RenderPart& part,
    const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection,
    const Camera& camera,
    const RenderFramePacket& packet,
    const Pose* pose,
    const MorphState* morphState,
    const MaterialMorphValues& materialValues,
    int oitPass
)
{
    Mesh& mesh = part.GetMesh();
    Material& material = part.GetMaterial();
    if (this->renderCache != nullptr)
    {
        mesh.SetRenderCache(this->renderCache);
        material.SetRenderCache(this->renderCache);
    }
    mesh.Attach();
    if (mesh.DynamicVertexProvider())
        mesh.DynamicVertexProvider()(mesh);
    VAO& vertexArray = this->VertexArrayFor(mesh);
    material.Attach();
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
            shaderInterface.materialTextureAddFactor,
            materialValues.textureAdd.r,
            materialValues.textureAdd.g,
            materialValues.textureAdd.b,
            materialValues.textureAdd.a
        );
        program.Uniform4f(
            shaderInterface.materialSphereTextureFactor,
            materialValues.sphereTextureFactor.r,
            materialValues.sphereTextureFactor.g,
            materialValues.sphereTextureFactor.b,
            materialValues.sphereTextureFactor.a
        );
        program.Uniform4f(
            shaderInterface.materialSphereTextureAddFactor,
            materialValues.sphereTextureAdd.r,
            materialValues.sphereTextureAdd.g,
            materialValues.sphereTextureAdd.b,
            materialValues.sphereTextureAdd.a
        );
        program.Uniform4f(
            shaderInterface.materialToonTextureFactor,
            materialValues.toonTextureFactor.r,
            materialValues.toonTextureFactor.g,
            materialValues.toonTextureFactor.b,
            materialValues.toonTextureFactor.a
        );
        program.Uniform4f(
            shaderInterface.materialToonTextureAddFactor,
            materialValues.toonTextureAdd.r,
            materialValues.toonTextureAdd.g,
            materialValues.toonTextureAdd.b,
            materialValues.toonTextureAdd.a
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
            packet,
            shaderInterface
        );
    }

    if (shaderInterface.shadowingSupported)
    {
        for (std::size_t cascade = 0U;
             cascade < ShadowCascadeCount;
             ++cascade)
        {
            program.UniformMat4f(
                shaderInterface.lightViewProjection + "[" +
                    std::to_string(cascade) + "]",
                this->shadowLightViewProjections[cascade]
            );
        }
        for (std::size_t index = 0U;
             index <= ShadowCascadeCount;
             ++index)
        {
            program.Uniform1f(
                shaderInterface.shadowSplitPositions + "[" +
                    std::to_string(index) + "]",
                this->shadowSplitPositions[index]
            );
        }
        program.Uniform1i(
            shaderInterface.shadowEnabled,
            this->shadowStateEnabled ? 1 : 0
        );
        program.Uniform1i(
            shaderInterface.receiveShadow,
            this->shadowStateEnabled && material.ReceivesSelfShadow()
                ? 1
                : 0
        );
        program.UniformTex(shaderInterface.shadowMap, ShadowMapTextureUnit);
        program.Uniform2f(
            shaderInterface.shadowMapSize,
            static_cast<float>(this->shadowMapSize),
            static_cast<float>(this->shadowMapSize)
        );
        program.Uniform1i(
            shaderInterface.shadowPcfRadius,
            this->shadowPcfRadius
        );
        program.Uniform1f(
            shaderInterface.shadowBias,
            this->shadowBias
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

void Renderer::EnsureShadowResources()
{
    if (this->shadowProgram == nullptr)
    {
        auto nextShader = std::make_unique<Shader>(
            wisteria::assets::Shader("shadow.vert"),
            wisteria::assets::Shader("shadow.frag")
        );
        auto nextProgram = std::make_unique<Program>(
            nextShader->GetShaderList()
        );
        this->shadowShader = std::move(nextShader);
        this->shadowProgram = std::move(nextProgram);
    }

    if (this->shadowDepthTexture == 0)
    {
        glGenTextures(1, &this->shadowDepthTexture);
        if (this->shadowDepthTexture == 0)
            throw std::runtime_error("Cannot create shadow depth texture");
        glBindTexture(GL_TEXTURE_2D_ARRAY, this->shadowDepthTexture);
        glTexImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            GL_DEPTH_COMPONENT24,
            this->shadowMapSize,
            this->shadowMapSize,
            static_cast<GLsizei>(ShadowCascadeCount),
            0,
            GL_DEPTH_COMPONENT,
            GL_FLOAT,
            nullptr
        );
        glTexParameteri(
            GL_TEXTURE_2D_ARRAY,
            GL_TEXTURE_MIN_FILTER,
            GL_NEAREST
        );
        glTexParameteri(
            GL_TEXTURE_2D_ARRAY,
            GL_TEXTURE_MAG_FILTER,
            GL_NEAREST
        );
        glTexParameteri(
            GL_TEXTURE_2D_ARRAY,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE
        );
        glTexParameteri(
            GL_TEXTURE_2D_ARRAY,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE
        );
        this->shadowFramebuffer.Create();
        this->shadowFramebuffer.Bind();
        glFramebufferTextureLayer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            this->shadowDepthTexture,
            0,
            0
        );
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        this->shadowFramebuffer.RequireComplete();
    }

    if (std::getenv("WISTERIA_SHADOW_DEBUG") != nullptr)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, this->shadowFramebuffer.Id());
        glReadBuffer(GL_NONE);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::vector<float> depths(4U, 1.0f);
        glReadPixels(
            this->shadowMapSize / 2,
            this->shadowMapSize / 2,
            2,
            2,
            GL_DEPTH_COMPONENT,
            GL_FLOAT,
            depths.data()
        );
        std::cout << "[SHADOW DEBUG] center depths="
                  << depths[0] << "," << depths[1] << ","
                  << depths[2] << "," << depths[3] << std::endl;
    }
}

void Renderer::RenderShadowPass(
    const std::vector<RenderCommand>& commands,
    const std::array<glm::mat4, 4>& lightViews,
    const std::array<glm::mat4, 4>& lightProjections
)
{
    this->EnsureShadowResources();
    this->shadowProgram->Use();
    const ShaderInterface shadowInterface;

    // Refresh skinned vertex positions once; every cascade shares the same
    // geometry within a frame, so four uploads would only burn bandwidth.
    for (const RenderCommand& command : commands)
    {
        if (!command.part->GetMaterial().CastsSelfShadow())
            continue;
        Mesh& mesh = command.part->GetMesh();
        if (this->renderCache != nullptr)
            mesh.SetRenderCache(this->renderCache);
        mesh.Attach();
        if (mesh.DynamicVertexProvider())
            mesh.DynamicVertexProvider()(mesh);
    }

    for (std::size_t cascade = 0U;
         cascade < ShadowCascadeCount;
         ++cascade)
    {
        this->shadowFramebuffer.Bind();
        glFramebufferTextureLayer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            this->shadowDepthTexture,
            0,
            static_cast<GLint>(cascade)
        );
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glViewport(0, 0, this->shadowMapSize, this->shadowMapSize);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        std::size_t drawnParts = 0U;
        for (const RenderCommand& command : commands)
        {
            // Only materials flagged CastSelfShadow contribute to the depth
            // map; non-casting materials neither occlude nor receive CSM.
            if (!command.part->GetMaterial().CastsSelfShadow())
                continue;

            Mesh& mesh = command.part->GetMesh();
            VAO& vertexArray = this->VertexArrayFor(mesh);
            this->UploadMorphing(
                vertexArray,
                shadowInterface,
                mesh,
                command.morphState
            );
            this->UploadTransforms(
                *this->shadowProgram,
                shadowInterface,
                command.model,
                lightViews[cascade],
                lightProjections[cascade]
            );
            this->UploadSkinning(
                *this->shadowProgram,
                shadowInterface,
                mesh,
                command.pose
            );
            vertexArray.Bind();
            mesh.Draw();
            vertexArray.unBind();
            ++drawnParts;
        }
        if (std::getenv("WISTERIA_SHADOW_DEBUG") != nullptr)
        {
            std::cout << "[SHADOW DEBUG] cascade=" << cascade
                      << " commands=" << commands.size()
                      << " castParts=" << drawnParts << std::endl;
        }
    }
    this->shadowProgram->unUse();
}

void Renderer::EnsureGroundShadowResources()
{
    if (this->groundShadowProgram != nullptr)
        return;

    auto nextShader = std::make_unique<Shader>(
        wisteria::assets::Shader("ground_shadow.vert"),
        wisteria::assets::Shader("ground_shadow.frag")
    );
    auto nextProgram = std::make_unique<Program>(
        nextShader->GetShaderList()
    );
    this->groundShadowShader = std::move(nextShader);
    this->groundShadowProgram = std::move(nextProgram);
}

void Renderer::RenderGroundShadowPass(
    const std::vector<RenderCommand>& commands,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& lightDirection,
    float groundY
)
{
    // Planar projection: flatten every vertex onto y = groundY along the
    // light travel direction, folded into the view matrix so the ground
    // shadow uses the model's already-skinned vertex positions.
    glm::mat4 shadowProjection(1.0f);
    if (std::abs(lightDirection.y) > 0.0001f)
    {
        const float kx = -lightDirection.x / lightDirection.y;
        const float kz = -lightDirection.z / lightDirection.y;
        shadowProjection = glm::mat4(
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(kx, 0.0f, kz, 0.0f),
            glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
            glm::vec4(-groundY * kx, groundY, -groundY * kz, 1.0f)
        );
    }

    this->EnsureGroundShadowResources();
    const glm::mat4 shadowView = view * shadowProjection;
    const ShaderInterface groundShadowInterface;
    const bool debugOverdraw =
        std::getenv("WISTERIA_GROUND_SHADOW_DEBUG") != nullptr;

    const auto drawFlattened = [&](const RenderCommand& command)
    {
        Mesh& mesh = command.part->GetMesh();
        if (this->renderCache != nullptr)
            mesh.SetRenderCache(this->renderCache);
        mesh.Attach();
        if (mesh.DynamicVertexProvider())
            mesh.DynamicVertexProvider()(mesh);
        VAO& vertexArray = this->VertexArrayFor(mesh);
        this->UploadMorphing(
            vertexArray,
            groundShadowInterface,
            mesh,
            command.morphState
        );
        this->UploadTransforms(
            *this->groundShadowProgram,
            groundShadowInterface,
            command.model,
            shadowView,
            projection
        );
        vertexArray.Bind();
        mesh.Draw();
        vertexArray.unBind();
    };

    // The ground shadow must have one uniform darkness everywhere. Drawing
    // every flattened part with alpha blending accumulates where parts
    // overlap (hair over torso, skirt over legs), producing darker patches.
    // Instead the pass first writes the silhouette into the stencil buffer
    // (each pixel set once, however many parts cover it), then blends the
    // shadow color exactly once per masked pixel.
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    const GLboolean stencilTestEnabled = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLint stencilFunc = GL_ALWAYS;
    GLint stencilRef = 0;
    GLint stencilValueMask = 0xFF;
    glGetIntegerv(GL_STENCIL_FUNC, &stencilFunc);
    glGetIntegerv(GL_STENCIL_REF, &stencilRef);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &stencilValueMask);

    this->groundShadowProgram->Use();
    std::size_t groundParts = 0U;

    if (!debugOverdraw)
    {
        // Mask pass: write stencil 1 wherever a flattened part lands. Depth
        // still tests LEQUAL against the polygon-offset ground so the
        // silhouette is confined to the floor, but neither color nor depth
        // is written, and overlapping parts all set the same value.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0xFF);

        for (const RenderCommand& command : commands)
        {
            if (!command.part->GetMaterial().IsGroundShadow())
                continue;
            ++groundParts;
            drawFlattened(command);
        }

        // Fill pass: draw the ground plane once with the shadow color where
        // the stencil mask is set. Every pixel receives exactly one blended
        // fragment, so the shadow has a single uniform darkness. Depth and
        // culling are irrelevant; the stencil test confines the fill.
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glStencilFunc(GL_EQUAL, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilMask(0x00);
        this->groundShadowProgram->Uniform4f(
            "shadowColor",
            0.12f,
            0.10f,
            0.10f,
            0.55f
        );
        for (const RenderCommand& command : commands)
        {
            const Material& material = command.part->GetMaterial();
            if (!material.IsGroundPlane() &&
                !material.ReceivesGroundShadow())
            {
                continue;
            }
            drawFlattened(command);
        }
    }
    else
    {
        // Debug overdraw: redraw the flattened silhouette on top of the
        // scene so the overlap accumulation (the old uneven-shadow artifact)
        // is visible for inspection.
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        this->groundShadowProgram->Uniform4f(
            "shadowColor",
            1.0f,
            0.0f,
            0.0f,
            0.85f
        );
        for (const RenderCommand& command : commands)
        {
            if (!command.part->GetMaterial().IsGroundShadow())
                continue;
            ++groundParts;
            drawFlattened(command);
        }
    }

    if (std::getenv("WISTERIA_SHADOW_DEBUG") != nullptr)
    {
        std::cout << "[GROUND SHADOW] commands=" << commands.size()
                  << " groundParts=" << groundParts << std::endl;
    }
    this->groundShadowProgram->unUse();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    // The fill pass disables depth testing so the stencil mask alone confines
    // the overlay; restore the entry state or every later opaque/transparent
    // part (and the skybox) would draw without depth testing.
    if (depthTestEnabled == GL_TRUE)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    if (stencilTestEnabled == GL_TRUE)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
    glStencilFunc(
        static_cast<GLenum>(stencilFunc),
        stencilRef,
        static_cast<GLuint>(stencilValueMask)
    );
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilMask(0xFF);
}

VAO& Renderer::VertexArrayFor(Mesh& mesh)
{
    const auto cached = this->meshVertexArrays.find(&mesh);
    if (cached != this->meshVertexArrays.end())
    {
        if (!cached->second.lifetime.expired())
            return *cached->second.vertexArray;
        // The mesh was destroyed since this VAO was cached; drop the stale
        // entry and rebuild from the current mesh.
        this->meshVertexArrays.erase(cached);
    }

    auto vertexArray = std::make_unique<VAO>(this->device);
    mesh.ConfigureVertexArray(*vertexArray);
    MeshVertexArrayEntry entry{
        mesh.LifetimeToken(),
        std::move(vertexArray)
    };
    VAO& result = *entry.vertexArray;
    this->meshVertexArrays.emplace(&mesh, std::move(entry));
    return result;
}

VAO& Renderer::SkyboxVertexArrayFor(EnvironmentMap& environment)
{
    const auto cached = this->skyboxVertexArrays.find(&environment);
    if (cached != this->skyboxVertexArrays.end())
        return *cached->second;

    auto vertexArray = std::make_unique<VAO>(this->device);
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
}  // namespace wisteria
