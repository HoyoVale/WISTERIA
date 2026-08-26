#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/wisteria_generic_runtime_driver.hpp"
#include "wisteria/core/deterministic_fingerprint.hpp"

#include <cmath>
#include <cstring>

namespace wisteria
{
namespace
{
// R1.8 frozen canonical motion rate (engine-owned coordinate; source clips
// remain continuous-time assets sampled at N/30).
constexpr float kGenericCanonicalMotionFps = 30.0f;

// Float canonical-time precision boundary: at 2^20 frames (~9.7h @30Hz) the
// 32-bit time still resolves adjacent frames (≈8 ulps per frame). Beyond
// this the engine refuses rather than silently losing exactness.
constexpr MotionFrameIndex kMaxGenericDeterministicFrame = 1U << 20;
}  // namespace

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
        this->ApplyPersistentMorphOverrides();
        this->ApplyVrmExpressionWeights();
    }
}

void WisteriaGenericRuntimeDriver::Reset()
{
    // Frozen B semantics (R1.5 Final Closure): Reset restores the runtime's
    // default startup playback state on the SAME objects.
    //   - default clip exists: AnimationClipAt(0), t=0, playing
    //   - no clip: bind pose, stopped
    //   - morph-only: initial zero weights
    // Reset never reallocates Pose/MorphState/Animator, so previously
    // published pointers stay valid.
    if (this->animator != nullptr)
    {
        // Animator::Stop(true) already returns Pose and MorphState to their
        // initial values; do not reset them a second time (that would bump
        // the Pose revision unnecessarily).
        this->animator->Stop(true);
        if (this->asset != nullptr &&
            this->asset->AnimationClipCount() > 0U)
        {
            this->animator->Play(this->asset->AnimationClipAt(0U));
        }
    }
    else
    {
        if (this->pose != nullptr)
            this->pose->ResetToBindPose();
        if (this->morphState != nullptr)
            this->morphState->Reset();
    }
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

bool WisteriaGenericRuntimeDriver::ValidateDeterministicConfig(
    const ReplayConfig& config
) noexcept
{
    return config.motionFps == 30U &&
        config.physicsHz == 120U &&
        config.warmupFrames == 0U;
}

TimelineStatus WisteriaGenericRuntimeDriver::PrepareFrameZero(
    const ReplayConfig& config
)
{
    if (!ValidateDeterministicConfig(config))
        return TimelineStatus::UnsupportedReplayProfile;
    if (this->animator == nullptr || this->asset == nullptr ||
        this->asset->AnimationClipCount() == 0U)
    {
        // Deterministic v1 requires a single active clip timeline; morph-
        // only and static assets have no canonical frames.
        return TimelineStatus::UnsupportedReplayProfile;
    }

    this->Reset();
    this->animator->SetLooping(config.loopMotion);
    if (!this->animator->IsDeterministicSubsetCompatible())
        return TimelineStatus::UnsupportedDeterministicState;

    // Frame 0: canonical evaluation at t=0; pending root motion is identity.
    this->animator->EvaluateCanonicalFrame(0.0f, 0.0f, config.loopMotion);
    this->pendingRootMotion = this->animator->ConsumeRootMotion();
    this->ApplyPersistentMorphOverrides();

    this->frozenConfig = config;
    this->deterministicPrepared = true;
    this->expectedNextFrame = 1U;
    return TimelineStatus::Ok;
}

TimelineStatus WisteriaGenericRuntimeDriver::StepMotionFrameExact(
    MotionFrameIndex frame,
    const ReplayConfig& config
)
{
    if (!this->deterministicPrepared)
        return TimelineStatus::InvalidState;
    if (config.motionFps != this->frozenConfig.motionFps ||
        config.physicsHz != this->frozenConfig.physicsHz ||
        config.warmupFrames != this->frozenConfig.warmupFrames ||
        config.loopMotion != this->frozenConfig.loopMotion)
    {
        return TimelineStatus::DeterminismViolation;
    }
    if (frame != this->expectedNextFrame)
        return TimelineStatus::NonSequentialFrame;
    if (frame > kMaxGenericDeterministicFrame)
        return TimelineStatus::UnsupportedReplayProfile;
    if (this->animator == nullptr ||
        !this->animator->IsDeterministicSubsetCompatible())
    {
        return TimelineStatus::UnsupportedDeterministicState;
    }

    // Coordinate evaluation: the delta comes from the canonical interval
    // [(N-1)/30, N/30], never from the last actual Animator time.
    const float previousTime =
        static_cast<float>(frame - 1U) / kGenericCanonicalMotionFps;
    const float currentTime =
        static_cast<float>(frame) / kGenericCanonicalMotionFps;
    this->animator->EvaluateCanonicalFrame(
        previousTime,
        currentTime,
        config.loopMotion
    );
    this->pendingRootMotion = this->animator->ConsumeRootMotion();
    this->ApplyPersistentMorphOverrides();
    this->expectedNextFrame = frame + 1U;
    return TimelineStatus::Ok;
}

ModelRuntimeCapabilities WisteriaGenericRuntimeDriver::Capabilities() const
{
    ModelRuntimeCapabilities capabilities;
    const bool hasTimeline = this->animator != nullptr &&
        this->asset != nullptr &&
        this->asset->AnimationClipCount() > 0U;
    // R1.8 Phase 0C: exact stepping + checkpoint/replay open together.
    capabilities.deterministic.supportsExactFrameStepping = hasTimeline;
    capabilities.deterministic.supportsCheckpointCapture = hasTimeline;
    capabilities.deterministic.supportsCheckpointRestore = hasTimeline;
    capabilities.deterministic.supportsReplayFromCheckpoint = hasTimeline;
    capabilities.checkpoint.supportsCheckpointCapture = hasTimeline;
    capabilities.checkpoint.supportsCheckpointRestore = hasTimeline;
    capabilities.checkpoint.supportsReplayFromCheckpoint = hasTimeline;
    return capabilities;
}

std::uint64_t WisteriaGenericRuntimeDriver::ComputeAssetFingerprint()
    const noexcept
{
    if (this->asset == nullptr)
        return FingerprintBuilder().Result();

    FingerprintBuilder fp;
    fp.String("wisteria-generic/runtime/v1");
    fp.U64(this->asset->DeterministicFingerprint());
    return fp.Result();
}

void WisteriaGenericRuntimeDriver::ApplyPersistentMorphOverrides()
{
    if (this->morphState == nullptr || this->morphOverrides.empty())
        return;
    const MorphSet& morphSet = this->morphState->GetMorphSet();
    for (const auto& [name, weight] : this->morphOverrides)
    {
        const std::optional<MorphIndex> index = morphSet.FindMorph(name);
        if (index.has_value())
            this->morphState->SetWeight(*index, weight);
    }
}

void WisteriaGenericRuntimeDriver::ApplyVrmExpressionWeights()
{
    if (this->morphState == nullptr || this->asset == nullptr)
        return;
    const VrmMetadata* vrm = this->asset->TryGetVrmMetadata();
    if (vrm == nullptr || this->vrmExpressionWeights.empty())
        return;

    std::unordered_map<MorphIndex, float> contributions;
    for (const VrmExpressionDefinition& expression : vrm->expressions)
    {
        const auto weight = this->vrmExpressionWeights.find(
            expression.name
        );
        if (weight == this->vrmExpressionWeights.end())
            continue;
        for (const VrmExpressionMorphBind& bind : expression.morphBinds)
        {
            if (bind.resolvedMorph == InvalidMorphIndex)
                continue;
            contributions[bind.resolvedMorph] +=
                weight->second * bind.weight;
        }
    }

    for (const auto& [morphIndex, weight] : contributions)
        this->morphState->SetWeight(morphIndex, weight);
}

bool WisteriaGenericRuntimeDriver::SetVrmExpressionWeight(
    std::string_view name,
    float weight
)
{
    if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
    {
        throw std::invalid_argument(
            "VRM expression weight must be finite and in [0, 1]"
        );
    }
    if (this->asset == nullptr)
        return false;
    const VrmMetadata* vrm = this->asset->TryGetVrmMetadata();
    if (vrm == nullptr)
        return false;
    for (const VrmExpressionDefinition& expression : vrm->expressions)
    {
        if (expression.name == name)
        {
            this->vrmExpressionWeights[std::string(name)] = weight;
            this->ApplyVrmExpressionWeights();
            return true;
        }
    }
    return false;
}

std::optional<float> WisteriaGenericRuntimeDriver::VrmExpressionWeight(
    std::string_view name
) const
{
    const auto entry = this->vrmExpressionWeights.find(std::string(name));
    if (entry == this->vrmExpressionWeights.end())
        return std::nullopt;
    return entry->second;
}

void WisteriaGenericRuntimeDriver::ClearVrmExpressionWeights()
{
    if (this->morphState != nullptr && this->asset != nullptr)
    {
        const VrmMetadata* vrm = this->asset->TryGetVrmMetadata();
        if (vrm != nullptr)
        {
            for (const VrmExpressionDefinition& expression :
                 vrm->expressions)
            {
                for (const VrmExpressionMorphBind& bind :
                     expression.morphBinds)
                {
                    if (bind.resolvedMorph != InvalidMorphIndex)
                    {
                        this->morphState->SetWeight(
                            bind.resolvedMorph,
                            0.0f
                        );
                    }
                }
            }
        }
    }
    this->vrmExpressionWeights.clear();
}

bool WisteriaGenericRuntimeDriver::SetMorphOverride(
    std::string_view name,
    float weight
)
{
    if (!std::isfinite(weight))
        throw std::invalid_argument("Morph override weight must be finite");
    if (this->morphState == nullptr)
        return false;
    const std::optional<MorphIndex> index =
        this->morphState->GetMorphSet().FindMorph(name);
    if (!index.has_value())
        return false;
    this->morphOverrides[std::string(name)] = weight;
    this->morphState->SetWeight(*index, weight);
    return true;
}

void WisteriaGenericRuntimeDriver::ClearMorphOverride(
    std::string_view name
)
{
    if (this->morphOverrides.erase(std::string(name)) == 0U)
        return;
    if (this->animator != nullptr)
        this->animator->Evaluate();
    this->ApplyPersistentMorphOverrides();
}

void WisteriaGenericRuntimeDriver::ClearAllMorphOverrides()
{
    if (this->morphOverrides.empty())
        return;
    this->morphOverrides.clear();
    if (this->animator != nullptr)
        this->animator->Evaluate();
    this->ApplyPersistentMorphOverrides();
}

TimelineStatus WisteriaGenericRuntimeDriver::CreateCheckpoint(
    GenericRuntimeCheckpoint& output
) const
{
    if (!this->deterministicPrepared)
        return TimelineStatus::InvalidState;
    if (this->animator == nullptr ||
        !this->animator->IsDeterministicSubsetCompatible())
    {
        return TimelineStatus::UnsupportedDeterministicState;
    }

    const MotionFrameIndex frame = this->expectedNextFrame - 1U;
    output.frame = frame;
    output.canonicalTime = this->animator->Time();
    output.looping = this->animator->IsLooping();
    output.playing = true;
    const float duration = this->animator->CurrentClip()->Duration();
    output.clipClamped =
        !output.looping &&
        output.canonicalTime >= duration - 0.0001f;
    output.rootMotionEnabled = this->animator->IsRootMotionEnabled();
    output.rootMotionBoneIndex =
        this->animator->RootMotionBone()
            ? std::optional<std::uint32_t>(
                  static_cast<std::uint32_t>(
                      *this->animator->RootMotionBone()
                  )
              )
            : std::nullopt;
    output.pendingRootMotion = this->pendingRootMotion;

    output.morphOverrides.clear();
    output.morphOverrides.reserve(this->morphOverrides.size());
    for (const auto& [name, weight] : this->morphOverrides)
        output.morphOverrides.emplace_back(name, weight);
    std::sort(
        output.morphOverrides.begin(),
        output.morphOverrides.end(),
        [](const auto& left, const auto& right)
        {
            return left.first < right.first;
        }
    );

    output.activeClipIndex.reset();
    const AnimationClip* currentClip = this->animator->CurrentClip();
    if (currentClip != nullptr && this->asset != nullptr)
    {
        for (std::size_t index = 0U;
             index < this->asset->AnimationClipCount();
             ++index)
        {
            if (&this->asset->AnimationClipAt(index) == currentClip)
            {
                output.activeClipIndex =
                    static_cast<std::uint32_t>(index);
                break;
            }
        }
    }
    output.motionFps = this->frozenConfig.motionFps;
    output.physicsHz = this->frozenConfig.physicsHz;
    output.warmupFrames = this->frozenConfig.warmupFrames;
    output.assetFingerprint = this->ComputeAssetFingerprint();
    return TimelineStatus::Ok;
}

TimelineStatus WisteriaGenericRuntimeDriver::RestoreCheckpoint(
    const GenericRuntimeCheckpoint& checkpoint
)
{
    if (this->animator == nullptr)
        return TimelineStatus::UnsupportedReplayProfile;
    if (!checkpoint.activeClipIndex.has_value() ||
        this->asset == nullptr ||
        *checkpoint.activeClipIndex >= this->asset->AnimationClipCount())
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    if (checkpoint.frame > kMaxGenericDeterministicFrame ||
        checkpoint.motionFps != 30U ||
        checkpoint.physicsHz != 120U ||
        checkpoint.warmupFrames != 0U)
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    if (checkpoint.assetFingerprint != this->ComputeAssetFingerprint())
        return TimelineStatus::InvalidCheckpoint;
    if (checkpoint.rootMotionEnabled &&
        !checkpoint.rootMotionBoneIndex.has_value())
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    if (checkpoint.rootMotionBoneIndex.has_value() &&
        this->pose != nullptr &&
        *checkpoint.rootMotionBoneIndex >= this->pose->BoneCount())
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    if (this->morphState == nullptr &&
        !checkpoint.morphOverrides.empty())
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    if (this->morphState != nullptr)
    {
        const MorphSet& morphSet = this->morphState->GetMorphSet();
        for (const auto& [name, weight] : checkpoint.morphOverrides)
        {
            if (!morphSet.FindMorph(name).has_value() ||
                !std::isfinite(weight))
            {
                return TimelineStatus::InvalidCheckpoint;
            }
        }
    }
    // R1.8 Final Fix: in-memory restore performs the same semantic
    // validation as the wire decoder (public RestoreCheckpoint must not
    // trust callers to have gone through the wire).
    if (!std::isfinite(checkpoint.canonicalTime) ||
        checkpoint.canonicalTime < 0.0f ||
        !checkpoint.playing)
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    const RootMotionDelta& pending = checkpoint.pendingRootMotion;
    if (!std::isfinite(pending.translation.x) ||
        !std::isfinite(pending.translation.y) ||
        !std::isfinite(pending.translation.z) ||
        !std::isfinite(pending.rotation.x) ||
        !std::isfinite(pending.rotation.y) ||
        !std::isfinite(pending.rotation.z) ||
        !std::isfinite(pending.rotation.w))
    {
        return TimelineStatus::InvalidCheckpoint;
    }
    const float quaternionNorm =
        pending.rotation.x * pending.rotation.x +
        pending.rotation.y * pending.rotation.y +
        pending.rotation.z * pending.rotation.z +
        pending.rotation.w * pending.rotation.w;
    if (std::abs(quaternionNorm - 1.0f) > 0.001f)
    {
        return TimelineStatus::InvalidCheckpoint;
    }

    this->Reset();
    this->animator->Play(
        this->asset->AnimationClipAt(*checkpoint.activeClipIndex),
        true
    );
    this->animator->SetLooping(checkpoint.looping);
    if (checkpoint.rootMotionBoneIndex.has_value())
    {
        this->animator->SetRootMotionBone(
            static_cast<BoneIndex>(*checkpoint.rootMotionBoneIndex)
        );
    }
    if (checkpoint.rootMotionEnabled)
    {
        this->animator->SetRootMotionEnabled(true);
    }
    if (!this->animator->IsDeterministicSubsetCompatible())
        return TimelineStatus::UnsupportedDeterministicState;

    const float frameTime =
        static_cast<float>(checkpoint.frame) / 30.0f;
    const float duration =
        this->animator->CurrentClip()->Duration();
    const float expectedTime = duration > 0.0f
        ? (checkpoint.looping
               ? std::fmod(frameTime, duration)
               : std::min(frameTime, duration))
        : 0.0f;
    if (std::abs(checkpoint.canonicalTime - expectedTime) > 0.0001f)
        return TimelineStatus::InvalidCheckpoint;
    const bool expectedClamped =
        !checkpoint.looping &&
        expectedTime >= duration - 0.0001f;
    if (checkpoint.clipClamped != expectedClamped)
        return TimelineStatus::InvalidCheckpoint;

    this->animator->EvaluateCanonicalFrame(
        frameTime,
        frameTime,
        checkpoint.looping
    );
    this->pendingRootMotion = checkpoint.pendingRootMotion;
    this->morphOverrides.clear();
    for (const auto& [name, weight] : checkpoint.morphOverrides)
        this->morphOverrides[name] = weight;
    this->ApplyPersistentMorphOverrides();

    this->frozenConfig.motionFps = checkpoint.motionFps;
    this->frozenConfig.physicsHz = checkpoint.physicsHz;
    this->frozenConfig.warmupFrames = checkpoint.warmupFrames;
    this->frozenConfig.loopMotion = checkpoint.looping;
    this->deterministicPrepared = true;
    this->expectedNextFrame = checkpoint.frame + 1U;
    return TimelineStatus::Ok;
}

TimelineStatus WisteriaGenericRuntimeDriver::ReplayFromCheckpoint(
    const GenericRuntimeCheckpoint& checkpoint,
    MotionFrameIndex target
)
{
    if (target <= checkpoint.frame)
        return TimelineStatus::InvalidCheckpoint;
    TimelineStatus status = this->RestoreCheckpoint(checkpoint);
    if (status != TimelineStatus::Ok)
        return status;
    const ReplayConfig config = this->frozenConfig;
    for (MotionFrameIndex frame = checkpoint.frame + 1U;
         frame <= target;
         ++frame)
    {
        status = this->StepMotionFrameExact(frame, config);
        if (status != TimelineStatus::Ok)
            return status;
    }
    return TimelineStatus::Ok;
}

bool WisteriaGenericRuntimeDriver::IsDeterministicRootMotionEnabled()
    const noexcept
{
    return this->animator != nullptr &&
        this->animator->IsRootMotionEnabled();
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
