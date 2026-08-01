#pragma once

#include "framebuffer.hpp"
#include "scene.hpp"
#include <glad/gl.h>
#include <memory>

class Program;
class Shader;
struct ShaderInterface;
class EnvironmentMap;

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
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Render(
        Scene& scene,
        const glm::mat4& projection,
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
    void DrawPart(
        RenderPart& part,
        const glm::mat4& model,
        const glm::mat4& view,
        const glm::mat4& projection,
        const Camera& camera,
        const Scene& scene,
        int oitPass
    );
    void EnsureOitResources(const SceneFramebuffer& target);
    void EnsurePresentResources();
    void BeginOitPass(const SceneFramebuffer& target);
    void CompositeOit(const SceneFramebuffer& target);
    void UploadTransforms(
        Program& program,
        const ShaderInterface& shaderInterface,
        const glm::mat4& model,
        const glm::mat4& view,
        const glm::mat4& projection
    );
    void UploadSceneUniforms(
        Program& program,
        const Scene& scene,
        const ShaderInterface& shaderInterface
    );
    void UploadEnvironment(
        Program& program,
        const Scene& scene,
        const ShaderInterface& shaderInterface
    );
    void UploadPointLights(
        Program& program,
        const Scene& scene,
        const ShaderInterface& shaderInterface
    );
    void UploadDirectionalLights(
        Program& program,
        const Scene& scene,
        const ShaderInterface& shaderInterface
    );
    void UploadSpotLights(
        Program& program,
        const Scene& scene,
        const ShaderInterface& shaderInterface
    );

private:
    Framebuffer oitFramebuffer;
    GLuint oitAccumulationTexture = 0;
    GLuint oitRevealageTexture = 0;
    GLuint fullscreenVao = 0;
    int oitWidth = 0;
    int oitHeight = 0;
    GLuint oitDepthAttachment = 0;
    bool independentBlendSupported = false;
    std::unique_ptr<Shader> oitCompositeShader;
    std::unique_ptr<Program> oitCompositeProgram;
    std::unique_ptr<Shader> presentShader;
    std::unique_ptr<Program> presentProgram;
    FxaaSettings fxaaSettings;
};
