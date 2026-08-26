#pragma once
#include <glm/glm.hpp>


#include "wisteria/animation/animation.hpp"
#include "wisteria/animation/morph.hpp"
#include "wisteria/mmd/physics/mmd_physics_asset.hpp"
#include "wisteria/rendering/render_part.hpp"
#include "wisteria/animation/skeleton.hpp"
#include <cstddef>
#include <filesystem>
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
enum class ModelBackendKind : std::uint8_t
{
    Static = 0,
    SabaMmd = 1,
    WisteriaGeneric = 2
};

struct ModelSourceDescriptor
{
    std::filesystem::path sourcePath;
    ModelBackendKind backend = ModelBackendKind::Static;
};

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

    void SetSourceDescriptor(ModelSourceDescriptor descriptor);
    bool HasSourceDescriptor() const noexcept;
    const ModelSourceDescriptor* TryGetSourceDescriptor() const noexcept;
    const ModelSourceDescriptor& GetSourceDescriptor() const;
    // R1.5 Phase 0D: explicit backend identity is the sole authority.
    // ModelSourceDescriptor describes the source only; its legacy backend
    // field is informational compatibility residue and never participates
    // in runtime selection.
    void SetBackendKind(ModelBackendKind kind);
    bool HasExplicitBackendKind() const noexcept;
    ModelBackendKind BackendKind() const noexcept;
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

    // R1.8 Final Micro Fix: deterministic fingerprint of the whole immutable
    // ModelAsset: backend kind, part ordering/local transforms, mesh topology
    // (vertices/layout/indices/source mapping), mesh morph offsets (vertex +
    // UV), skeleton, morph definitions and animation clips/keys. This is the
    // asset identity gate for Generic checkpoints and offline sessions.
    std::uint64_t DeterministicFingerprint() const noexcept;

    RenderPart& AddPart(
        Mesh& mesh,
        Material& material,
        const glm::mat4& localTransform = glm::mat4(1.0f),
        std::optional<std::uint32_t> morphMaterialIndex = std::nullopt
    );

private:
    std::string name;
    std::optional<ModelBackendKind> backendKind;
    std::optional<ModelSourceDescriptor> sourceDescriptor;
    std::vector<RenderPart> parts;
    std::optional<Skeleton> skeleton;
    std::optional<MorphSet> morphSet;
    std::optional<MmdPhysicsAsset> mmdPhysics;
    // Stable clip addresses are required because Animator stores a pointer to
    // its current shared clip while more clips may still be added.
    std::vector<std::unique_ptr<AnimationClip>> animationClips;
};
}  // namespace wisteria
