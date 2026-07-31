#include "pch.hpp"
#include "material.hpp"

Material::Material(const MaterialData &_data)
    :data(_data)
{
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
    std::unordered_map<std::string, std::unique_ptr<Texture>> nextTextures;

    unsigned int unit = 0;
    for (const auto& [uniformName, filePath] : this->data.textureFilePath)
    {
        auto texture = std::make_unique<Texture>();
        texture->Upload(filePath, unit);
        nextTextures.emplace(uniformName, std::move(texture));
        ++unit;
    }

    // Commit only after every resource has been created successfully.
    // Swap the container first; unique_ptr swaps below are noexcept.
    this->textures.swap(nextTextures);
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
