#include "pch.hpp"
#include "manager.hpp"
#include "importer.hpp"
#include <utility>

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

Texture& ResourceManager::CreateTexture(
    const std::string& name,
    TextureData data
)
{
    if (this->textures.contains(name))
        throw std::invalid_argument("Texture resource already exists: " + name);

    auto texture = std::make_shared<Texture>(std::move(data));
    Texture& result = *texture;
    this->textures.emplace(name, std::move(texture));
    return result;
}

ModelAsset& ResourceManager::CreateModel(const std::string& name)
{
    if (this->models.contains(name))
        throw std::invalid_argument("Model resource already exists: " + name);

    auto model = std::make_unique<ModelAsset>(name);
    ModelAsset& result = *model;
    this->models.emplace(name, std::move(model));
    return result;
}

ModelAsset& ResourceManager::LoadModel(
    const std::string& name,
    const std::filesystem::path& filePath
)
{
    if (name.empty())
        throw std::invalid_argument("Model resource name must not be empty");
    if (this->models.contains(name))
        throw std::invalid_argument("Model resource already exists: " + name);

    ImportedModelData imported = ModelImporter().Import(filePath);

    std::vector<std::string> textureNames;
    std::vector<std::string> materialNames;
    std::vector<std::string> meshNames;
    textureNames.reserve(imported.textures.size());
    materialNames.reserve(imported.materials.size());
    meshNames.reserve(imported.meshes.size());

    for (std::size_t index = 0; index < imported.textures.size(); ++index)
    {
        const std::string resourceName =
            name + "::texture::" + std::to_string(index);
        if (this->textures.contains(resourceName))
            throw std::invalid_argument("Texture resource already exists: " + resourceName);
        textureNames.push_back(resourceName);
    }
    for (std::size_t index = 0; index < imported.materials.size(); ++index)
    {
        const std::string resourceName =
            name + "::material::" + std::to_string(index);
        if (this->materials.contains(resourceName))
            throw std::invalid_argument("Material resource already exists: " + resourceName);
        materialNames.push_back(resourceName);

        const std::optional<std::size_t> textureIndex =
            imported.materials[index].baseColorTexture;
        if (textureIndex.has_value() && *textureIndex >= imported.textures.size())
            throw std::runtime_error("Imported material references an invalid texture index");
    }
    for (std::size_t index = 0; index < imported.meshes.size(); ++index)
    {
        const std::string resourceName =
            name + "::mesh::" + std::to_string(index);
        if (this->meshes.contains(resourceName))
            throw std::invalid_argument("Mesh resource already exists: " + resourceName);
        if (imported.meshes[index].materialIndex >= imported.materials.size())
            throw std::runtime_error("Imported mesh references an invalid material index");
        meshNames.push_back(resourceName);
    }
    for (const ImportedPartData& part : imported.parts)
    {
        if (part.meshIndex >= imported.meshes.size())
            throw std::runtime_error("Imported part references an invalid mesh index");
    }

    std::vector<std::shared_ptr<Texture>> importedTextures;
    importedTextures.reserve(imported.textures.size());
    for (std::size_t index = 0; index < imported.textures.size(); ++index)
    {
        auto texture = std::make_shared<Texture>(
            std::move(imported.textures[index].source)
        );
        importedTextures.push_back(std::move(texture));
    }

    std::vector<std::unique_ptr<Material>> importedMaterials;
    importedMaterials.reserve(imported.materials.size());
    for (std::size_t index = 0; index < imported.materials.size(); ++index)
    {
        const ImportedMaterialData& source = imported.materials[index];
        MaterialData data;
        data.textureSources.clear();
        data.baseColorFactor = source.baseColorFactor;
        data.specularColor = source.specularColor;
        data.shininess = source.shininess;
        data.alphaMode = source.alphaMode;
        data.alphaCutoff = source.alphaCutoff;
        data.doubleSided = source.doubleSided;

        MaterialTextureBindings bindings;
        if (source.baseColorTexture.has_value())
        {
            const std::size_t textureIndex = *source.baseColorTexture;
            bindings.emplace(
                data.shaderInterface.baseColorTexture,
                importedTextures[textureIndex]
            );
        }

        importedMaterials.push_back(
            std::make_unique<Material>(data, std::move(bindings))
        );
    }

    std::vector<std::unique_ptr<Mesh>> importedMeshes;
    importedMeshes.reserve(imported.meshes.size());
    for (std::size_t index = 0; index < imported.meshes.size(); ++index)
    {
        importedMeshes.push_back(
            std::make_unique<Mesh>(std::move(imported.meshes[index].data))
        );
    }

    auto model = std::make_unique<ModelAsset>(name);
    for (const ImportedPartData& part : imported.parts)
    {
        const std::size_t materialIndex =
            imported.meshes[part.meshIndex].materialIndex;

        model->AddPart(
            *importedMeshes[part.meshIndex].get(),
            *importedMaterials[materialIndex].get(),
            part.localTransform
        );
    }

    // Reserve first and then commit. If any insertion still fails, erase only
    // resources inserted by this call so the manager never keeps half a model.
    this->textures.reserve(this->textures.size() + importedTextures.size());
    this->materials.reserve(this->materials.size() + importedMaterials.size());
    this->meshes.reserve(this->meshes.size() + importedMeshes.size());
    this->models.reserve(this->models.size() + 1);

    std::size_t textureCommitCount = 0;
    std::size_t materialCommitCount = 0;
    std::size_t meshCommitCount = 0;
    try
    {
        for (std::size_t index = 0; index < importedTextures.size(); ++index)
        {
            this->textures.emplace(textureNames[index], importedTextures[index]);
            ++textureCommitCount;
        }
        for (std::size_t index = 0; index < importedMaterials.size(); ++index)
        {
            this->materials.emplace(
                materialNames[index],
                std::move(importedMaterials[index])
            );
            ++materialCommitCount;
        }
        for (std::size_t index = 0; index < importedMeshes.size(); ++index)
        {
            this->meshes.emplace(meshNames[index], std::move(importedMeshes[index]));
            ++meshCommitCount;
        }

        ModelAsset& result = *model;
        this->models.emplace(name, std::move(model));
        return result;
    }
    catch (...)
    {
        this->models.erase(name);
        for (std::size_t index = 0; index < materialCommitCount; ++index)
            this->materials.erase(materialNames[index]);
        for (std::size_t index = 0; index < meshCommitCount; ++index)
            this->meshes.erase(meshNames[index]);
        for (std::size_t index = 0; index < textureCommitCount; ++index)
            this->textures.erase(textureNames[index]);
        throw;
    }
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

Texture* ResourceManager::FindTexture(const std::string& name) noexcept
{
    const auto iterator = this->textures.find(name);
    return iterator != this->textures.end() ? iterator->second.get() : nullptr;
}

const Texture* ResourceManager::FindTexture(const std::string& name) const noexcept
{
    const auto iterator = this->textures.find(name);
    return iterator != this->textures.end() ? iterator->second.get() : nullptr;
}

ModelAsset* ResourceManager::FindModel(const std::string& name) noexcept
{
    const auto iterator = this->models.find(name);
    return iterator != this->models.end() ? iterator->second.get() : nullptr;
}

const ModelAsset* ResourceManager::FindModel(
    const std::string& name
) const noexcept
{
    const auto iterator = this->models.find(name);
    return iterator != this->models.end() ? iterator->second.get() : nullptr;
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

Texture& ResourceManager::GetTexture(const std::string& name)
{
    Texture* texture = this->FindTexture(name);
    if (texture == nullptr)
        throw std::out_of_range("Texture resource was not found: " + name);
    return *texture;
}

const Texture& ResourceManager::GetTexture(const std::string& name) const
{
    const Texture* texture = this->FindTexture(name);
    if (texture == nullptr)
        throw std::out_of_range("Texture resource was not found: " + name);
    return *texture;
}

ModelAsset& ResourceManager::GetModel(const std::string& name)
{
    ModelAsset* model = this->FindModel(name);
    if (model == nullptr)
        throw std::out_of_range("Model resource was not found: " + name);

    return *model;
}

const ModelAsset& ResourceManager::GetModel(const std::string& name) const
{
    const ModelAsset* model = this->FindModel(name);
    if (model == nullptr)
        throw std::out_of_range("Model resource was not found: " + name);

    return *model;
}

std::size_t ResourceManager::MeshCount() const noexcept
{
    return this->meshes.size();
}

std::size_t ResourceManager::MaterialCount() const noexcept
{
    return this->materials.size();
}

std::size_t ResourceManager::TextureCount() const noexcept
{
    return this->textures.size();
}

std::size_t ResourceManager::ModelCount() const noexcept
{
    return this->models.size();
}

void ResourceManager::Clear() noexcept
{
    this->models.clear();
    this->materials.clear();
    this->textures.clear();
    this->meshes.clear();
}
