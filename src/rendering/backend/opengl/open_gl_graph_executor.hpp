#pragma once

// R2.0 Final Architecture Closure (P0-1): the OpenGL backend owns graph
// execution. OpenGlGraphExecutor is the OpenGL pass executor; it is created
// and owned by OpenGlRenderDevice (one per GL context). The Renderer facade
// only extracts frames and hands the graph + context to the RenderDevice;
// it no longer owns pass execution or GL execution resources.

#include "open_gl_render_device.hpp"
#include "rendering/renderer_internal.hpp"

#include "wisteria/rendering/render_graph.hpp"
#include "wisteria/rendering/render_device.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace wisteria
{
class OpenGlRenderDevice;
class RenderResourceCache;

class OpenGlGraphExecutor
{
public:
    explicit OpenGlGraphExecutor(OpenGlRenderDevice* openGl);
    ~OpenGlGraphExecutor();

    OpenGlGraphExecutor(const OpenGlGraphExecutor&) = delete;
    OpenGlGraphExecutor& operator=(const OpenGlGraphExecutor&) = delete;

    // Executes the compiled frame graph against the frame context. The
    // graph is the pass-existence/order authority; no separate scheduler or
    // callback wiring exists in the executor.
    void Execute(
        const RenderGraph& graph,
        const RenderGraphExecutionContext& context
    );
    void Present(
        const SceneFramebuffer& source,
        int destinationWidth,
        int destinationHeight
    );
    void SetConfig(const Renderer::Config& nextConfig) noexcept;
    const Renderer::Config& GetConfig() const noexcept;
    void SetFxaaSettings(const FxaaSettings& settings);
    const FxaaSettings& GetFxaaSettings() const noexcept;
    void Release() noexcept;

private:
    struct MorphCacheEntry
    {
        GLuint buffer = 0;
        std::uint64_t revision = 0;
        std::uint64_t lastUsedFrame = 0;
        std::size_t capacityBytes = 0;
        bool initialized = false;
        bool active = false;
        std::vector<MorphVertexDelta> offsets;
    };

    void DrawPart(
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
    );
    void EnsureOitResources(const SceneFramebuffer& target);
    void EnsureShadowResources();
    void EnsurePresentResources();
    void EnsurePhysicsDebugResources();
    void RenderShadowPass(
        const std::vector<RenderCommand>& commands,
        const std::array<glm::mat4, 4>& lightViews,
        const std::array<glm::mat4, 4>& lightProjections
    );
    void EnsureGroundShadowResources();
    void RenderGroundShadowPass(
        const std::vector<RenderCommand>& commands,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& lightDirection,
        float groundY
    );
    void ExecuteShadowDepth(
        const RenderFramePacket& packet,
        const SceneFramebuffer& target,
        const Camera& camera,
        const glm::mat4& view,
        const glm::mat4& projection,
        const std::vector<RenderCommand>& commands
    );
    void ExecuteGroundReceivers(
        const RenderFramePacket& packet,
        const std::vector<RenderCommand>& commands,
        const Camera& camera,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    void ExecuteMmdGroundShadow(
        const RenderFramePacket& packet,
        const std::vector<RenderCommand>& commands,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    void ExecuteOpaque(
        const RenderFramePacket& packet,
        const std::vector<RenderCommand>& commands,
        const Camera& camera,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    void ExecuteSkybox(
        EnvironmentMap& environment,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    void ExecuteTransparent(
        const RenderFramePacket& packet,
        const SceneFramebuffer& target,
        const std::vector<RenderCommand>& commands,
        const Camera& camera,
        const glm::mat4& view,
        const glm::mat4& projection,
        bool oitEnabled
    );
    void ExecuteOitComposite(const SceneFramebuffer& target);
    void ExecutePhysicsDebug(
        const std::vector<PhysicsDebugLine>& lines,
        const SceneFramebuffer& target,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    void DrawPhysicsDebug(
        const std::vector<PhysicsDebugLine>& lines,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    VAO& VertexArrayFor(Mesh& mesh);
    VAO& SkyboxVertexArrayFor(EnvironmentMap& environment);
    void BeginOitPass(const SceneFramebuffer& target);
    void CompositeOit(const SceneFramebuffer& target);
    void UploadTransforms(
        Program& program,
        const ShaderInterface& shaderInterface,
        const glm::mat4& model,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    void UploadSkinning(
        Program& program,
        const ShaderInterface& shaderInterface,
        const Mesh& mesh,
        const Pose* pose
    );
    void EnsureSkinningResources();
    void UploadMorphing(
        VAO& vertexArray,
        const ShaderInterface& shaderInterface,
        const Mesh& mesh,
        const MorphState* morphState
    );
    void BeginMorphingFrame();
    void ReleaseMorphingCache() noexcept;
    void UploadSceneUniforms(
        Program& program,
        const RenderFramePacket& packet,
        const ShaderInterface& shaderInterface
    );
    void UploadEnvironment(
        Program& program,
        const RenderFramePacket& packet,
        const ShaderInterface& shaderInterface
    );
    void UploadPointLights(
        Program& program,
        const RenderFramePacket& packet,
        const ShaderInterface& shaderInterface
    );
    void UploadDirectionalLights(
        Program& program,
        const RenderFramePacket& packet,
        const ShaderInterface& shaderInterface
    );
    void UploadSpotLights(
        Program& program,
        const RenderFramePacket& packet,
        const ShaderInterface& shaderInterface
    );

private:
    OpenGlRenderDevice* openGl = nullptr;
    GraphicsDevice* device = nullptr;
    Renderer::Config config;
    Framebuffer oitFramebuffer;
    GLuint oitAccumulationTexture = 0;
    GLuint oitRevealageTexture = 0;
    GLuint fullscreenVao = 0;
    GLuint skinningBuffer = 0;
    GLuint skinningTexture = 0;
    int oitWidth = 0;
    int oitHeight = 0;
    GLuint oitDepthAttachment = 0;
    GLuint shadowDepthTexture = 0;
    Framebuffer shadowFramebuffer;
    std::unique_ptr<Shader> shadowShader;
    std::unique_ptr<Program> shadowProgram;
    std::unique_ptr<Shader> groundShadowShader;
    std::unique_ptr<Program> groundShadowProgram;
    int shadowMapSize = 2048;
    int shadowPcfRadius = 1;
    float shadowBias = 0.003f;
    RenderResourceCache* renderCache = nullptr;
    bool independentBlendSupported = false;
    std::size_t maximumSkinningMatrices = 0U;
    const Pose* uploadedPose = nullptr;
    std::uint64_t uploadedPoseRevision = 0U;
    std::unordered_map<
        const MorphState*,
        std::unordered_map<const Mesh*, MorphCacheEntry>
    > morphingCache;
    std::uint64_t morphingFrame = 0U;
    std::unique_ptr<Shader> oitCompositeShader;
    std::unique_ptr<Program> oitCompositeProgram;
    std::unique_ptr<Shader> presentShader;
    std::unique_ptr<Program> presentProgram;
    std::unique_ptr<Shader> physicsDebugShader;
    std::unique_ptr<Program> physicsDebugProgram;
    GLuint physicsDebugVao = 0;
    GLuint physicsDebugBuffer = 0;
    std::size_t physicsDebugCapacityBytes = 0U;
    FxaaSettings fxaaSettings;
    bool shadowStateEnabled = false;
    std::array<glm::mat4, 4> shadowLightViewProjections;
    std::array<float, 5> shadowSplitPositions;
    struct MeshVertexArrayEntry
    {
        std::weak_ptr<const void> lifetime;
        std::unique_ptr<VAO> vertexArray;
    };
    std::unordered_map<const Mesh*, MeshVertexArrayEntry> meshVertexArrays;
    std::unordered_map<const EnvironmentMap*, std::unique_ptr<VAO>>
        skyboxVertexArrays;
};
}  // namespace wisteria
