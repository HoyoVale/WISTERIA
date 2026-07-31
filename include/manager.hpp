#pragma once

#include "material.hpp"
#include "mesh.hpp"
#include <memory>
#include <string>
#include <unordered_map>

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

    Mesh* FindMesh(const std::string& name) noexcept;
    const Mesh* FindMesh(const std::string& name) const noexcept;
    Material* FindMaterial(const std::string& name) noexcept;
    const Material* FindMaterial(const std::string& name) const noexcept;

    Mesh& GetMesh(const std::string& name);
    const Mesh& GetMesh(const std::string& name) const;
    Material& GetMaterial(const std::string& name);
    const Material& GetMaterial(const std::string& name) const;

    bool RemoveMesh(const std::string& name);
    bool RemoveMaterial(const std::string& name);
    void Clear() noexcept;

private:
    struct ManagedMesh
    {
        explicit ManagedMesh(const DefaultModelData& source);

        DefaultModelData data;
        Mesh mesh;
    };

    std::unordered_map<std::string, std::unique_ptr<ManagedMesh>> meshes;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
};
