#pragma once

#include "wisteria/assets/importer.hpp"
#include "wisteria/assets/model_asset.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/rendering/texture.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace wisteria
{
// R1.9 Final Fix: engine-owned CPU assembly of an ImportedModelData into a
// ModelAsset plus its meshes, materials and textures. ResourceManager and
// the stable C ABI reuse this single pipeline so material/texture channels
// never diverge between the two paths.
//
// `textures` must parallel `imported.textures` (index-aligned); the caller
// owns texture policy (ResourceManager path caching, stable direct
// creation). Meshes/materials are created with the given GraphicsDevice
// (nullptr = CPU-only assembly; GL objects attach lazily with a context
// current).
struct ModelAssetBundle
{
    std::unique_ptr<ModelAsset> asset;
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::vector<std::unique_ptr<Material>> materials;
    std::vector<std::shared_ptr<Texture>> textures;
};

ModelAssetBundle BuildModelAssetBundle(
    ImportedModelData imported,
    std::vector<std::shared_ptr<Texture>> textures,
    ModelBackendKind backendKind,
    const std::filesystem::path& sourcePath,
    const std::string& name,
    GraphicsDevice* device
);
}  // namespace wisteria
