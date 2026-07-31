#pragma once

#include "scene.hpp"
#include <cstddef>

class Program;

class Renderer
{
public:
    static constexpr std::size_t MaxPointLights = 8;
    static constexpr std::size_t MaxDirectionalLights = 4;
    static constexpr std::size_t MaxSpotLights = 4;

    void Render(Scene& scene, const glm::mat4& projection);

private:
    void UploadSceneUniforms(Program& program, const Scene& scene);
    void UploadPointLights(Program& program, const Scene& scene);
    void UploadDirectionalLights(Program& program, const Scene& scene);
    void UploadSpotLights(Program& program, const Scene& scene);
};
