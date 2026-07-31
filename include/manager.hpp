#pragma once

#include "material.hpp"
#include "mesh.hpp"
#include "model_asset.hpp"
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
    ModelAsset& CreateModel(const std::string& name);

    Mesh* FindMesh(const std::string& name) noexcept;
    const Mesh* FindMesh(const std::string& name) const noexcept;
    Material* FindMaterial(const std::string& name) noexcept;
    const Material* FindMaterial(const std::string& name) const noexcept;
    ModelAsset* FindModel(const std::string& name) noexcept;
    const ModelAsset* FindModel(const std::string& name) const noexcept;

    Mesh& GetMesh(const std::string& name);
    const Mesh& GetMesh(const std::string& name) const;
    Material& GetMaterial(const std::string& name);
    const Material& GetMaterial(const std::string& name) const;
    ModelAsset& GetModel(const std::string& name);
    const ModelAsset& GetModel(const std::string& name) const;

private:
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, std::unique_ptr<ModelAsset>> models;

    // Resources may only be released during Window shutdown, after Scene is clear.
    void Clear() noexcept;

    friend class Window;
};
