#include "wisteria/common/pch.hpp"
#include "wisteria/rendering/material.hpp"
#include <cmath>
#include "backend/opengl/material_gpu_resource.hpp"

namespace wisteria
{
namespace
{
MaterialTextureBindings BuildTextureBindings(
    const MaterialData& data,
    RenderResourceCache* cache
)
{
    MaterialTextureBindings bindings;
    for (const auto& [uniformName, source] : data.textureSources)
    {
        bindings.emplace(
            uniformName,
            std::make_shared<Texture>(source, cache)
        );
    }
    return bindings;
}

}

Material::Material(const MaterialData& _data, RenderResourceCache* cache)
    : Material(
          _data,
          BuildTextureBindings(_data, cache),
          std::make_shared<ProgramCache>(),
          cache
      )
{
}

Material::Material(
    const MaterialData& data,
    std::shared_ptr<ProgramCache> programCache,
    RenderResourceCache* cache
)
    : Material(
          data,
          BuildTextureBindings(data, cache),
          std::move(programCache),
          cache
      )
{
}

Material::Material(
    const MaterialData& data,
    MaterialTextureBindings textureBindings,
    RenderResourceCache* cache
)
    : Material(
          data,
          std::move(textureBindings),
          std::make_shared<ProgramCache>(),
          cache
      )
{
}

Material::Material(
    const MaterialData& data,
    MaterialTextureBindings textureBindings,
    std::shared_ptr<ProgramCache> programCache,
    RenderResourceCache* cache
)
    : data(data),
      programCache(std::move(programCache)),
      textures(std::move(textureBindings)),
      cache(cache)
{
    if (!std::isfinite(this->data.alphaCutoff))
        throw std::invalid_argument("Material alpha cutoff must be finite");
    if (!std::isfinite(this->data.normalScale))
        throw std::invalid_argument("Material normal scale must be finite");
    if (!std::isfinite(this->data.metallicFactor) ||
        !std::isfinite(this->data.roughnessFactor) ||
        !std::isfinite(this->data.occlusionStrength))
    {
        throw std::invalid_argument("PBR scalar factors must be finite");
    }
    if (!std::isfinite(this->data.emissiveFactor.x) ||
        !std::isfinite(this->data.emissiveFactor.y) ||
        !std::isfinite(this->data.emissiveFactor.z))
    {
        throw std::invalid_argument("Material emissive factor must be finite");
    }
    if (!std::isfinite(this->data.ambientColor.x) ||
        !std::isfinite(this->data.ambientColor.y) ||
        !std::isfinite(this->data.ambientColor.z) ||
        !std::isfinite(this->data.edgeColor.x) ||
        !std::isfinite(this->data.edgeColor.y) ||
        !std::isfinite(this->data.edgeColor.z) ||
        !std::isfinite(this->data.edgeColor.w) ||
        !std::isfinite(this->data.edgeSize))
    {
        throw std::invalid_argument("MMD material factors must be finite");
    }
    this->data.alphaCutoff = glm::clamp(this->data.alphaCutoff, 0.0f, 1.0f);
    this->data.baseColorFactor = glm::clamp(
        this->data.baseColorFactor,
        glm::vec4(0.0f),
        glm::vec4(1.0f)
    );
    this->data.normalScale = glm::max(this->data.normalScale, 0.0f);
    this->data.metallicFactor = glm::clamp(
        this->data.metallicFactor,
        0.0f,
        1.0f
    );
    this->data.roughnessFactor = glm::clamp(
        this->data.roughnessFactor,
        0.0f,
        1.0f
    );
    this->data.emissiveFactor = glm::max(
        this->data.emissiveFactor,
        glm::vec3(0.0f)
    );
    this->data.occlusionStrength = glm::clamp(
        this->data.occlusionStrength,
        0.0f,
        1.0f
    );
    this->data.ambientColor = glm::max(
        this->data.ambientColor,
        glm::vec3(0.0f)
    );
    this->data.edgeColor = glm::clamp(
        this->data.edgeColor,
        glm::vec4(0.0f),
        glm::vec4(1.0f)
    );
    this->data.edgeSize = glm::max(this->data.edgeSize, 0.0f);

    this->gpu = std::make_unique<MaterialGpuResource>(
        this->data,
        this->textures,
        this->programCache,
        this->cache
    );
}

Material::~Material() = default;

void Material::SetRenderCache(RenderResourceCache* nextCache)
{
    // Materials are assets: allow re-resolving for another device even
    // after attach (programs and texture realizations stay alive through
    // their own caches/shared ownership).
    this->cache = nextCache;
    if (this->cache == nullptr)
        return;
    for (const auto& [uniformName, texture] : this->textures)
        texture->SetRenderCache(this->cache);
    this->gpu = std::make_unique<MaterialGpuResource>(
        this->data,
        this->textures,
        this->programCache,
        this->cache
    );
}

void Material::Attach()
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Material without a RenderResourceCache cannot attach"
        );
    }
    this->gpu->Attach(this->data);
}

