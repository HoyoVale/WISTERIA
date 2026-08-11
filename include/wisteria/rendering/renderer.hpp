#pragma once

#include "wisteria/rendering/framebuffer.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/scene/scene.hpp"
#include "wisteria/physics/physics_types.hpp"
#include <glad/gl.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace wisteria
{
class Program;
class Shader;
struct ShaderInterface;
class EnvironmentMap;
class Mesh;
class MorphState;
class Pose;
class VAO;
class RenderDevice;
class RenderResourceCache;
class RenderFramePacket;

// Defined in src/rendering/renderer_internal.hpp; forward-declared here so
// private pass methods can reference command lists in the public header.
struct RenderCommand;

struct FxaaSettings
{
    bool enabled = true;
    float minimumContrast = 0.0312f;
    float relativeContrast = 0.125f;
    float subpixelBlending = 0.75f;
};

class Renderer
{
public:
    struct Config
    {
        int shadowMapSize = 2048;
        int shadowPcfRadius = 1;
        bool shadowsEnabled = true;
        bool groundShadowEnabled = true;
        // MMD CSM depth bias (R1-08): exposed so frontends can tune
        // shadow acne vs peter-panning per scene.
        float shadowBias = 0.003f;
    };

    // R2.0 Phase 0B: the renderer consumes the backend-neutral RenderDevice.
    // The current OpenGL implementation downcasts internally (0C migrates
    // mesh/material/texture layers onto device handles).
    explicit Renderer(RenderDevice* device = nullptr);
    ~Renderer();

    void SetConfig(const Config& config) noexcept;
    const Config& GetConfig() const noexcept;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Render(
        Scene& scene,
        const Camera& camera,
        const glm::mat4& projection,
        SceneFramebuffer& target
    );
    // R2.0 Phase 0D Stage 1+2B: packet-only rendering path. All GL work
    // happens here; the packet is the sole frame-data authority (no Scene
    // access below this point). Stage 2B Part 2 executes the frame through
    // the explicit RenderGraph DAG (BuildCurrentRenderGraph +
    // SetPassCallback + Execute) with the existing OpenGL pass bodies.
    void RenderPacket(
        const RenderFramePacket& packet,
        SceneFramebuffer& target
    );
    void Present(
        const SceneFramebuffer& source,
        int destinationWidth,
        int destinationHeight
    );
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
    GraphicsDevice* device = nullptr;
    Config config;
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
    // Default matches the internal ShadowMapResolution constant (2048); the
    // runtime knob WISTERIA_SHADOW_MAP_SIZE overrides this at startup.
    int shadowMapSize = 2048;
    int shadowPcfRadius = 1;
    float shadowBias = 0.003f;
    // R2.0 Phase 0C 6A: per-device cache propagated to CPU-created assets
    // right before their first GPU touch (the Renderer owns the composition
    // moment; assets may have been created before any session existed).
    RenderResourceCache* renderCache = nullptr;
    bool independentBlendSupported = false;
    std::size_t maximumSkinningMatrices = 0;
    const Pose* uploadedPose = nullptr;
    std::uint64_t uploadedPoseRevision = 0;
    std::unordered_map<
        const MorphState*,
        std::unordered_map<const Mesh*, MorphCacheEntry>
    > morphingCache;
    std::uint64_t morphingFrame = 0;
    std::unique_ptr<Shader> oitCompositeShader;
    std::unique_ptr<Program> oitCompositeProgram;
    std::unique_ptr<Shader> presentShader;
    std::unique_ptr<Program> presentProgram;
    std::unique_ptr<Shader> physicsDebugShader;
    std::unique_ptr<Program> physicsDebugProgram;
    GLuint physicsDebugVao = 0;
    GLuint physicsDebugBuffer = 0;
    std::size_t physicsDebugCapacityBytes = 0;
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
