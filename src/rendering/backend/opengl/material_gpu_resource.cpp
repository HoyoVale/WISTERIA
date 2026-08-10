#include "wisteria/common/pch.hpp"

#include "material_gpu_resource.hpp"

#include "wisteria/rendering/program_cache.hpp"
#include "render_resource_cache.hpp"
#include "wisteria/rendering/shader.hpp"

#include <stdexcept>

namespace wisteria
{
MaterialGpuResource::MaterialGpuResource(
    const MaterialData&,
    MaterialTextureBindings nextTextures,
    std::shared_ptr<ProgramCache> nextProgramCache,
    RenderResourceCache* cache
)
    : device(cache != nullptr ? cache->Device() : nullptr),
      programCache(std::move(nextProgramCache)),
      textures(std::move(nextTextures))
{
    if (this->programCache == nullptr)
        throw std::invalid_argument("Material program cache must not be null");
    for (const auto& [uniformName, texture] : this->textures)
    {
        if (uniformName.empty())
            throw std::invalid_argument("Texture uniform name must not be empty");
        if (texture == nullptr)
            throw std::invalid_argument("Material texture binding must not be null");
    }
}

void MaterialGpuResource::Attach(const MaterialData& data)
{
    if (this->program != nullptr)
        return;

    this->program = this->programCache->Acquire(
        data.shaderFilePath.VertexPath,
        data.shaderFilePath.FragmentPath
    );
    for (const auto& [uniformName, texture] : this->textures)
        texture->Attach();
}

void MaterialGpuResource::Bind()
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

void MaterialGpuResource::Unbind()
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

bool MaterialGpuResource::HasTexture(
    const std::string& uniformName
) const noexcept
{
    return this->textures.contains(uniformName);
}

bool MaterialGpuResource::IsAttached() const noexcept
{
    return this->program != nullptr;
}

Program& MaterialGpuResource::GetProgram()
{
    if (this->program == nullptr)
        throw std::logic_error("Material must be attached before getting program");
    return *this->program;
}

const Program& MaterialGpuResource::GetProgram() const
{
    if (this->program == nullptr)
        throw std::logic_error("Material must be attached before getting program");
    return *this->program;
}
}  // namespace wisteria
