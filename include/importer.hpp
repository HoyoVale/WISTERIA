#pragma once

#include "material.hpp"
#include "model.hpp"
#include "skeleton.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ImportedTextureData
{
    std::string name;
    TextureData source;
    std::optional<bool> hasNonOpaqueAlpha;
};

struct ImportedMaterialData
{
    std::string name;
    MaterialShadingModel shadingModel =
        MaterialShadingModel::PbrMetallicRoughness;
    std::optional<std::size_t> baseColorTexture;
    std::optional<std::size_t> normalTexture;
    std::optional<std::size_t> metallicRoughnessTexture;
    std::optional<std::size_t> emissiveTexture;
    std::optional<std::size_t> occlusionTexture;
    std::optional<std::size_t> sphereTexture;
    std::optional<std::size_t> toonTexture;
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 specularColor{1.0f};
    float shininess = 32.0f;
    float normalScale = 1.0f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor{0.0f};
    float occlusionStrength = 1.0f;
    glm::vec3 ambientColor{0.0f};
    MmdSphereMapMode sphereMapMode = MmdSphereMapMode::Disabled;
    glm::vec4 edgeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float edgeSize = 0.0f;
    bool edgeEnabled = false;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct ImportedMeshData
{
    std::string name;
    DefaultModelData data;
    std::size_t materialIndex = 0;
    std::size_t requiredBoneCount = 0;
};

struct ImportedPartData
{
    std::string name;
    std::size_t meshIndex = 0;
    glm::mat4 localTransform{1.0f};
};

struct ImportedModelData
{
    std::vector<ImportedTextureData> textures;
    std::vector<ImportedMaterialData> materials;
    std::vector<ImportedMeshData> meshes;
    std::vector<ImportedPartData> parts;
    std::optional<Skeleton> skeleton;
};

// CPU-only model import. It never creates OpenGL objects and is safe to use
// before a graphics context exists.
class ModelImporter
{
public:
    ImportedModelData Import(const std::filesystem::path& filePath) const;
};
