#pragma once

#include "material.hpp"
#include "model.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ImportedTextureData
{
    std::string name;
    TextureData source;
};

struct ImportedMaterialData
{
    std::string name;
    std::optional<std::size_t> baseColorTexture;
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 specularColor{1.0f};
    float shininess = 32.0f;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct ImportedMeshData
{
    std::string name;
    DefaultModelData data;
    std::size_t materialIndex = 0;
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
};

// CPU-only static model import. It never creates OpenGL objects and is safe to
// use before a graphics context exists.
class ModelImporter
{
public:
    ImportedModelData Import(const std::filesystem::path& filePath) const;
};
