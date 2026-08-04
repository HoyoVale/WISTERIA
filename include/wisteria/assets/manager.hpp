#pragma once

#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/mesh.hpp"
#include "wisteria/assets/model_asset.hpp"
#include "wisteria/assets/importer.hpp"
#include "wisteria/rendering/texture.hpp"
#include "wisteria/rendering/environment.hpp"
#include "wisteria/mmd/vmd_importer.hpp"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace wisteria
{
class Application;

struct TexturePathKey
{
    std::filesystem::path path;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;

    bool operator==(const TexturePathKey&) const noexcept = default;
};

struct TexturePathKeyHash
{
    std::size_t operator()(const TexturePathKey& key) const noexcept
    {
        const std::size_t pathHash =
            std::hash<std::filesystem::path>{}(key.path);
        const std::size_t colorSpaceHash =
            std::hash<int>{}(static_cast<int>(key.colorSpace));
        return pathHash ^ (colorSpaceHash + 0x9e3779b9U +
            (pathHash << 6U) + (pathHash >> 2U));
    }
};

class ResourceManager
{
public:
    ResourceManager() = default;
    ~ResourceManager() = default;

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) noexcept = default;
    ResourceManager& operator=(ResourceManager&&) noexcept = default;

    Mesh& CreateMesh(
        const std::string& name,
        const DefaultModelData& data,
        std::size_t requiredBoneCount = 0U,
        std::vector<MeshMorphTarget> morphTargets = {}
    );
    Material& CreateMaterial(
        const std::string& name,
        const MaterialData& data = {}
    );
    Material& CreateMaterial(
        const std::string& name,
        const MaterialData& data,
        MaterialTextureBindings bindings
    );
    Texture& CreateTexture(const std::string& name, TextureData data);
    std::shared_ptr<Texture> CreateTextureShared(
        const std::string& name,
        TextureData data
    );
    ModelAsset& CreateModel(const std::string& name);
    // Builds a complete ModelAsset from an already-imported model. Reuses the
    // same texture path cache / material binding pipeline as LoadModel.
    ModelAsset& CreateModel(
        const std::string& name,
        ImportedModelData imported
    );
    ModelAsset& LoadModel(
        const std::string& name,
        const std::filesystem::path& filePath
    );
    AnimationClip& LoadVmdAnimation(
        ModelAsset& model,
        const std::filesystem::path& filePath,
        const VmdImportOptions& options = {}
    );
    EnvironmentMap& CreateEnvironment(
        const std::string& name,
        const EnvironmentMapData& data = {}
    );

    Mesh* FindMesh(const std::string& name) noexcept;
    const Mesh* FindMesh(const std::string& name) const noexcept;
    Material* FindMaterial(const std::string& name) noexcept;
    const Material* FindMaterial(const std::string& name) const noexcept;
    Texture* FindTexture(const std::string& name) noexcept;
    const Texture* FindTexture(const std::string& name) const noexcept;
    Texture* FindTextureByPath(
        const std::filesystem::path& filePath,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb
    );
    const Texture* FindTextureByPath(
        const std::filesystem::path& filePath,
        TextureColorSpace colorSpace = TextureColorSpace::Srgb
    ) const;
    ModelAsset* FindModel(const std::string& name) noexcept;
    const ModelAsset* FindModel(const std::string& name) const noexcept;
    ModelAsset* FindModelByPath(const std::filesystem::path& filePath);
    const ModelAsset* FindModelByPath(
        const std::filesystem::path& filePath
    ) const;
    EnvironmentMap* FindEnvironment(const std::string& name) noexcept;
    const EnvironmentMap* FindEnvironment(
        const std::string& name
    ) const noexcept;

    Mesh& GetMesh(const std::string& name);
    const Mesh& GetMesh(const std::string& name) const;
    Material& GetMaterial(const std::string& name);
    const Material& GetMaterial(const std::string& name) const;
    Texture& GetTexture(const std::string& name);
    const Texture& GetTexture(const std::string& name) const;
    ModelAsset& GetModel(const std::string& name);
    const ModelAsset& GetModel(const std::string& name) const;
    EnvironmentMap& GetEnvironment(const std::string& name);
    const EnvironmentMap& GetEnvironment(const std::string& name) const;

    std::size_t MeshCount() const noexcept;
    std::size_t MaterialCount() const noexcept;
    std::size_t TextureCount() const noexcept;
    std::size_t ModelCount() const noexcept;
    std::size_t EnvironmentCount() const noexcept;

private:
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
    std::unordered_map<std::string, std::unique_ptr<ModelAsset>> models;
    std::unordered_map<std::string, std::unique_ptr<EnvironmentMap>> environments;
    std::unordered_map<
        TexturePathKey,
        std::weak_ptr<Texture>,
        TexturePathKeyHash
    > texturePathCache;
    std::unordered_map<std::filesystem::path, ModelAsset*> modelPathCache;

    // Resources may only be released by Application after every Scene and
    // context-local renderer cache has been cleared while a shared context lives.
    void Clear() noexcept;

    friend class Application;
};
}  // namespace wisteria
