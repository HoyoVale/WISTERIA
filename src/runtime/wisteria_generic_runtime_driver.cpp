#include "wisteria/common/pch.hpp"
#include "wisteria/runtime/wisteria_generic_runtime_driver.hpp"

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

std::uint64_t FnvBytesGeneric(
    const std::uint8_t* data,
    std::size_t size
) noexcept
{
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t state = kOffsetBasis;
    for (std::size_t index = 0U; index < size; ++index)
    {
        state ^= data[index];
        state *= kPrime;
    }
    return state;
}

void FnvU32Generic(std::uint64_t& state, std::uint32_t value) noexcept
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        state ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        state *= 1099511628211ULL;
    }
}

// R1.8 Final Fix: stable, platform-independent explicit byte encoding of
// immutable runtime semantics for the asset fingerprint.
class FingerprintBuilder
{
public:
    void Bytes(const void* data, std::size_t size)
    {
        this->state = FnvBytesGeneric(
            static_cast<const std::uint8_t*>(data),
            size
        ) ^ this->state;
        this->state *= 1099511628211ULL;
    }

    void U8(std::uint8_t value)
    {
        Bytes(&value, sizeof(value));
    }

    void Bool(bool value)
    {
        U8(value ? 1U : 0U);
    }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            U8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }

    void U64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            U8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }

    void F32(float value)
    {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }

    void String(std::string_view value)
    {
        U64(value.size());
        Bytes(value.data(), value.size());
    }

    void Vec2(const glm::vec2& value)
    {
        F32(value.x);
        F32(value.y);
    }

    void Vec3(const glm::vec3& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
    }

    void Vec4(const glm::vec4& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
        F32(value.w);
    }

    void Quat(const glm::quat& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
        F32(value.w);
    }

    void Interpolation(const KeyframeInterpolation& interpolation)
    {
        U32(static_cast<std::uint32_t>(interpolation.mode));
        Vec2(interpolation.controlPoint1);
        Vec2(interpolation.controlPoint2);
    }

    std::uint64_t Result() const noexcept
    {
        return this->state;
    }

private:
    std::uint64_t state = 14695981039346656037ULL;
};
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

    const Skeleton* skeleton = this->asset->TryGetSkeleton();
    if (skeleton != nullptr)
    {
        fp.U32(static_cast<std::uint32_t>(skeleton->BoneCount()));
        for (std::size_t index = 0U; index < skeleton->BoneCount(); ++index)
        {
            const Bone& bone = skeleton->BoneAt(
                static_cast<BoneIndex>(index)
            );
            fp.String(bone.name);
            fp.U32(bone.parentIndex);
            for (glm::length_t column = 0U; column < 4U; ++column)
            {
                for (glm::length_t row = 0U; row < 4U; ++row)
                    fp.F32(bone.bindLocalMatrix[column][row]);
            }
        }
    }
    else
    {
        fp.U32(0U);
    }

    const MorphSet* morphSet = this->asset->TryGetMorphSet();
    if (morphSet != nullptr)
    {
        fp.U32(static_cast<std::uint32_t>(morphSet->MorphCount()));
        for (const MorphDefinition& definition : morphSet->Definitions())
        {
            fp.String(definition.name);
            fp.U32(static_cast<std::uint32_t>(definition.category));
            fp.U32(static_cast<std::uint32_t>(definition.kind));
            fp.U32(static_cast<std::uint32_t>(
                definition.groupMembers.size()
            ));
            for (const GroupMorphMember& member : definition.groupMembers)
            {
                fp.U32(member.morphIndex);
                fp.F32(member.weight);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.flipMembers.size()
            ));
            for (const FlipMorphMember& member : definition.flipMembers)
            {
                fp.U32(member.morphIndex);
                fp.F32(member.weight);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.boneOffsets.size()
            ));
            for (const BoneMorphOffset& offset : definition.boneOffsets)
            {
                fp.U32(offset.boneIndex);
                fp.Vec3(offset.translation);
                fp.Quat(offset.rotation);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.materialOffsets.size()
            ));
            for (const MaterialMorphOffset& offset :
                 definition.materialOffsets)
            {
                fp.U32(offset.materialIndex);
                fp.U32(static_cast<std::uint32_t>(offset.operation));
                fp.Vec4(offset.diffuse);
                fp.Vec3(offset.specular);
                fp.F32(offset.shininess);
                fp.Vec3(offset.ambient);
                fp.Vec4(offset.edgeColor);
                fp.F32(offset.edgeSize);
                fp.Vec4(offset.textureFactor);
                fp.Vec4(offset.sphereTextureFactor);
                fp.Vec4(offset.toonTextureFactor);
            }
            fp.U32(static_cast<std::uint32_t>(
                definition.impulseOffsets.size()
            ));
            for (const ImpulseMorphOffset& offset :
                 definition.impulseOffsets)
            {
                fp.U32(offset.rigidBodyIndex);
                fp.Bool(offset.local);
                fp.Vec3(offset.velocity);
                fp.Vec3(offset.torque);
            }
        }
    }
    else
    {
        fp.U32(0U);
    }

    fp.U32(static_cast<std::uint32_t>(
        this->asset->AnimationClipCount()
    ));
    for (std::size_t index = 0U;
         index < this->asset->AnimationClipCount();
         ++index)
    {
        const AnimationClip& clip = this->asset->AnimationClipAt(index);
        fp.String(clip.Name());
        fp.F32(clip.Duration());
        fp.U32(static_cast<std::uint32_t>(clip.TrackCount()));
        for (const AnimationTrack& track : clip.Tracks())
        {
            fp.U32(track.Bone());
            fp.U32(static_cast<std::uint32_t>(
                track.TranslationKeys().size()
            ));
            for (const VectorKeyframe& key : track.TranslationKeys())
            {
                fp.F32(key.time);
                fp.Vec3(key.value);
                for (const KeyframeInterpolation& interpolation :
                     key.interpolation)
                {
                    fp.Interpolation(interpolation);
                }
            }
            fp.U32(static_cast<std::uint32_t>(
                track.RotationKeys().size()
            ));
            for (const QuaternionKeyframe& key : track.RotationKeys())
            {
                fp.F32(key.time);
                fp.Quat(key.value);
                fp.Interpolation(key.interpolation);
            }
            fp.U32(static_cast<std::uint32_t>(
                track.ScaleKeys().size()
            ));
            for (const VectorKeyframe& key : track.ScaleKeys())
            {
                fp.F32(key.time);
                fp.Vec3(key.value);
                for (const KeyframeInterpolation& interpolation :
                     key.interpolation)
                {
                    fp.Interpolation(interpolation);
                }
            }
        }
        fp.U32(static_cast<std::uint32_t>(
            clip.MorphWeightTrackCount()
        ));
        for (const MorphWeightTrack& track : clip.MorphWeightTracks())
        {
            fp.U32(track.Morph());
            fp.U32(static_cast<std::uint32_t>(track.Keys().size()));
            for (const FloatKeyframe& key : track.Keys())
            {
                fp.F32(key.time);
                fp.F32(key.value);
            }
        }
        fp.U32(static_cast<std::uint32_t>(
            clip.MmdIkStateTrackCount()
        ));
        for (const MmdIkStateTrack& track : clip.MmdIkStateTracks())
        {
            fp.U32(track.ControllerBone());
            fp.U32(static_cast<std::uint32_t>(track.Keys().size()));
            for (const BoolKeyframe& key : track.Keys())
            {
                fp.F32(key.time);
                fp.Bool(key.value);
            }
        }
    }
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
