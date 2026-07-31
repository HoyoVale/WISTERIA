#pragma once
#include "shader.hpp"
#include "texture.hpp"
#include <string>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <unordered_map>

inline const std::string textureRootPath =
    (std::filesystem::current_path() / "assets" / "textures").string() + "\\";

enum class TransformUniformMode
{
    SeparateModelViewProjection,
    CombinedTransform
};

// Describes the uniform contract implemented by a material's shader.
// Custom shaders can change names, capacities, or disable lighting entirely.
struct ShaderInterface
{
    TransformUniformMode transformMode =
        TransformUniformMode::SeparateModelViewProjection;

    std::string model = "model";
    std::string view = "view";
    std::string projection = "projection";
    std::string combinedTransform = "transform";

    bool lightingEnabled = true;
    std::string cameraPosition = "cameraPosition";
    std::string materialSpecularColor = "materialSpecularColor";
    std::string materialShininess = "materialShininess";
    std::string ambientStrength = "ambientStrength";

    std::string pointLights = "pointLights";
    std::string pointLightCount = "pointLightCount";
    std::size_t maxPointLights = 8;

    std::string directionalLights = "directionalLights";
    std::string directionalLightCount = "directionalLightCount";
    std::size_t maxDirectionalLights = 4;

    std::string spotLights = "spotLights";
    std::string spotLightCount = "spotLightCount";
    std::size_t maxSpotLights = 4;

    std::string lightPositionField = "position";
    std::string lightDirectionField = "direction";
    std::string lightRadianceField = "radiance";
    std::string lightRangeField = "range";
    std::string lightConstantField = "constant";
    std::string lightLinearField = "linear";
    std::string lightQuadraticField = "quadratic";
    std::string spotInnerCutoffField = "innerCutoff";
    std::string spotOuterCutoffField = "outerCutoff";
};

struct MaterialData{
    Path shaderFilePath;
    std::unordered_map<std::string, std::string> textureFilePath = {
        {"texture", textureRootPath + "chessboard.png"}
    };
    glm::vec3 specularColor = {1.0f, 1.0f, 1.0f}; // Specular reflection color.
    float shininess = 32.0f; // Higher values make a sharper highlight.
    ShaderInterface shaderInterface;
};

class Material{
public:
    explicit Material(const MaterialData &_data = {});
    ~Material() = default;

    void Attach();
    void Bind();
    void Unbind();

    Program& GetProgram();
    const Program& GetProgram() const;

    const glm::vec3& SpecularColor() const noexcept;
    float Shininess() const noexcept;
    const ShaderInterface& Interface() const noexcept;

    void SetSpecularColor(const glm::vec3& color) noexcept;
    void SetShininess(float shininess) noexcept;

private:
    std::unique_ptr<Shader> shader;
    std::unique_ptr<Program> program;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    MaterialData data;
};
