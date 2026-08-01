#pragma once

#include "scene.hpp"
#include <glad/gl.h>
#include <memory>

class Program;
class Shader;
struct ShaderInterface;
class EnvironmentMap;

class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Render(Scene& scene, const glm::mat4& projection);
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
    void EnsureOitResources(int width, int height);
    void BeginOitPass(int width, int height);
    void CompositeOit();
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
    GLuint oitFramebuffer = 0;
    GLuint oitAccumulationTexture = 0;
    GLuint oitRevealageTexture = 0;
    GLuint oitDepthRenderbuffer = 0;
    GLuint oitFullscreenVao = 0;
    int oitWidth = 0;
    int oitHeight = 0;
    bool independentBlendSupported = false;
    std::unique_ptr<Shader> oitCompositeShader;
    std::unique_ptr<Program> oitCompositeProgram;
};
