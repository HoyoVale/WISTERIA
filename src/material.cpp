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

    this->shader = std::make_unique<Shader>(
        this->data.shaderFilePath.VertexPath,
        this->data.shaderFilePath.FragmentPath
    );
    this->program = std::make_unique<Program>(
        this->shader->GetShaderList()
    );

    unsigned int unit = 0;
    for (const auto& [uniformName, filePath] : this->data.textureFilePath)
    {
        auto texture = std::make_unique<Texture>();
        texture->Upload(filePath, unit);
        this->textures.emplace(uniformName, std::move(texture));
        ++unit;
    }
}

void Material::Bind()
{
    if (this->program == nullptr)
        throw std::logic_error("Material must be attached before binding");

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
