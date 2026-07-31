#include "pch.hpp"
#include "manager.hpp"

Mesh& ResourceManager::CreateMesh(
    const std::string& name,
    const DefaultModelData& data
)
{
    if (this->meshes.contains(name))
        throw std::invalid_argument("Mesh resource already exists: " + name);

    auto resource = std::make_unique<Mesh>(data);
    Mesh& result = *resource;
    this->meshes.emplace(name, std::move(resource));
    return result;
}

Material& ResourceManager::CreateMaterial(
    const std::string& name,
    const MaterialData& data
)
{
    if (this->materials.contains(name))
        throw std::invalid_argument("Material resource already exists: " + name);

    auto material = std::make_unique<Material>(data);
    Material& result = *material;
    this->materials.emplace(name, std::move(material));
    return result;
}

Mesh* ResourceManager::FindMesh(const std::string& name) noexcept
{
    const auto iterator = this->meshes.find(name);
    return iterator != this->meshes.end() ? iterator->second.get() : nullptr;
}

const Mesh* ResourceManager::FindMesh(const std::string& name) const noexcept
{
    const auto iterator = this->meshes.find(name);
    return iterator != this->meshes.end() ? iterator->second.get() : nullptr;
}

Material* ResourceManager::FindMaterial(const std::string& name) noexcept
{
    const auto iterator = this->materials.find(name);
    return iterator != this->materials.end() ? iterator->second.get() : nullptr;
}

const Material* ResourceManager::FindMaterial(
    const std::string& name
) const noexcept
{
    const auto iterator = this->materials.find(name);
    return iterator != this->materials.end() ? iterator->second.get() : nullptr;
}

Mesh& ResourceManager::GetMesh(const std::string& name)
{
    Mesh* mesh = this->FindMesh(name);
    if (mesh == nullptr)
        throw std::out_of_range("Mesh resource was not found: " + name);

    return *mesh;
}

const Mesh& ResourceManager::GetMesh(const std::string& name) const
{
    const Mesh* mesh = this->FindMesh(name);
    if (mesh == nullptr)
        throw std::out_of_range("Mesh resource was not found: " + name);

    return *mesh;
}

Material& ResourceManager::GetMaterial(const std::string& name)
{
    Material* material = this->FindMaterial(name);
    if (material == nullptr)
        throw std::out_of_range("Material resource was not found: " + name);

    return *material;
}

const Material& ResourceManager::GetMaterial(const std::string& name) const
{
    const Material* material = this->FindMaterial(name);
    if (material == nullptr)
        throw std::out_of_range("Material resource was not found: " + name);

    return *material;
}

void ResourceManager::Clear() noexcept
{
    this->materials.clear();
    this->meshes.clear();
}
