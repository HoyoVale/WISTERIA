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
    this->data.alphaCutoff = glm::clamp(this->data.alphaCutoff, 0.0f, 1.0f);
    this->data.baseColorFactor = glm::clamp(
        this->data.baseColorFactor,
        glm::vec4(0.0f),
        glm::vec4(1.0f)
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
