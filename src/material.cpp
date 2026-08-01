#include "pch.hpp"
#include "material.hpp"
#include <cmath>

namespace
{
MaterialTextureBindings BuildTextureBindings(const MaterialData& data)
{
    MaterialTextureBindings bindings;
    for (const auto& [uniformName, source] : data.textureSources)
    {
        bindings.emplace(
            uniformName,
            std::make_shared<Texture>(source)
        );
    }
    return bindings;
}
}

Material::Material(const MaterialData &_data)
    : Material(_data, BuildTextureBindings(_data))
{
}

Material::Material(
    const MaterialData& data,
    MaterialTextureBindings textureBindings
)
    : textures(std::move(textureBindings)),
      data(data)
{
    for (const auto& [uniformName, texture] : this->textures)
    {
        if (uniformName.empty())
            throw std::invalid_argument("Texture uniform name must not be empty");
        if (texture == nullptr)
            throw std::invalid_argument("Material texture binding must not be null");
    }
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
}

void Material::Attach()
{
    if (this->program != nullptr)
        return;

    auto nextShader = std::make_unique<Shader>(
        this->data.shaderFilePath.VertexPath,
        this->data.shaderFilePath.FragmentPath
    );
    auto nextProgram = std::make_unique<Program>(
        nextShader->GetShaderList()
    );
    for (const auto& [uniformName, texture] : this->textures)
        texture->Attach();

    // Commit only after every resource has been created successfully.
    this->program.swap(nextProgram);
    this->shader.swap(nextShader);
}

void Material::Bind()
{
    if (this->program == nullptr)
        throw std::logic_error("Material must be attached before binding");

    this->program->Use();

    unsigned int unit = 0;
    for (const auto& [uniformName, texture] : this->textures)
    {
        texture->Bind(unit);
        this->program->UniformTex(uniformName, unit);
        ++unit;
    }
}

void Material::Unbind()
{
    unsigned int unit = 0;
    for (const auto& [uniformName, texture] : this->textures)
    {
        texture->Unbind(unit);
        ++unit;
    }

    if (this->program != nullptr)
        this->program->unUse();
}

Program& Material::GetProgram()
{
    if (this->program == nullptr)
        throw std::logic_error("Material must be attached before getting program");

    return *this->program;
}

const Program& Material::GetProgram() const
{
    if (this->program == nullptr)
        throw std::logic_error("Material must be attached before getting program");

    return *this->program;
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
