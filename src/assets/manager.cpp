#include "wisteria/common/pch.hpp"
#include "wisteria/assets/manager.hpp"
#include "wisteria/assets/model_asset_bundle.hpp"
#include "wisteria/assets/importer.hpp"
#include "wisteria/assets/saba_mmd_importer.hpp"
#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace wisteria
{
namespace
{
std::filesystem::path NormalizeResourcePath(
    const std::filesystem::path& filePath
)
{
    if (filePath.empty())
        throw std::invalid_argument("Resource file path must not be empty");

    return std::filesystem::weakly_canonical(
        std::filesystem::absolute(filePath)
    );
}

std::string LowerExtension(const std::filesystem::path& filePath)
{
    std::string extension = filePath.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        }
    );
    return extension;
}
}

void ResourceManager::BindGraphicsDevice(GraphicsDevice& device)
{
    this->graphicsDevice = &device;
    this->programCache = device.Programs();
}

Mesh& ResourceManager::CreateMesh(
    const std::string& name,
    const DefaultModelData& data,
    std::size_t requiredBoneCount,
    std::vector<MeshMorphTarget> morphTargets
)
{
    if (this->meshes.contains(name))
        throw std::invalid_argument("Mesh resource already exists: " + name);

    auto resource = std::make_unique<Mesh>(
        data,
        requiredBoneCount,
        std::move(morphTargets),
        std::vector<std::uint32_t>{},
        this->graphicsDevice
    );
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

    auto material = std::make_unique<Material>(
        data,
        this->programCache,
        this->graphicsDevice
    );
    Material& result = *material;
    this->materials.emplace(name, std::move(material));
    return result;
}

Texture& ResourceManager::CreateTexture(
    const std::string& name,
    TextureData data
)
{
    return *this->CreateTextureShared(name, std::move(data));
}

std::shared_ptr<Texture> ResourceManager::CreateTextureShared(
    const std::string& name,
    TextureData data
)
{
    if (this->textures.contains(name))
        throw std::invalid_argument("Texture resource already exists: " + name);

    std::optional<TexturePathKey> cacheKey;
    std::shared_ptr<Texture> texture;
    bool createdTexture = false;

    if (data.IsFile())
    {
        cacheKey = TexturePathKey{
            NormalizeResourcePath(data.filePath),
            data.colorSpace
        };
        data.filePath = cacheKey->path;

        const auto cached = this->texturePathCache.find(*cacheKey);
        if (cached != this->texturePathCache.end())
        {
            texture = cached->second.lock();
            if (texture == nullptr)
                this->texturePathCache.erase(cached);
        }
    }

    if (texture == nullptr)
    {
        texture = std::make_shared<Texture>(std::move(data));
        createdTexture = true;
    }

    this->textures.emplace(name, texture);

    if (cacheKey.has_value() && createdTexture)
    {
        try
        {
            this->texturePathCache.emplace(*cacheKey, texture);
        }
        catch (...)
        {
            this->textures.erase(name);
            throw;
        }
    }

    return texture;
}

