#pragma once

#include "animation.hpp"
#include "morph.hpp"
#include "render_part.hpp"
#include "skeleton.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shared, scene-independent description of an imported or procedural model.
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

    std::size_t MmdRigidBodyCount() const noexcept;
    void SetMmdRigidBodyCount(std::size_t count) noexcept;

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
    std::size_t mmdRigidBodyCount = 0U;
    // Stable clip addresses are required because Animator stores a pointer to
    // its current shared clip while more clips may still be added.
    std::vector<std::unique_ptr<AnimationClip>> animationClips;
};
