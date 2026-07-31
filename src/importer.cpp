#include "pch.hpp"
#include "importer.hpp"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
constexpr unsigned int ImportFlags =
    aiProcess_Triangulate |
    aiProcess_JoinIdenticalVertices |
    aiProcess_GenSmoothNormals |
    // Texture decoders keep the first image row at the top. Normalize Assimp's
    // UV output to the same top-left convention used by glTF and our uploader.
    aiProcess_FlipUVs |
    aiProcess_ImproveCacheLocality |
    aiProcess_SortByPType |
    aiProcess_FindInvalidData |
    aiProcess_ValidateDataStructure;

std::string ToString(const aiString& value)
{
    return std::string(value.C_Str(), value.length);
}

std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::filesystem::path FromUtf8(std::string_view value)
{
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char byte : value)
        utf8.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(utf8);
}

std::string PathExtensionHint(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    if (!extension.empty() && extension.front() == '.')
        extension.erase(extension.begin());
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

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open model file: " + path.string());

    const std::streampos end = stream.tellg();
    if (end <= 0)
        throw std::runtime_error("Model file is empty: " + path.string());
    if (static_cast<std::uintmax_t>(end) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error("Model file is too large: " + path.string());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    if (!stream)
        throw std::runtime_error("Cannot read model file: " + path.string());
    return bytes;
}

glm::mat4 ToGlm(const aiMatrix4x4& matrix)
{
    glm::mat4 result(1.0f);
    result[0][0] = matrix.a1;
    result[1][0] = matrix.a2;
    result[2][0] = matrix.a3;
    result[3][0] = matrix.a4;
    result[0][1] = matrix.b1;
    result[1][1] = matrix.b2;
    result[2][1] = matrix.b3;
    result[3][1] = matrix.b4;
    result[0][2] = matrix.c1;
    result[1][2] = matrix.c2;
    result[2][2] = matrix.c3;
    result[3][2] = matrix.c4;
    result[0][3] = matrix.d1;
    result[1][3] = matrix.d2;
    result[2][3] = matrix.d3;
    result[3][3] = matrix.d4;
    return result;
}

bool IsFinite(const glm::mat4& matrix)
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrix[column][row]))
                return false;
        }
    }
    return true;
}

std::string ResourceName(const aiString& name, const char* prefix, std::size_t index)
{
    std::string result = ToString(name);
    if (result.empty())
        result = std::string(prefix) + std::to_string(index);
    return result;
}

ImportedMeshData ImportMesh(const aiMesh& mesh, std::size_t index)
{
    if (mesh.mNumVertices == 0)
        throw std::runtime_error("Imported mesh has no vertices");
    if (!mesh.HasNormals())
        throw std::runtime_error("Assimp did not provide mesh normals");

    ImportedMeshData result;
    result.name = ResourceName(mesh.mName, "mesh", index);
    result.materialIndex = mesh.mMaterialIndex;
    result.data.layout = {
        {"position", 3, FLOAT},
        {"color", 3, FLOAT},
        {"texCoord", 2, FLOAT},
        {"normal", 3, FLOAT}
    };
    result.data.vertices.reserve(static_cast<std::size_t>(mesh.mNumVertices) * 11);

    for (unsigned int vertexIndex = 0;
         vertexIndex < mesh.mNumVertices;
         ++vertexIndex)
    {
        const aiVector3D& position = mesh.mVertices[vertexIndex];
        const aiVector3D& normal = mesh.mNormals[vertexIndex];
        const aiColor4D color = mesh.HasVertexColors(0)
            ? mesh.mColors[0][vertexIndex]
            : aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
        const aiVector3D texCoord = mesh.HasTextureCoords(0)
            ? mesh.mTextureCoords[0][vertexIndex]
            : aiVector3D(0.0f, 0.0f, 0.0f);

        const float values[] = {
            position.x, position.y, position.z,
            color.r, color.g, color.b,
            texCoord.x, texCoord.y,
            normal.x, normal.y, normal.z
        };
        result.data.vertices.insert(
            result.data.vertices.end(),
            std::begin(values),
            std::end(values)
        );
    }

    for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
    {
        const aiFace& face = mesh.mFaces[faceIndex];
        if (face.mNumIndices != 3)
            throw std::runtime_error("Imported mesh contains a non-triangle face");
        for (unsigned int corner = 0; corner < face.mNumIndices; ++corner)
            result.data.indices.push_back(face.mIndices[corner]);
    }

    if (result.data.indices.empty())
        throw std::runtime_error("Imported mesh has no triangle indices");
    return result;
}

