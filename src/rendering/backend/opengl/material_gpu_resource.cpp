#include "wisteria/common/pch.hpp"

#include "material_gpu_resource.hpp"

#include "wisteria/core/asset_paths.hpp"
#include "wisteria/rendering/program_cache.hpp"
#include "render_resource_cache.hpp"
#include "wisteria/rendering/shader.hpp"

#include <stdexcept>

namespace wisteria
{
namespace
{
// R2.0 Phase 0C Step 7: built-in semantic pipeline selection. Custom routes
// to the legacy GLSL path; built-in variants resolve to engine shaders
// through the asset system (not cwd-relative paths).
Path ResolveShaderPaths(const MaterialData& data)
{
    if (data.pipelineVariant.variant == PipelineVariant::Custom)
        return data.shaderFilePath;
    switch (data.pipelineVariant.variant)
    {
    case PipelineVariant::MmdToon:
        return Path{
            wisteria::assets::Shader("mmd.vert"),
            wisteria::assets::Shader("mmd.frag")
        };
    case PipelineVariant::PbrMetallicRoughness:
    default:
        return Path{
            wisteria::assets::Shader("basicTex.vert"),
            wisteria::assets::Shader("basicTex.frag")
        };
    }
}
}  // namespace

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

    // Creation provenance: programs and textures must be created under the
    // owning device's share group, before any GL work.
    if (this->device != nullptr)
    {
        this->device->RequireShareGroupToken(
            GraphicsDevice::CurrentShareGroup()
        );
        if (GraphicsDevice::CurrentContext() == nullptr)
        {
            throw std::logic_error(
                "Material GPU realization requires a current owning context"
            );
        }
    }

    // Transactional attach: the program is committed only after every
    // texture also attached. A failure leaves the realization retryable
    // (program == nullptr), never pseudo-attached.
    const Path shaderPaths = ResolveShaderPaths(data);
    auto nextProgram = this->programCache->Acquire(
        shaderPaths.VertexPath,
        shaderPaths.FragmentPath
    );
    for (const auto& [uniformName, texture] : this->textures)
        texture->Attach();
    this->program = std::move(nextProgram);
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
