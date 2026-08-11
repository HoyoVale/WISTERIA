#include "wisteria/common/pch.hpp"

#include "wisteria/rendering/render_frame_packet.hpp"

#include "wisteria/rendering/material.hpp"
#include "wisteria/scene/scene.hpp"

#include <memory>
#include <utility>

namespace wisteria
{
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

RenderFramePacket BuildRenderFramePacket(
    Scene& scene,
    const Camera& camera,
    const glm::mat4& projection
)
{
    RenderFramePacket packet;
    packet.camera = camera;
    packet.projection = projection;
    for (const auto& light : scene.DirectionalLights())
        packet.directionalLights.push_back(light.get());
    for (const auto& light : scene.PointLights())
        packet.pointLights.push_back(light.get());
    for (const auto& light : scene.SpotLights())
        packet.spotLights.push_back(light.get());
    packet.environment = scene.Environment();

    // Scene traversal: visibility, world transform, ModelRenderFrameView,
    // pose/morph state, runtime material override, opaque/transparent
    // classification. No GL, no runtime mutation.
    for (const std::unique_ptr<Entity>& entityPointer : scene.Entities())
    {
        Entity& entity = *entityPointer;
        if (!entity.IsVisible())
            continue;

        const glm::mat4 entityTransform = entity.GetTransform().Matrix();
        for (RenderPart& part : entity.RenderParts())
        {
            const glm::mat4 model =
                entityTransform * part.LocalTransform();
            ModelRenderFrameView frame;
            if (entity.TryGetModelInstance() != nullptr &&
                entity.TryGetModelInstance()->HasRuntime())
            {
                frame = entity.TryGetModelInstance()->LastRenderFrameView();
            }
            else
            {
                frame.pose = entity.TryGetPose();
                frame.morphState = entity.TryGetMorphState();
            }
            RenderCommand command{
                &part,
                model,
                frame.pose,
                frame.morphState
            };
            command.material = ResolveMaterialState(part, frame);
            if (EffectiveAlphaMode(
                    part.GetMaterial(),
                    command.material
                ) ==
                MaterialAlphaMode::Blend)
            {
                packet.transparentDraws.push_back(command);
            }
            else
            {
                packet.opaqueDraws.push_back(command);
            }
        }
    }

    // Physics debug lines: collected at extraction time so the GPU pass
    // never traverses Scene/Physics/Entity again.
    if (scene.Physics().DebugDrawEnabled())
    {
        const std::span<const PhysicsDebugLine> worldLines =
            scene.Physics().DebugLines();
        packet.debugLines.insert(
            packet.debugLines.end(),
            worldLines.begin(),
            worldLines.end()
        );
    }
    for (const std::unique_ptr<Entity>& entity : scene.Entities())
        entity->AppendPhysicsDebugLines(packet.debugLines);

    return packet;
}
}  // namespace wisteria
