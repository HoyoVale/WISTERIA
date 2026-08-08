#pragma once

// Internal Renderer implementation details, private to src/rendering.
// Texture-unit constants, shared helpers and the RenderState guard live
// here so the split renderer translation units stay consistent. R0.2 showed
// that state leaking across frames (a texture still bound for sampling while
// it becomes a draw attachment) can black out Mesa/D3D12 without any GL
// error; RenderStateScope makes the frame/pass boundary restore explicit.

#include "wisteria/core/asset_paths.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/rendering/shader.hpp"
#include "wisteria/rendering/environment.hpp"
#include "wisteria/rendering/vao.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/physics/physics_instance.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace wisteria
{
namespace
{
constexpr unsigned int IrradianceTextureUnit = 8;
constexpr unsigned int PrefilterTextureUnit = 9;
constexpr unsigned int BrdfLutTextureUnit = 10;
constexpr unsigned int SkinningTextureUnit = 11;
// Post-processing uses dedicated high texture units so render-target textures
// never collide with material textures (normally starting at unit 0) or the
// environment/skinning bindings above. OpenGL 3.3 guarantees at least 16
// fragment texture units.
constexpr unsigned int ScenePresentTextureUnit = 12;
constexpr unsigned int OitAccumulationTextureUnit = 13;
constexpr unsigned int OitRevealageTextureUnit = 14;
constexpr unsigned int ShadowMapTextureUnit = 15;
constexpr int ShadowMapResolution = 2048;
constexpr std::size_t ShadowCascadeCount = 4;

void UnbindTexture2DFromUnit(unsigned int unit, GLuint texture)
{
    if (texture == 0)
        return;

    GLint previousActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0 + unit);
    GLint boundTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
    if (static_cast<GLuint>(boundTexture) == texture)
        glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
}

MaterialMorphValues EvaluateMaterialMorphs(
    const RenderPart& part,
    const MorphState* morphState
)
{
    const Material& material = part.GetMaterial();
    MaterialMorphValues values;
    values.diffuse = material.BaseColorFactor();
    values.specular = material.SpecularColor();
    values.shininess = material.Shininess();
    values.ambient = material.AmbientColor();
    values.edgeColor = material.EdgeColor();
    values.edgeSize = material.EdgeSize();

    if (material.ShadingModel() == MaterialShadingModel::MmdToon &&
        morphState != nullptr &&
        morphState->GetMorphSet().HasKind(MorphKind::Material))
    {
        morphState->GetMorphSet().ApplyMaterialMorphs(
            part.MorphMaterialIndex().value_or(AllMaterialMorphTargets),
            morphState->EffectiveWeights(),
            values
        );
    }
    return values;
}

// R1.6 Phase 0C: single resolved material state per part per frame.
// Priority: runtime material override (Saba, keyed by
// MorphMaterialIndex -> runtime slot) -> Generic MorphState -> base.
MaterialMorphValues ResolveMaterialState(
    const RenderPart& part,
    const ModelRenderFrameView& frame
)
{
    if (!frame.materials.empty())
    {
        const std::optional<std::uint32_t> slot =
            part.MorphMaterialIndex();
        if (slot.has_value())
        {
            if (*slot >= frame.materials.size())
            {
                throw std::logic_error(
                    "RenderPart runtime material slot is out of range"
                );
            }
            const MaterialRuntimeOverride& override =
                frame.materials[*slot];
            MaterialMorphValues values;
            values.diffuse = override.diffuse;
            values.specular = override.specular;
            values.shininess = override.shininess;
            values.ambient = override.ambient;
            values.edgeColor = override.edgeColor;
            values.edgeSize = override.edgeSize;
            values.textureFactor = override.textureMultiply;
            values.textureAdd = override.textureAdd;
            values.sphereTextureFactor = override.sphereTextureMultiply;
            values.sphereTextureAdd = override.sphereTextureAdd;
            values.toonTextureFactor = override.toonTextureMultiply;
            values.toonTextureAdd = override.toonTextureAdd;
            return values;
        }
        // MorphMaterialIndex == nullopt means this part is not connected to
        // a runtime material slot (e.g. a user-added RenderPart); fall back
        // to the MorphState / base material path.
    }
    return EvaluateMaterialMorphs(part, frame.morphState);
}

MaterialAlphaMode EffectiveAlphaMode(
    const Material& material,
    const MaterialMorphValues& values
)
{
    if (material.ShadingModel() == MaterialShadingModel::MmdToon &&
        values.diffuse.a < 0.999f)
    {
        return MaterialAlphaMode::Blend;
    }
    return material.AlphaMode();
}

}

// Referenced by Renderer's private pass declarations, so it must be visible
// at wisteria scope (matching the renderer.hpp forward declaration).
struct RenderCommand
{
    RenderPart* part = nullptr;
    glm::mat4 model{1.0f};
    const Pose* pose = nullptr;
    const MorphState* morphState = nullptr;
    MaterialMorphValues material;
};

constexpr unsigned int RendererBoundTextureUnits[] = {
    IrradianceTextureUnit,
    PrefilterTextureUnit,
    BrdfLutTextureUnit,
    SkinningTextureUnit,
    ScenePresentTextureUnit,
    OitAccumulationTextureUnit,
    OitRevealageTextureUnit,
    ShadowMapTextureUnit
};
constexpr std::size_t RendererTrackedTextureUnitCount =
    std::size(RendererBoundTextureUnits);

// OpenGL state the renderer mutates during a frame or present. Capturing and
// restoring it at pass boundaries prevents cross-frame/cross-window leaks.
struct RenderState
{
    GLint activeTexture = GL_TEXTURE0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    GLint drawBuffer = GL_BACK;
    GLboolean blendEnabled = GL_FALSE;
    GLint blendSource = GL_ONE;
    GLint blendDestination = GL_ZERO;
    GLboolean depthTestEnabled = GL_FALSE;
    GLboolean depthWriteMask = GL_TRUE;
    GLboolean scissorTestEnabled = GL_FALSE;
    GLboolean stencilTestEnabled = GL_FALSE;
    GLboolean rasterizerDiscardEnabled = GL_FALSE;
    GLboolean cullFaceEnabled = GL_FALSE;
    GLboolean colorWriteMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint boundTextures[RendererTrackedTextureUnitCount] = {0};
};

void CaptureRenderState(RenderState& state);
void RestoreRenderState(const RenderState& state);

// RAII: captures the tracked GL state on construction and restores it on
// destruction, including exception unwinding.
class RenderStateScope
{
public:
    RenderStateScope() { CaptureRenderState(this->saved); }
    ~RenderStateScope() { RestoreRenderState(this->saved); }
    RenderStateScope(const RenderStateScope&) = delete;
    RenderStateScope& operator=(const RenderStateScope&) = delete;

private:
    RenderState saved;
};
}  // namespace wisteria
