#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/wisteria_generic_runtime_driver.hpp"

#include <cmath>

namespace wisteria
{
WisteriaGenericRuntimeDriver::WisteriaGenericRuntimeDriver(
    const ModelAsset& asset
)
    : asset(&asset)
{
}

bool WisteriaGenericRuntimeDriver::Initialize()
{
    if (this->asset == nullptr)
        return false;
    // Contract §6: R1.5 does not support pose-less animation playback.
    if (this->asset->AnimationClipCount() > 0U &&
        !this->asset->HasSkeleton())
    {
        return false;
    }
    // A Generic runtime must own at least one dynamic channel; assets with
    // no skeleton and no morphs belong to Static and get no runtime.
    if (!this->asset->HasSkeleton() && !this->asset->HasMorphs())
        return false;

    if (this->asset->HasSkeleton())
    {
        this->pose = std::make_unique<Pose>(this->asset->GetSkeleton());
    }
    if (this->asset->HasMorphs())
    {
        this->morphState =
            std::make_unique<MorphState>(this->asset->GetMorphSet());
    }
    if (this->pose != nullptr)
    {
        this->animator = this->morphState != nullptr
            ? std::make_unique<Animator>(*this->pose, this->morphState.get())
            : std::make_unique<Animator>(*this->pose);
    }

    // Inherit the old Scene path's default playback behaviour (contract
    // §5.3): a skeleton-backed asset with clips starts clip 0 automatically.
    if (this->animator != nullptr &&
        this->asset->AnimationClipCount() > 0U)
    {
        this->animator->Play(this->asset->AnimationClipAt(0U));
    }
    return true;
}

void WisteriaGenericRuntimeDriver::Update(float deltaTime)
{
    if (this->animator != nullptr)
    {
        this->animator->Update(deltaTime);
        this->pendingRootMotion = this->animator->ConsumeRootMotion();
    }
}

void WisteriaGenericRuntimeDriver::Reset()
{
    if (this->animator != nullptr)
        this->animator->Stop(true);
    if (this->pose != nullptr)
        this->pose->ResetToBindPose();
    if (this->morphState != nullptr)
        this->morphState->Reset();
    this->pendingRootMotion = {};
}

Pose* WisteriaGenericRuntimeDriver::TryGetPose() noexcept
{
    return this->pose.get();
}

const Pose* WisteriaGenericRuntimeDriver::TryGetPose() const noexcept
{
    return this->pose.get();
}

Animator* WisteriaGenericRuntimeDriver::TryGetAnimator() noexcept
{
    return this->animator.get();
}

const Animator* WisteriaGenericRuntimeDriver::TryGetAnimator() const noexcept
{
    return this->animator.get();
}

MorphState* WisteriaGenericRuntimeDriver::TryGetMorphState() noexcept
{
    return this->morphState.get();
}

const MorphState* WisteriaGenericRuntimeDriver::TryGetMorphState()
    const noexcept
{
    return this->morphState.get();
}

RootMotionDelta WisteriaGenericRuntimeDriver::ConsumeRootMotion() noexcept
{
    const RootMotionDelta result = this->pendingRootMotion;
    this->pendingRootMotion = {};
    return result;
}

bool WisteriaGenericRuntimeDriver::NeedsDynamicVertexUpload() const noexcept
{
    // Generic models deform through GPU skinning from Pose matrices; there
    // is no CPU-deformed vertex frame.
    return false;
}

ModelVertexFrame WisteriaGenericRuntimeDriver::VertexFrame() const noexcept
{
    return {};
}

PhysicsInstance* WisteriaGenericRuntimeDriver::TryGetPhysicsInstance()
    noexcept
{
    return nullptr;
}

const PhysicsInstance* WisteriaGenericRuntimeDriver::
    TryGetPhysicsInstance() const noexcept
{
    return nullptr;
}

std::string_view WisteriaGenericRuntimeDriver::BackendName() const noexcept
{
    return "wisteria-generic";
}

std::size_t WisteriaGenericRuntimeDriver::MorphCount() const noexcept
{
    return this->morphState != nullptr ? this->morphState->MorphCount() : 0U;
}

bool WisteriaGenericRuntimeDriver::DescribeMorph(
    std::size_t index,
    MorphDescriptor& output
) const
{
    if (this->morphState == nullptr ||
        index >= this->morphState->MorphCount())
    {
        return false;
    }
    const MorphDefinition& definition =
        this->morphState->GetMorphSet().DefinitionAt(
            static_cast<MorphIndex>(index)
        );
    output.name = definition.name;
    output.kind = definition.kind;
    return true;
}

bool WisteriaGenericRuntimeDriver::ReadMorphState(
    std::size_t index,
    MorphRuntimeState& output
) const
{
    if (this->morphState == nullptr ||
        index >= this->morphState->MorphCount())
    {
        return false;
    }
    const MorphIndex morphIndex = static_cast<MorphIndex>(index);
    output.rawWeight = this->morphState->Weight(morphIndex);
    output.effectiveWeight =
        this->morphState->EffectiveWeights()[morphIndex];
    return true;
}

std::uint64_t WisteriaGenericRuntimeDriver::MorphRevision() const noexcept
{
    return this->morphState != nullptr
        ? this->morphState->Revision()
        : 0U;
}

bool WisteriaGenericRuntimeDriver::SetMorphWeight(
    std::string_view name,
    float weight
)
{
    if (this->morphState == nullptr || !std::isfinite(weight))
        return false;
    const std::optional<MorphIndex> index =
        this->morphState->GetMorphSet().FindMorph(name);
    if (!index.has_value())
        return false;
    this->morphState->SetWeight(*index, weight);
    return true;
}

std::optional<float> WisteriaGenericRuntimeDriver::MorphWeight(
    std::string_view name
) const
{
    if (this->morphState == nullptr)
        return std::nullopt;
    const std::optional<MorphIndex> index =
        this->morphState->GetMorphSet().FindMorph(name);
    if (!index.has_value())
        return std::nullopt;
    return this->morphState->Weight(*index);
}
}  // namespace wisteria