std::size_t ImportTexture(
    const aiScene& scene,
    const aiString& texturePath,
    const std::filesystem::path& modelDirectory,
    ImportedModelData& result,
    std::unordered_map<std::string, std::size_t>& textureIndices
)
{
    const std::string key = ToString(texturePath);
    const auto existing = textureIndices.find(key);
    if (existing != textureIndices.end())
        return existing->second;

    ImportedTextureData imported;
    imported.name = key.empty()
        ? "texture" + std::to_string(result.textures.size())
        : key;

    if (const aiTexture* embedded = scene.GetEmbeddedTexture(texturePath.C_Str()))
    {
        if (embedded->mHeight == 0)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(embedded->pcData);
            imported.source = TextureData::FromEncoded(
                std::vector<std::uint8_t>(begin, begin + embedded->mWidth)
            );
        }
        else
        {
            const std::size_t pixelCount =
                static_cast<std::size_t>(embedded->mWidth) * embedded->mHeight;
            if (pixelCount > std::numeric_limits<std::size_t>::max() / 4)
                throw std::length_error("Embedded texture dimensions overflow");

            std::vector<std::uint8_t> rgba(pixelCount * 4);
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                rgba[pixel * 4 + 0] = embedded->pcData[pixel].r;
                rgba[pixel * 4 + 1] = embedded->pcData[pixel].g;
                rgba[pixel * 4 + 2] = embedded->pcData[pixel].b;
                rgba[pixel * 4 + 3] = embedded->pcData[pixel].a;
            }
            imported.source = TextureData::FromRgba8(
                static_cast<int>(embedded->mWidth),
                static_cast<int>(embedded->mHeight),
                std::move(rgba)
            );
        }
    }
    else
    {
        std::string normalizedKey = key;
        std::replace(normalizedKey.begin(), normalizedKey.end(), '\\', '/');
        std::filesystem::path externalPath = FromUtf8(normalizedKey);
        if (externalPath.is_relative())
            externalPath = modelDirectory / externalPath;
        externalPath = externalPath.lexically_normal();
        if (!std::filesystem::is_regular_file(externalPath))
            throw std::runtime_error("External model texture was not found: " + externalPath.string());
        imported.source = TextureData::FromFile(externalPath);
    }

    const std::size_t textureIndex = result.textures.size();
    result.textures.push_back(std::move(imported));
    textureIndices.emplace(key, textureIndex);
    return textureIndex;
}