void Material::Bind()
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Material without a RenderResourceCache cannot bind"
        );
    }
    this->gpu->Bind();
}

void Material::Unbind()
{
    if (this->gpu == nullptr)
        return;
    this->gpu->Unbind();
}

Program& Material::GetProgram()
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Material without a RenderResourceCache cannot provide a program"
        );
    }
    return this->gpu->GetProgram();
}

const Program& Material::GetProgram() const
{
    if (this->gpu == nullptr)
    {
        throw std::logic_error(
            "Material without a RenderResourceCache cannot provide a program"
        );
    }
    return this->gpu->GetProgram();
}

const glm::vec3& Material::SpecularColor() const noexcept
{
    return this->data.specularColor;
}

float Material::Shininess() const noexcept
{
    return this->data.shininess;
}

float Material::NormalScale() const noexcept
{
    return this->data.normalScale;
}

float Material::MetallicFactor() const noexcept
{
    return this->data.metallicFactor;
}

float Material::RoughnessFactor() const noexcept
{
    return this->data.roughnessFactor;
}

const glm::vec3& Material::EmissiveFactor() const noexcept
{
    return this->data.emissiveFactor;
}

float Material::OcclusionStrength() const noexcept
{
    return this->data.occlusionStrength;
}

MaterialShadingModel Material::ShadingModel() const noexcept
{
    return this->data.shadingModel;
}

const glm::vec3& Material::AmbientColor() const noexcept
{
    return this->data.ambientColor;
}

MmdSphereMapMode Material::SphereMapMode() const noexcept
{
    return this->data.sphereMapMode;
}

const glm::vec4& Material::EdgeColor() const noexcept
{
    return this->data.edgeColor;
}

float Material::EdgeSize() const noexcept
{
    return this->data.edgeSize;
}

bool Material::IsEdgeEnabled() const noexcept
{
    return this->data.edgeEnabled;
}

const glm::vec4& Material::BaseColorFactor() const noexcept
{
    return this->data.baseColorFactor;
}

MaterialAlphaMode Material::AlphaMode() const noexcept
{
    return this->data.alphaMode;
}

float Material::AlphaCutoff() const noexcept
{
    return this->data.alphaCutoff;
}

bool Material::IsDoubleSided() const noexcept
{
    return this->data.doubleSided;
}

bool Material::IsGroundShadow() const noexcept
{
    return this->data.groundShadow;
}

bool Material::CastsSelfShadow() const noexcept
{
    return this->data.castSelfShadow;
}

bool Material::ReceivesSelfShadow() const noexcept
{
    return this->data.receiveSelfShadow;
}

bool Material::ReceivesGroundShadow() const noexcept
{
    return this->data.receivesGroundShadow;
}

bool Material::IsGroundPlane() const noexcept
{
    return this->data.groundPlane;
}

void Material::SetReceivesGroundShadow(bool enabled) noexcept
{
    this->data.receivesGroundShadow = enabled;
}

bool Material::HasTexture(const std::string& uniformName) const noexcept
{
    return this->textures.contains(uniformName);
}

const ShaderInterface& Material::Interface() const noexcept
{
    return this->data.shaderInterface;
}

void Material::SetSpecularColor(const glm::vec3& color) noexcept
{
    this->data.specularColor = glm::max(color, glm::vec3(0.0f));
}

void Material::SetShininess(float shininess) noexcept
{
    this->data.shininess = glm::max(shininess, 1.0f);
}

void Material::SetNormalScale(float normalScale) noexcept
{
    if (std::isfinite(normalScale))
        this->data.normalScale = glm::max(normalScale, 0.0f);
}

void Material::SetMetallicFactor(float metallicFactor) noexcept
{
    if (std::isfinite(metallicFactor))
        this->data.metallicFactor = glm::clamp(metallicFactor, 0.0f, 1.0f);
}

void Material::SetRoughnessFactor(float roughnessFactor) noexcept
{
    if (std::isfinite(roughnessFactor))
        this->data.roughnessFactor = glm::clamp(roughnessFactor, 0.0f, 1.0f);
}

void Material::SetEmissiveFactor(const glm::vec3& emissiveFactor) noexcept
{
    if (std::isfinite(emissiveFactor.x) &&
        std::isfinite(emissiveFactor.y) &&
        std::isfinite(emissiveFactor.z))
    {
        this->data.emissiveFactor = glm::max(
            emissiveFactor,
            glm::vec3(0.0f)
        );
    }
}

void Material::SetOcclusionStrength(float occlusionStrength) noexcept
{
    if (std::isfinite(occlusionStrength))
    {
        this->data.occlusionStrength = glm::clamp(
            occlusionStrength,
            0.0f,
            1.0f
        );
    }
}
}  // namespace wisteria
