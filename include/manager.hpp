#pragma once

#include "material.hpp"
#include "mesh.hpp"
#include "model_asset.hpp"
#include "texture.hpp"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

class Window;

class ResourceManager
{
public:
    ResourceManager() = default;
    ~ResourceManager() = default;

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) noexcept = default;
    ResourceManager& operator=(ResourceManager&&) noexcept = default;

    Mesh& CreateMesh(const std::string& name, const DefaultModelData& data);
    Material& CreateMaterial(
        const std::string& name,
        const MaterialData& data = {}
    );
    Texture& CreateTexture(const std::string& name, TextureData data);
    ModelAsset& CreateModel(const std::string& name);
    ModelAsset& LoadModel(const std::string& name,const std::filesystem::path& filePath);

    Mesh* FindMesh(const std::string& name) noexcept;
    const Mesh* FindMesh(const std::string& name) const noexcept;
    Material* FindMaterial(const std::string& name) noexcept;
    const Material* FindMaterial(const std::string& name) const noexcept;
    Texture* FindTexture(const std::string& name) noexcept;
    const Texture* FindTexture(const std::string& name) const noexcept;
    ModelAsset* FindModel(const std::string& name) noexcept;
    const ModelAsset* FindModel(const std::string& name) const noexcept;

    Mesh& GetMesh(const std::string& name);
    const Mesh& GetMesh(const std::string& name) const;
    Material& GetMaterial(const std::string& name);
    const Material& GetMaterial(const std::string& name) const;
    Texture& GetTexture(const std::string& name);
    const Texture& GetTexture(const std::string& name) const;
    ModelAsset& GetModel(const std::string& name);
    const ModelAsset& GetModel(const std::string& name) const;

    std::size_t MeshCount() const noexcept;
    std::size_t MaterialCount() const noexcept;
    std::size_t TextureCount() const noexcept;
    std::size_t ModelCount() const noexcept;

private:
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
    std::unordered_map<std::string, std::unique_ptr<ModelAsset>> models;

    // Resources may only be released during Window shutdown, after Scene is clear.
    void Clear() noexcept;

    friend class Window;
};
