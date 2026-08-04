#pragma once
#include "wisteria/rendering/shader.hpp"
#include "wisteria/rendering/program_cache.hpp"
#include "wisteria/core/asset_paths.hpp"
#include "wisteria/rendering/texture.hpp"
#include <string>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <unordered_map>

enum class TransformUniformMode
{
    SeparateModelViewProjection,
    CombinedTransform
};

enum class MaterialAlphaMode
{
    Opaque = 0,
    Mask = 1,
    Blend = 2
};

enum class MaterialShadingModel
{
    PbrMetallicRoughness,
    MmdToon
};

enum class MmdSphereMapMode
{
    Disabled = 0,
    Multiply = 1,
    Add = 2,
    SubTexture = 3
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

    bool skinningSupported = true;
    std::string skinningEnabled = "skinningEnabled";
    std::string boneMatrixPalette = "boneMatrixPalette";
    bool morphingSupported = true;

    bool lightingEnabled = true;
    std::string cameraPosition = "cameraPosition";
    std::string materialSpecularColor = "materialSpecularColor";
    std::string materialShininess = "materialShininess";
    std::string materialBaseColorFactor = "materialBaseColorFactor";
    std::string materialAlphaMode = "materialAlphaMode";
    std::string materialAlphaCutoff = "materialAlphaCutoff";
    std::string oitPass = "oitPass";
    std::string hasBaseTexture = "hasBaseTexture";
    std::string baseColorTexture = "baseColorTexture";
    std::string hasNormalTexture = "hasNormalTexture";
    std::string normalTexture = "normalTexture";
    std::string materialNormalScale = "materialNormalScale";
    std::string materialMetallicFactor = "materialMetallicFactor";
    std::string materialRoughnessFactor = "materialRoughnessFactor";
    std::string materialEmissiveFactor = "materialEmissiveFactor";
    std::string materialOcclusionStrength = "materialOcclusionStrength";
    std::string hasMetallicRoughnessTexture ="hasMetallicRoughnessTexture";
    std::string metallicRoughnessTexture = "metallicRoughnessTexture";
    std::string hasEmissiveTexture = "hasEmissiveTexture";
    std::string emissiveTexture = "emissiveTexture";
    std::string hasOcclusionTexture = "hasOcclusionTexture";
    std::string occlusionTexture = "occlusionTexture";
    std::string ambientStrength = "ambientStrength";
    bool imageBasedLightingEnabled = true;
    std::string hasEnvironment = "hasEnvironment";
    std::string irradianceMap = "irradianceMap";
    std::string prefilterMap = "prefilterMap";
    std::string brdfLut = "brdfLut";
    std::string environmentIntensity = "environmentIntensity";
    std::string maxReflectionLod = "maxReflectionLod";
    std::string materialAmbientColor = "materialAmbientColor";
    std::string hasSphereTexture = "hasSphereTexture";
    std::string sphereTexture = "sphereTexture";
    std::string sphereMapMode = "sphereMapMode";
    std::string hasToonTexture = "hasToonTexture";
    std::string toonTexture = "toonTexture";
    std::string outlinePass = "outlinePass";
    std::string materialEdgeColor = "materialEdgeColor";
    std::string materialEdgeSize = "materialEdgeSize";
    std::string materialTextureFactor = "materialTextureFactor";
    std::string materialSphereTextureFactor = "materialSphereTextureFactor";
    std::string materialToonTextureFactor = "materialToonTextureFactor";

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
    MaterialShadingModel shadingModel =
        MaterialShadingModel::PbrMetallicRoughness;
    std::unordered_map<std::string, TextureData> textureSources = {
        {
            "baseColorTexture",
            TextureData::FromFile(
                wisteria::assets::Texture("chessboard.png")
            )
        }
    };
    glm::vec4 baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 specularColor = {1.0f, 1.0f, 1.0f}; // Specular reflection color.
    float shininess = 32.0f; // Higher values make a sharper highlight.
    float normalScale = 1.0f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor = {0.0f, 0.0f, 0.0f};
    float occlusionStrength = 1.0f;
    glm::vec3 ambientColor = {0.0f, 0.0f, 0.0f};
    MmdSphereMapMode sphereMapMode = MmdSphereMapMode::Disabled;
    glm::vec4 edgeColor = {0.0f, 0.0f, 0.0f, 1.0f};
    float edgeSize = 0.0f;
    bool edgeEnabled = false;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    ShaderInterface shaderInterface;
};

using MaterialTextureBindings =
    std::unordered_map<std::string, std::shared_ptr<Texture>>;

class Material{
public:
    explicit Material(const MaterialData &_data = {});
    Material(
        const MaterialData& data,
        std::shared_ptr<ProgramCache> programCache
    );
    Material(
        const MaterialData& data,
        MaterialTextureBindings textureBindings
    );
    Material(
        const MaterialData& data,
        MaterialTextureBindings textureBindings,
        std::shared_ptr<ProgramCache> programCache
    );
    ~Material() = default;

    void Attach();
    void Bind();
    void Unbind();

    Program& GetProgram();
    const Program& GetProgram() const;


    const glm::vec3& SpecularColor() const noexcept;
    float Shininess() const noexcept;
    float NormalScale() const noexcept;
    float MetallicFactor() const noexcept;
    float RoughnessFactor() const noexcept;
    const glm::vec3& EmissiveFactor() const noexcept;
    float OcclusionStrength() const noexcept;
    MaterialShadingModel ShadingModel() const noexcept;
    const glm::vec3& AmbientColor() const noexcept;
    MmdSphereMapMode SphereMapMode() const noexcept;
    const glm::vec4& EdgeColor() const noexcept;
    float EdgeSize() const noexcept;
    bool IsEdgeEnabled() const noexcept;
    const glm::vec4& BaseColorFactor() const noexcept;
    MaterialAlphaMode AlphaMode() const noexcept;
    float AlphaCutoff() const noexcept;
    bool IsDoubleSided() const noexcept;
    bool HasTexture(const std::string& uniformName) const noexcept;
    const ShaderInterface& Interface() const noexcept;

    void SetSpecularColor(const glm::vec3& color) noexcept;
    void SetShininess(float shininess) noexcept;
    void SetNormalScale(float normalScale) noexcept;
    void SetMetallicFactor(float metallicFactor) noexcept;
    void SetRoughnessFactor(float roughnessFactor) noexcept;
    void SetEmissiveFactor(const glm::vec3& emissiveFactor) noexcept;
    void SetOcclusionStrength(float occlusionStrength) noexcept;

private:
    std::shared_ptr<ProgramCache> programCache;
    std::shared_ptr<Program> program;
    MaterialTextureBindings textures;
    MaterialData data;
};
