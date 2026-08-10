#pragma once

#include "wisteria/rendering/material.hpp"
#include "render_resource_cache.hpp"

#include <memory>

namespace wisteria
{
class Program;
class ProgramCache;

// R2.0 Phase 0C Step 4: per-device GPU/pipeline realization of a Material
// CPU definition. Owns the program cache reference, compiled program and
// texture bindings; the semantic MaterialData stays in the asset layer.
class MaterialGpuResource
{
public:
    MaterialGpuResource(
        const MaterialData& data,
        MaterialTextureBindings textureBindings,
        std::shared_ptr<ProgramCache> programCache,
        RenderResourceCache* cache
    );

    void Attach(const MaterialData& data);
    void Bind();
    void Unbind();
    bool HasTexture(const std::string& uniformName) const noexcept;
    bool IsAttached() const noexcept;
    Program& GetProgram();
    const Program& GetProgram() const;

private:
    GraphicsDevice* device = nullptr;
    std::shared_ptr<ProgramCache> programCache;
    std::shared_ptr<Program> program;
    MaterialTextureBindings textures;
};
}  // namespace wisteria
