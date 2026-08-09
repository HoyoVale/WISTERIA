#include "wisteria/common/pch.hpp"

#include "wisteria/assets/model_asset_bundle.hpp"
#include "wisteria/rendering/program_cache.hpp"

#include <stdexcept>
#include <utility>

namespace wisteria
{
ModelAssetBundle BuildModelAssetBundle(
    ImportedModelData imported,
    std::vector<std::shared_ptr<Texture>> textures,
    ModelBackendKind backendKind,
    const std::filesystem::path& sourcePath,
    const std::string& name,
    GraphicsDevice* device
)
{
    if (textures.size() != imported.textures.size())
    {
        throw std::invalid_argument(
            "texture vector must be index-aligned with imported textures"
        );
    }
    for (std::size_t index = 0U; index < imported.materials.size(); ++index)
    {
        const ImportedMaterialData& material =
            imported.materials[index];
        const auto validTexture = [&textures](
            const std::optional<std::size_t>& textureIndex,
            const char* semantic
        )
        {
            if (textureIndex.has_value() &&
                *textureIndex >= textures.size())
            {
                throw std::runtime_error(
                    std::string("Imported material references an invalid ") +
                    semantic + " texture index"
                );
            }
        };
        validTexture(material.baseColorTexture, "base-color");
        validTexture(material.normalTexture, "normal");
        validTexture(
            material.metallicRoughnessTexture,
            "metallic-roughness"
        );
        validTexture(material.emissiveTexture, "emissive");
        validTexture(material.occlusionTexture, "occlusion");
        validTexture(material.sphereTexture, "MMD sphere-map");
        validTexture(material.toonTexture, "MMD Toon");
    }
    for (const ImportedPartData& part : imported.parts)
    {
        if (part.meshIndex >= imported.meshes.size())
            throw std::runtime_error("Imported part references an invalid mesh index");
        if (imported.meshes[part.meshIndex].materialIndex >=
            imported.materials.size())
        {
            throw std::runtime_error(
                "Imported mesh references an invalid material index"
            );
        }
    }

    ModelAssetBundle bundle;

    // Materials: full texture bindings, not a single default material.
    const auto programCache = std::make_shared<ProgramCache>();
    bundle.materials.reserve(imported.materials.size());
    for (const ImportedMaterialData& source : imported.materials)
    {
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
        const auto bind = [&bindings, &textures, &data](
            const std::optional<std::size_t>& textureIndex,
            const std::string& uniformName
        )
        {
            if (textureIndex.has_value())
            {
                bindings.emplace(
                    uniformName,
                    textures[*textureIndex]
                );
            }
        };
        bind(
            source.baseColorTexture,
            data.shaderInterface.baseColorTexture
        );
        bind(source.normalTexture, data.shaderInterface.normalTexture);
        bind(
            source.metallicRoughnessTexture,
            data.shaderInterface.metallicRoughnessTexture
        );
        bind(
            source.emissiveTexture,
            data.shaderInterface.emissiveTexture
        );
        bind(
            source.occlusionTexture,
            data.shaderInterface.occlusionTexture
        );
        bind(source.sphereTexture, data.shaderInterface.sphereTexture);
        bind(source.toonTexture, data.shaderInterface.toonTexture);

        bundle.materials.push_back(
            std::make_unique<Material>(
                data,
                std::move(bindings),
                programCache,
                device
            )
        );
    }

    // Meshes: record per-mesh material/morph-material indices BEFORE the
    // mesh data is moved.
    std::vector<std::size_t> meshMaterialIndices;
    std::vector<std::optional<std::uint32_t>> meshMorphMaterialIndices;
    meshMaterialIndices.reserve(imported.meshes.size());
    meshMorphMaterialIndices.reserve(imported.meshes.size());
    bundle.meshes.reserve(imported.meshes.size());
    for (ImportedMeshData& meshData : imported.meshes)
    {
        meshMaterialIndices.push_back(meshData.materialIndex);
        meshMorphMaterialIndices.push_back(
            meshData.morphMaterialIndex
        );
        bundle.meshes.push_back(
            std::make_unique<Mesh>(
                std::move(meshData.data),
                meshData.requiredBoneCount,
                std::move(meshData.morphTargets),
                std::move(meshData.sourceVertexIndices),
                device
            )
        );
    }

    // Asset: skeleton/morphs/clips/physics + parts referencing the bundle's
    // meshes and materials.
    bundle.asset = std::make_unique<ModelAsset>(name);
    bundle.asset->SetSourceDescriptor(ModelSourceDescriptor{
        sourcePath,
        backendKind
    });
    bundle.asset->SetBackendKind(backendKind);
    if (imported.skeleton.has_value())
        bundle.asset->SetSkeleton(std::move(*imported.skeleton));
    if (imported.mmdPhysics.has_value())
        bundle.asset->SetMmdPhysics(std::move(*imported.mmdPhysics));
    if (!imported.morphs.empty())
        bundle.asset->SetMorphs(std::move(imported.morphs));
    for (AnimationClip& clip : imported.animations)
        bundle.asset->AddAnimationClip(std::move(clip));
    for (const ImportedPartData& part : imported.parts)
    {
        bundle.asset->AddPart(
            *bundle.meshes[part.meshIndex],
            *bundle.materials[meshMaterialIndices[part.meshIndex]],
            part.localTransform,
            meshMorphMaterialIndices[part.meshIndex]
        );
    }

    bundle.textures = std::move(textures);
    return bundle;
}
}  // namespace wisteria