std::optional<aiString> BaseColorTexturePath(const aiMaterial& material)
{
    aiString path;
    if (material.GetTextureCount(aiTextureType_BASE_COLOR) > 0 &&
        material.GetTexture(aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS)
    {
        return path;
    }
    if (material.GetTextureCount(aiTextureType_DIFFUSE) > 0 &&
        material.GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
    {
        return path;
    }
    return std::nullopt;
}

ImportedMaterialData ImportMaterial(
    const aiScene& scene,
    const aiMaterial& material,
    std::size_t index,
    const std::filesystem::path& modelDirectory,
    ImportedModelData& result,
    std::unordered_map<std::string, std::size_t>& textureIndices
)
{
    ImportedMaterialData imported;
    aiString name;
    if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
        imported.name = ResourceName(name, "material", index);
    else
        imported.name = "material" + std::to_string(index);

    aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
    if (material.Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
        material.Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
    imported.baseColorFactor = glm::clamp(
        glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a),
        glm::vec4(0.0f),
        glm::vec4(1.0f)
    );

    float opacity = 1.0f;
    if (material.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS &&
        std::isfinite(opacity))
    {
        // glTF importers may expose baseColorFactor.a through both keys.
        // Only use the legacy opacity key when alpha was not already supplied.
        if (imported.baseColorFactor.a >= 1.0f)
            imported.baseColorFactor.a = glm::clamp(opacity, 0.0f, 1.0f);
    }

    aiColor3D specular(1.0f, 1.0f, 1.0f);
    if (material.Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS)
    {
        imported.specularColor = glm::max(
            glm::vec3(specular.r, specular.g, specular.b),
            glm::vec3(0.0f)
        );
    }
    float shininess = imported.shininess;
    if (material.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS &&
        std::isfinite(shininess))
    {
        imported.shininess = std::max(shininess, 1.0f);
    }

    aiString alphaMode;
    if (material.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
    {
        const std::string mode = ToString(alphaMode);
        if (mode == "MASK")
            imported.alphaMode = MaterialAlphaMode::Mask;
        else if (mode == "BLEND")
            imported.alphaMode = MaterialAlphaMode::Blend;
    }
    else if (imported.baseColorFactor.a < 1.0f)
    {
        imported.alphaMode = MaterialAlphaMode::Blend;
    }
    else if (material.GetTextureCount(aiTextureType_OPACITY) > 0)
    {
        imported.alphaMode = MaterialAlphaMode::Blend;
    }

    float alphaCutoff = imported.alphaCutoff;
    if (material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) == AI_SUCCESS &&
        std::isfinite(alphaCutoff))
    {
        imported.alphaCutoff = glm::clamp(alphaCutoff, 0.0f, 1.0f);
    }

    int doubleSided = 0;
    if (material.Get(AI_MATKEY_TWOSIDED, doubleSided) == AI_SUCCESS)
        imported.doubleSided = doubleSided != 0;

    if (const std::optional<aiString> path = BaseColorTexturePath(material))
    {
        imported.baseColorTexture = ImportTexture(
            scene,
            *path,
            modelDirectory,
            result,
            textureIndices
        );
    }
    return imported;
}

void ImportNodeParts(
    const aiScene& scene,
    const aiNode& node,
    const glm::mat4& parentTransform,
    ImportedModelData& result
)
{
    const glm::mat4 transform = parentTransform * ToGlm(node.mTransformation);
    if (!IsFinite(transform))
        throw std::runtime_error("Imported node transform contains non-finite values");

    for (unsigned int index = 0; index < node.mNumMeshes; ++index)
    {
        const std::size_t meshIndex = node.mMeshes[index];
        if (meshIndex >= result.meshes.size() || meshIndex >= scene.mNumMeshes)
            throw std::runtime_error("Imported node references an invalid mesh index");

        result.parts.push_back(ImportedPartData{
            ResourceName(node.mName, "part", result.parts.size()),
            meshIndex,
            transform
        });
    }

    for (unsigned int child = 0; child < node.mNumChildren; ++child)
    {
        if (node.mChildren[child] == nullptr)
            throw std::runtime_error("Imported node contains a null child");
        ImportNodeParts(scene, *node.mChildren[child], transform, result);
    }
}
}

ImportedModelData ModelImporter::Import(
    const std::filesystem::path& filePath
) const
{
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(filePath).lexically_normal();
    if (!std::filesystem::is_regular_file(absolutePath))
        throw std::invalid_argument("Model file does not exist: " + filePath.string());

    const std::string extensionHint = PathExtensionHint(absolutePath);
    Assimp::Importer importer;
    const aiScene* scene = nullptr;
    std::vector<std::uint8_t> fileBytes;
    if (extensionHint == "glb")
    {
        // A GLB is self-contained. Reading it through std::filesystem keeps
        // Chinese Windows paths independent of the active system code page.
        fileBytes = ReadBinaryFile(absolutePath);
        scene = importer.ReadFileFromMemory(
            fileBytes.data(),
            fileBytes.size(),
            ImportFlags,
            extensionHint.c_str()
        );
    }
    else
    {
        // OBJ and textual glTF can reference MTL, buffers and images beside
        // the model, so Assimp must receive the real UTF-8 file path.
        const std::string utf8Path = ToUtf8(absolutePath);
        scene = importer.ReadFile(utf8Path.c_str(), ImportFlags);
    }
    if (scene == nullptr)
    {
        throw std::runtime_error(
            "Assimp cannot import model " + filePath.string() + ": " +
            importer.GetErrorString()
        );
    }
    if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mRootNode == nullptr)
    {
        throw std::runtime_error("Assimp returned an incomplete model scene");
    }

    ImportedModelData result;
    std::unordered_map<std::string, std::size_t> textureIndices;
    std::unordered_map<unsigned int, std::size_t> materialIndices;
    result.meshes.reserve(scene->mNumMeshes);
    for (unsigned int index = 0; index < scene->mNumMeshes; ++index)
    {
        if (scene->mMeshes[index] == nullptr)
            throw std::runtime_error("Imported scene contains a null mesh");

        ImportedMeshData mesh = ImportMesh(*scene->mMeshes[index], index);
        const unsigned int sourceMaterialIndex =
            static_cast<unsigned int>(mesh.materialIndex);
        if (sourceMaterialIndex >= scene->mNumMaterials ||
            scene->mMaterials[sourceMaterialIndex] == nullptr)
        {
            throw std::runtime_error("Imported mesh references an invalid material index");
        }

        auto materialEntry = materialIndices.find(sourceMaterialIndex);
        if (materialEntry == materialIndices.end())
        {
            const std::size_t importedMaterialIndex = result.materials.size();
            result.materials.push_back(ImportMaterial(
                *scene,
                *scene->mMaterials[sourceMaterialIndex],
                sourceMaterialIndex,
                absolutePath.parent_path(),
                result,
                textureIndices
            ));
            materialEntry = materialIndices.emplace(
                sourceMaterialIndex,
                importedMaterialIndex
            ).first;
        }
        mesh.materialIndex = materialEntry->second;
        result.meshes.push_back(std::move(mesh));
    }

    ImportNodeParts(*scene, *scene->mRootNode, glm::mat4(1.0f), result);
    if (result.meshes.empty() || result.parts.empty())
        throw std::runtime_error("Imported model contains no drawable mesh parts");
    return result;
}
