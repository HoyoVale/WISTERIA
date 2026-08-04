#pragma once

#include "wisteria/animation/animation.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/mmd/physics/mmd_physics_asset.hpp"
#include "wisteria/rendering/render_part.hpp"
#include "wisteria/animation/skeleton.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shared, scene-independent description of an imported or procedural model.
namespace wisteria
{
class ModelAsset
{
public:
    explicit ModelAsset(std::string name);
    ~ModelAsset() = default;

    ModelAsset(const ModelAsset&) = delete;
    ModelAsset& operator=(const ModelAsset&) = delete;
    ModelAsset(ModelAsset&&) = delete;
    ModelAsset& operator=(ModelAsset&&) = delete;

    const std::string& Name() const noexcept;
    std::size_t PartCount() const noexcept;
    std::span<const RenderPart> Parts() const noexcept;

    bool HasSkeleton() const noexcept;
    const Skeleton* TryGetSkeleton() const noexcept;
    const Skeleton& GetSkeleton() const;
    void SetSkeleton(Skeleton skeleton);

    bool HasMorphs() const noexcept;
    const MorphSet* TryGetMorphSet() const noexcept;
    const MorphSet& GetMorphSet() const;
    void SetMorphs(std::vector<MorphDefinition> definitions);

    bool HasMmdPhysics() const noexcept;
    const MmdPhysicsAsset* TryGetMmdPhysics() const noexcept;
    const MmdPhysicsAsset& GetMmdPhysics() const;
    void SetMmdPhysics(MmdPhysicsAsset physics);
    std::size_t MmdRigidBodyCount() const noexcept;

    std::size_t AnimationClipCount() const noexcept;
    const AnimationClip& AnimationClipAt(std::size_t index) const;
    const AnimationClip* FindAnimationClip(std::string_view name) const noexcept;
    AnimationClip& AddAnimationClip(AnimationClip clip);

    RenderPart& AddPart(
        Mesh& mesh,
        Material& material,
        const glm::mat4& localTransform = glm::mat4(1.0f),
        std::optional<std::uint32_t> morphMaterialIndex = std::nullopt
    );

private:
    std::string name;
    std::vector<RenderPart> parts;
    std::optional<Skeleton> skeleton;
    std::optional<MorphSet> morphSet;
    std::optional<MmdPhysicsAsset> mmdPhysics;
    // Stable clip addresses are required because Animator stores a pointer to
    // its current shared clip while more clips may still be added.
    std::vector<std::unique_ptr<AnimationClip>> animationClips;
};
}  // namespace wisteria