Material& ResourceManager::CreateMaterial(
    const std::string& name,
    const MaterialData& data,
    MaterialTextureBindings bindings
)
{
    if (this->materials.contains(name))
        throw std::invalid_argument("Material resource already exists: " + name);

    auto material = std::make_unique<Material>(
        data,
        std::move(bindings),
        this->programCache,
        this->graphicsDevice
    );
    Material& result = *material;
    this->materials.emplace(name, std::move(material));
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

ModelAsset& ResourceManager::CreateModel(
    const std::string& name,
    ImportedModelData imported
)
{
    if (name.empty())
        throw std::invalid_argument("Model resource name must not be empty");
    if (this->models.contains(name))
        throw std::invalid_argument("Model resource already exists: " + name);

    std::vector<std::shared_ptr<Texture>> textures;
    textures.reserve(imported.textures.size());
    for (std::size_t index = 0U; index < imported.textures.size(); ++index)
    {
        textures.push_back(this->CreateTextureShared(
            name + "::texture::" + std::to_string(index),
            std::move(imported.textures[index].source)
        ));
    }

    std::vector<std::unique_ptr<Material>> materials;
    materials.reserve(imported.materials.size());
    for (std::size_t index = 0U; index < imported.materials.size(); ++index)
    {
        const ImportedMaterialData& source = imported.materials[index];
        MaterialData data;
        data.textureSources.clear();
        data.baseColorFactor = source.baseColorFactor;
        data.specularColor = source.specularColor;
        data.shininess = source.shininess;
        data.normalScale = source.normalScale;
        data.shadingModel = source.shadingModel;
        data.metallicFactor = source.metallicFactor;
        data.roughnessFactor = source.roughnessFactor;
        data.emissiveFactor = source.emissiveFactor;
        data.occlusionStrength = source.occlusionStrength;
        data.ambientColor = source.ambientColor;
        data.sphereMapMode = source.sphereMapMode;
        data.edgeColor = source.edgeColor;
        data.edgeSize = source.edgeSize;
        data.edgeEnabled = source.edgeEnabled;
        data.alphaMode = source.alphaMode;
        data.alphaCutoff = source.alphaCutoff;
        data.doubleSided = source.doubleSided;
        data.groundShadow = source.groundShadow;
        data.castSelfShadow = source.castSelfShadow;
        data.receiveSelfShadow = source.receiveSelfShadow;
        if (source.shadingModel == MaterialShadingModel::MmdToon)
        {
            const std::filesystem::path shaderDirectory =
                std::filesystem::current_path() / "assets" / "shaders";
            data.shaderFilePath.VertexPath =
                (shaderDirectory / "mmd.vert").string();
            data.shaderFilePath.FragmentPath =
                (shaderDirectory / "mmd.frag").string();
            data.shaderInterface.imageBasedLightingEnabled = false;
            data.shaderInterface.shadowingSupported = true;
        }

        MaterialTextureBindings bindings;
        const auto bindTexture = [&](std::size_t textureIndex,
                                     const std::string& shaderName)
        {
            if (textureIndex < textures.size())
                bindings.emplace(shaderName, textures[textureIndex]);
        };
        if (source.baseColorTexture.has_value())
            bindTexture(
                *source.baseColorTexture,
                data.shaderInterface.baseColorTexture
            );
        if (source.sphereTexture.has_value())
            bindTexture(
                *source.sphereTexture,
                data.shaderInterface.sphereTexture
            );
        if (source.toonTexture.has_value())
            bindTexture(
                *source.toonTexture,
                data.shaderInterface.toonTexture
            );
        materials.push_back(std::make_unique<Material>(
            data,
            std::move(bindings),
            this->programCache,
            this->graphicsDevice
        ));
    }

    std::vector<std::unique_ptr<Mesh>> meshes;
    meshes.reserve(imported.meshes.size());
    for (std::size_t index = 0U; index < imported.meshes.size(); ++index)
    {
        meshes.push_back(std::make_unique<Mesh>(
            std::move(imported.meshes[index].data),
            imported.meshes[index].requiredBoneCount,
            std::move(imported.meshes[index].morphTargets),
            std::move(imported.meshes[index].sourceVertexIndices),
            this->graphicsDevice
        ));
    }

    auto model = std::make_unique<ModelAsset>(name);
    if (imported.skeleton.has_value())
        model->SetSkeleton(std::move(*imported.skeleton));
    if (imported.mmdPhysics.has_value())
        model->SetMmdPhysics(std::move(*imported.mmdPhysics));
    if (!imported.morphs.empty())
        model->SetMorphs(std::move(imported.morphs));
    for (AnimationClip& clip : imported.animations)
        model->AddAnimationClip(std::move(clip));
    for (const ImportedPartData& part : imported.parts)
    {
        const std::size_t materialIndex =
            imported.meshes[part.meshIndex].materialIndex;
        model->AddPart(
            *meshes[part.meshIndex],
            *materials[materialIndex],
            part.localTransform,
            imported.meshes[part.meshIndex].morphMaterialIndex
        );
    }

    this->materials.reserve(this->materials.size() + imported.materials.size());
    this->meshes.reserve(this->meshes.size() + imported.meshes.size());
    this->models.reserve(this->models.size() + 1);

    std::size_t materialCommitCount = 0U;
    std::size_t meshCommitCount = 0U;
    try
    {
        for (std::size_t index = 0U; index < materials.size(); ++index)
        {
            this->materials.emplace(
                name + "::material::" + std::to_string(index),
                std::move(materials[index])
            );
            ++materialCommitCount;
        }
        for (std::size_t index = 0U; index < meshes.size(); ++index)
        {
            this->meshes.emplace(
                name + "::mesh::" + std::to_string(index),
                std::move(meshes[index])
            );
            ++meshCommitCount;
        }
        ModelAsset* result = model.get();
        this->models.emplace(name, std::move(model));
        return *result;
    }
    catch (...)
    {
        this->models.erase(name);
        for (std::size_t index = 0U; index < materialCommitCount; ++index)
            this->materials.erase(name + "::material::" + std::to_string(index));
        for (std::size_t index = 0U; index < meshCommitCount; ++index)
            this->meshes.erase(name + "::mesh::" + std::to_string(index));
        for (std::size_t index = 0U; index < textures.size(); ++index)
            this->textures.erase(name + "::texture::" + std::to_string(index));
        throw;
    }
}

ModelAsset& ResourceManager::LoadModel(
    const std::string& name,
    const std::filesystem::path& filePath
)
{
    if (name.empty())
        throw std::invalid_argument("Model resource name must not be empty");

    const std::filesystem::path normalizedModelPath =
        NormalizeResourcePath(filePath);
    const auto cachedModel = this->modelPathCache.find(normalizedModelPath);
    if (cachedModel != this->modelPathCache.end())
    {
        if (cachedModel->second->Name() != name)
        {
            throw std::invalid_argument(
                "Model file is already loaded as resource: " +
                cachedModel->second->Name()
            );
        }
        return *cachedModel->second;
    }

    if (this->models.contains(name))
        throw std::invalid_argument("Model resource already exists: " + name);

    const std::string extension = LowerExtension(normalizedModelPath);
    const bool isPmx = extension == ".pmx";
    ImportedModelData imported = isPmx
        ? SabaMmdImporter().Import(normalizedModelPath)
        : ModelImporter().Import(normalizedModelPath);
    // R1.5 Phase 0D: backend identity is decided by the imported result, not
    // by file extension. PMX always means MMD; any other asset with a
    // Skeleton, Morphs or AnimationClip needs the Wisteria generic runtime;
    // everything else is a true static model with no runtime.
    ModelBackendKind backendKind = ModelBackendKind::Static;
    if (isPmx)
    {
        backendKind = ModelBackendKind::SabaMmd;
    }
    else if (imported.skeleton.has_value() ||
        !imported.morphs.empty() ||
        !imported.animations.empty())
    {
        backendKind = ModelBackendKind::WisteriaGeneric;
    }

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

        const auto validateTextureIndex =
            [&imported](
                const std::optional<std::size_t>& textureIndex,
                const char* semantic
            )
        {
            if (textureIndex.has_value() &&
                *textureIndex >= imported.textures.size())
            {
                throw std::runtime_error(
                    std::string("Imported material references an invalid ") +
                    semantic + " texture index"
                );
            }
        };
        const ImportedMaterialData& material = imported.materials[index];
        validateTextureIndex(material.baseColorTexture, "base-color");
        validateTextureIndex(material.normalTexture, "normal");
        validateTextureIndex(
            material.metallicRoughnessTexture,
            "metallic-roughness"
        );
        validateTextureIndex(material.emissiveTexture, "emissive");
        validateTextureIndex(material.occlusionTexture, "occlusion");
        validateTextureIndex(material.sphereTexture, "MMD sphere-map");
        validateTextureIndex(material.toonTexture, "MMD Toon");
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
    std::unordered_map<
        TexturePathKey,
        std::shared_ptr<Texture>,
        TexturePathKeyHash
    > newExternalTextures;
    importedTextures.reserve(imported.textures.size());
    for (std::size_t index = 0; index < imported.textures.size(); ++index)
    {
        TextureData source = std::move(imported.textures[index].source);
        std::shared_ptr<Texture> texture;

        if (source.IsFile())
        {
            const std::filesystem::path normalizedTexturePath =
                NormalizeResourcePath(source.filePath);
            source.filePath = normalizedTexturePath;
            const TexturePathKey cacheKey{
                normalizedTexturePath,
                source.colorSpace
            };

            const auto cached =
                this->texturePathCache.find(cacheKey);
            if (cached != this->texturePathCache.end())
            {
                texture = cached->second.lock();
                if (texture == nullptr)
                    this->texturePathCache.erase(cached);
            }

            if (texture == nullptr)
            {
                const auto pending =
                    newExternalTextures.find(cacheKey);
                if (pending != newExternalTextures.end())
                    texture = pending->second;
            }

            if (texture == nullptr)
            {
                texture = std::make_shared<Texture>(std::move(source));
                newExternalTextures.emplace(cacheKey, texture);
            }
        }
        else
        {
            texture = std::make_shared<Texture>(std::move(source));
        }

        importedTextures.push_back(std::move(texture));
    }

    // R1.9 Final Fix: engine-owned assembly shared with the stable C ABI.
    ModelAssetBundle bundle = BuildModelAssetBundle(
        std::move(imported),
        std::move(importedTextures),
        backendKind,
        normalizedModelPath,
        name,
        this->graphicsDevice,
        this->programCache
    );

    // Reserve first and then commit. If any insertion still fails, erase only
    // resources inserted by this call so the manager never keeps half a model.
    this->textures.reserve(this->textures.size() + bundle.textures.size());
    this->materials.reserve(this->materials.size() + bundle.materials.size());
    this->meshes.reserve(this->meshes.size() + bundle.meshes.size());
    this->models.reserve(this->models.size() + 1);
    this->texturePathCache.reserve(
        this->texturePathCache.size() + newExternalTextures.size()
    );
    this->modelPathCache.reserve(this->modelPathCache.size() + 1);

    std::size_t textureCommitCount = 0;
    std::size_t materialCommitCount = 0;
    std::size_t meshCommitCount = 0;
    bool modelPathCommitted = false;
    try
    {
        for (std::size_t index = 0; index < bundle.textures.size(); ++index)
        {
            this->textures.emplace(
                textureNames[index],
                bundle.textures[index]
            );
            ++textureCommitCount;
        }
        for (std::size_t index = 0; index < bundle.materials.size(); ++index)
        {
            this->materials.emplace(
                materialNames[index],
                std::move(bundle.materials[index])
            );
            ++materialCommitCount;
        }
        for (std::size_t index = 0; index < bundle.meshes.size(); ++index)
        {
            this->meshes.emplace(
                meshNames[index],
                std::move(bundle.meshes[index])
            );
            ++meshCommitCount;
        }

        ModelAsset* result = bundle.asset.get();
        this->models.emplace(name, std::move(bundle.asset));

        const bool modelPathInserted =
            this->modelPathCache.emplace(normalizedModelPath, result).second;
        if (!modelPathInserted)
            throw std::logic_error("Model path cache changed during import");
        modelPathCommitted = true;

        for (const auto& [key, texture] : newExternalTextures)
        {
            const bool texturePathInserted =
                this->texturePathCache.emplace(key, texture).second;
            if (!texturePathInserted)
                throw std::logic_error("Texture path cache changed during import");
        }

        return *result;
    }
    catch (...)
    {
        if (modelPathCommitted)
            this->modelPathCache.erase(normalizedModelPath);
        for (const auto& [key, texture] : newExternalTextures)
        {
            const auto cached = this->texturePathCache.find(key);
            if (
                cached != this->texturePathCache.end() &&
                cached->second.lock() == texture
            )
            {
                this->texturePathCache.erase(cached);
            }
        }
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

AnimationClip& ResourceManager::LoadVmdAnimation(
    ModelAsset& model,
    const std::filesystem::path& filePath,
    const VmdImportOptions& options
)
{
    if (!model.HasSkeleton())
    {
        throw std::invalid_argument(
            "Cannot load VMD animation into a model without a Skeleton"
        );
    }
    ImportedVmdAnimationData imported = VmdImporter().Import(
        filePath,
        model.GetSkeleton(),
        options,
        model.TryGetMorphSet()
    );
    return model.AddAnimationClip(std::move(imported.clip));
}

EnvironmentMap& ResourceManager::CreateEnvironment(
    const std::string& name,
    const EnvironmentMapData& data
)
{
    if (name.empty())
        throw std::invalid_argument("Environment resource name must not be empty");
    if (this->environments.contains(name))
    {
        throw std::invalid_argument(
            "Environment resource already exists: " + name
        );
    }

    auto environment = std::make_unique<EnvironmentMap>(data);
    EnvironmentMap& result = *environment;
    this->environments.emplace(name, std::move(environment));
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

Texture* ResourceManager::FindTextureByPath(
    const std::filesystem::path& filePath,
    TextureColorSpace colorSpace
)
{
    const TexturePathKey key{NormalizeResourcePath(filePath), colorSpace};
    const auto iterator = this->texturePathCache.find(key);
    if (iterator == this->texturePathCache.end())
        return nullptr;

    const std::shared_ptr<Texture> texture = iterator->second.lock();
    if (texture == nullptr)
    {
        this->texturePathCache.erase(iterator);
        return nullptr;
    }
    return texture.get();
}

const Texture* ResourceManager::FindTextureByPath(
    const std::filesystem::path& filePath,
    TextureColorSpace colorSpace
) const
{
    const TexturePathKey key{NormalizeResourcePath(filePath), colorSpace};
    const auto iterator = this->texturePathCache.find(key);
    if (iterator == this->texturePathCache.end())
        return nullptr;

    const std::shared_ptr<Texture> texture = iterator->second.lock();
    return texture != nullptr ? texture.get() : nullptr;
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

ModelAsset* ResourceManager::FindModelByPath(
    const std::filesystem::path& filePath
)
{
    const std::filesystem::path normalizedPath =
        NormalizeResourcePath(filePath);
    const auto iterator = this->modelPathCache.find(normalizedPath);
    return iterator != this->modelPathCache.end() ? iterator->second : nullptr;
}

const ModelAsset* ResourceManager::FindModelByPath(
    const std::filesystem::path& filePath
) const
{
    const std::filesystem::path normalizedPath =
        NormalizeResourcePath(filePath);
    const auto iterator = this->modelPathCache.find(normalizedPath);
    return iterator != this->modelPathCache.end() ? iterator->second : nullptr;
}

EnvironmentMap* ResourceManager::FindEnvironment(
    const std::string& name
) noexcept
{
    const auto iterator = this->environments.find(name);
    return iterator != this->environments.end()
        ? iterator->second.get()
        : nullptr;
}

const EnvironmentMap* ResourceManager::FindEnvironment(
    const std::string& name
) const noexcept
{
    const auto iterator = this->environments.find(name);
    return iterator != this->environments.end()
        ? iterator->second.get()
        : nullptr;
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

EnvironmentMap& ResourceManager::GetEnvironment(const std::string& name)
{
    EnvironmentMap* environment = this->FindEnvironment(name);
    if (environment == nullptr)
    {
        throw std::out_of_range(
            "Environment resource was not found: " + name
        );
    }
    return *environment;
}

const EnvironmentMap& ResourceManager::GetEnvironment(
    const std::string& name
) const
{
    const EnvironmentMap* environment = this->FindEnvironment(name);
    if (environment == nullptr)
    {
        throw std::out_of_range(
            "Environment resource was not found: " + name
        );
    }
    return *environment;
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

std::size_t ResourceManager::EnvironmentCount() const noexcept
{
    return this->environments.size();
}

void ResourceManager::Clear() noexcept
{
    // Programs must be released while a context of the device's share group
    // is current; Application guarantees this before calling Clear.
    this->programCache->Clear();
    this->modelPathCache.clear();
    this->texturePathCache.clear();
    this->models.clear();
    this->environments.clear();
    this->materials.clear();
    this->textures.clear();
    this->meshes.clear();
}
}  // namespace wisteria
