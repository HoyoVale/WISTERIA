#pragma once

#include "wisteria/assets/model_asset.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/animation/animator.hpp"
#include "wisteria/core/root_motion.hpp"

#include <memory>
#include <string_view>

namespace wisteria
{
// R1.5 Phase 0C: WISTERIA-owned generic dynamic model runtime. It takes over
// the mutable Pose / MorphState / Animator that the pre-R1.5 Entity path
// owned directly; ModelAsset stays immutable and is the only input.
//
// Existence rules (contract §6):
//   HasSkeleton -> Pose + Animator exist
//   HasMorphs   -> MorphState exists
//   !HasSkeleton && HasMorphs -> MorphState only
//   AnimationClipCount > 0 && !HasSkeleton -> Initialize() fails honestly
class WisteriaGenericRuntimeDriver final : public IModelRuntimeDriver
{
public:
    explicit WisteriaGenericRuntimeDriver(const ModelAsset& asset);
    ~WisteriaGenericRuntimeDriver() override = default;

    WisteriaGenericRuntimeDriver(const WisteriaGenericRuntimeDriver&) = delete;
    WisteriaGenericRuntimeDriver& operator=(
        const WisteriaGenericRuntimeDriver&
    ) = delete;
    WisteriaGenericRuntimeDriver(WisteriaGenericRuntimeDriver&&) = delete;
    WisteriaGenericRuntimeDriver& operator=(WisteriaGenericRuntimeDriver&&) =
        delete;

    bool Initialize() override;
    void Update(float deltaTime) override;
    void Reset() override;

    Pose* TryGetPose() noexcept override;
    const Pose* TryGetPose() const noexcept override;
    Animator* TryGetAnimator() noexcept override;
    const Animator* TryGetAnimator() const noexcept override;
    MorphState* TryGetMorphState() noexcept override;
    const MorphState* TryGetMorphState() const noexcept override;

    RootMotionDelta ConsumeRootMotion() noexcept override;
    bool NeedsDynamicVertexUpload() const noexcept override;
    ModelVertexFrame VertexFrame() const noexcept override;
    PhysicsInstance* TryGetPhysicsInstance() noexcept override;
    const PhysicsInstance* TryGetPhysicsInstance() const noexcept override;
    std::string_view BackendName() const noexcept override;

    // Neutral morph bridge: all reads/writes route to the owned MorphState.
    std::size_t MorphCount() const noexcept override;
    bool DescribeMorph(
        std::size_t index,
        MorphDescriptor& output
    ) const override;
    bool ReadMorphState(
        std::size_t index,
        MorphRuntimeState& output
    ) const override;
    std::uint64_t MorphRevision() const noexcept override;
    bool SetMorphWeight(std::string_view name, float weight) override;
    std::optional<float> MorphWeight(std::string_view name) const override;

private:
    const ModelAsset* asset = nullptr;
    std::unique_ptr<Pose> pose;
    std::unique_ptr<MorphState> morphState;
    std::unique_ptr<Animator> animator;
    RootMotionDelta pendingRootMotion;
};
}  // namespace wisteria
