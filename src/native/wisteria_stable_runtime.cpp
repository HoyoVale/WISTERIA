/*
 * R1.4 Phase 0B: Stable C ABI v1 subset implementation.
 *
 * Contract: docs/architecture/R1_4_STABLE_RUNTIME_BOUNDARY_CONTRACT.md
 *
 * Context-owned opaque handles (contract §4):
 *   Context └─ map<WisteriaEntity, StableEntityEntry>
 *            └─ map<WisteriaCheckpoint, FrameCheckpoint>
 *
 * All stable calls are creator-thread-affine; the registry lease is the
 * only shared state (one mutex lookup per call), and every entry runs
 * inside InvokeAbi so no C++ exception crosses the extern "C" boundary.
 */

#include "wisteria/native/wisteria_stable_runtime.h"

#include "internal/native_context.hpp"
#include "internal/stable_native_context.hpp"
#include "wisteria/assets/importer.hpp"
#include "wisteria/assets/saba_mmd_importer.hpp"
#include "wisteria/runtime/checkpoint_serialization.hpp"
#include "wisteria/runtime/generic_checkpoint.hpp"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/runtime/model_backend.hpp"
#include "wisteria/runtime/runtime_creation_options.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace wisteria;
using namespace wisteria::native;

namespace
{
constexpr std::uint64_t kStructuralFrameLimit =
    std::numeric_limits<std::uint64_t>::max() / 4U;
// Saba VMDAnimation::Evaluate(float) exact integer domain (contract §2D).
constexpr std::uint64_t kSabaExactFrameLimit = 16777216ULL;
// R1.8 Generic canonical-time precision boundary (contract §4.1).
constexpr std::uint64_t kGenericExactFrameLimit = 1ULL << 20;

// The Stable v1 surface advertises max_deterministic_motion_frame = 2^24
// (Saba float exact domain). Every exact-timeline entry and every stored
// checkpoint must respect this bound even though the core structural guard
// is wider (UINT64_MAX / 4).
bool StableFrameWithinExactDomain(std::uint64_t frame) noexcept
{
    return frame <= kSabaExactFrameLimit;
}

std::uint64_t StableMaxFrameFor(
    const IModelRuntimeDriver& runtime
) noexcept
{
    if (dynamic_cast<const MmdRuntimeModel*>(&runtime) != nullptr)
        return kSabaExactFrameLimit;
    if (dynamic_cast<const IDeterministicCheckpoint*>(&runtime) != nullptr)
        return kGenericExactFrameLimit;
    return kStructuralFrameLimit;
}

std::string LowerAsciiExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    // MSVC path::string() can include a trailing NUL character; strip it
    // before any comparison (".pmx\0" != ".pmx").
    const std::size_t nul = extension.find('\0');
    if (nul != std::string::npos)
        extension.resize(nul);
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    return extension;
}

// Asset identity validity (Phase 0B precondition): the PMX hash must be
// present, and a claimed VMD must carry a real hash ("hasMotion" is what
// distinguishes no-VMD from vmdFileHash == 0).
bool StableAssetIdentityValid(const FrameCheckpoint& checkpoint) noexcept
{
    return checkpoint.fingerprint.asset.pmxFileHash != 0U &&
        (!checkpoint.fingerprint.asset.hasMotion ||
         checkpoint.fingerprint.asset.vmdFileHash != 0U);
}

bool StableAssetIdentityValid(
    const GenericRuntimeCheckpoint& checkpoint
) noexcept
{
    return checkpoint.assetFingerprint != 0U;
}

bool StableAssetIdentityValid(
    const std::variant<FrameCheckpoint, GenericRuntimeCheckpoint>& value
) noexcept
{
    if (std::holds_alternative<GenericRuntimeCheckpoint>(value))
    {
        return StableAssetIdentityValid(
            std::get<GenericRuntimeCheckpoint>(value)
        );
    }
    return StableAssetIdentityValid(std::get<FrameCheckpoint>(value));
}

std::uint64_t StableCheckpointFrame(
    const std::variant<FrameCheckpoint, GenericRuntimeCheckpoint>& value
) noexcept
{
    if (std::holds_alternative<GenericRuntimeCheckpoint>(value))
    {
        return std::get<GenericRuntimeCheckpoint>(value).frame;
    }
    return std::get<FrameCheckpoint>(value).frame;
}

std::uint32_t StableCheckpointPayloadKind(
    const std::variant<FrameCheckpoint, GenericRuntimeCheckpoint>& value
) noexcept
{
    return std::holds_alternative<GenericRuntimeCheckpoint>(value)
        ? WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18
        : WISTERIA_CHECKPOINT_PAYLOAD_KIND_MMD_R12C;
}

std::vector<std::uint8_t> StableSerializeCheckpoint(
    const std::variant<FrameCheckpoint, GenericRuntimeCheckpoint>& value
)
{
    if (std::holds_alternative<GenericRuntimeCheckpoint>(value))
    {
        return SerializeGenericCheckpoint(
            std::get<GenericRuntimeCheckpoint>(value)
        );
    }
    return SerializeCheckpoint(std::get<FrameCheckpoint>(value));
}

std::uint32_t StableWirePayloadKind(
    const std::uint8_t* bytes,
    std::uint64_t size
) noexcept
{
    // Envelope header: magic(4) + wireVersion(4) + payloadKind(4).
    if (bytes == nullptr || size < 12U)
        return 0U;
    return static_cast<std::uint32_t>(bytes[8]) |
        (static_cast<std::uint32_t>(bytes[9]) << 8U) |
        (static_cast<std::uint32_t>(bytes[10]) << 16U) |
        (static_cast<std::uint32_t>(bytes[11]) << 24U);
}

std::uint32_t MapTimelineStatus(TimelineStatus status) noexcept
{
    switch (status)
    {
        case TimelineStatus::Ok:
            return WISTERIA_STATUS_OK;
        case TimelineStatus::NoPhysics:
            return WISTERIA_STATUS_NO_PHYSICS;
        case TimelineStatus::InvalidCheckpoint:
            return WISTERIA_STATUS_INVALID_CHECKPOINT;
        case TimelineStatus::UnsupportedReplayProfile:
            return WISTERIA_STATUS_UNSUPPORTED_REPLAY_PROFILE;
        case TimelineStatus::InvalidState:
            return WISTERIA_STATUS_INVALID_STATE;
        case TimelineStatus::NonSequentialFrame:
            return WISTERIA_STATUS_NON_SEQUENTIAL_FRAME;
        case TimelineStatus::DeterminismViolation:
            return WISTERIA_STATUS_DETERMINISM_VIOLATION;
        case TimelineStatus::SnapshotMismatch:
            return WISTERIA_STATUS_SNAPSHOT_MISMATCH;
        case TimelineStatus::InvalidSnapshot:
            return WISTERIA_STATUS_INVALID_SNAPSHOT;
        case TimelineStatus::Poisoned:
            return WISTERIA_STATUS_POISONED;
    }
    return WISTERIA_STATUS_INTERNAL;
}

std::uint32_t StableInvalidArgument(
    Context* context,
    const char* message
)
{
    TrySetError(context, message);
    return WISTERIA_STATUS_INVALID_ARGUMENT;
}

std::uint32_t StableNotFound(
    Context* context,
    const char* message
)
{
    TrySetError(context, message);
    return WISTERIA_STATUS_NOT_FOUND;
}

MmdRuntimeModel* RequireStableMmd(
    Context& context,
    WisteriaEntity handle
)
{
    StableEntityEntry* entry = FindStableEntity(context, handle);
    if (entry == nullptr)
    {
        TrySetError(&context, "unknown stable entity handle");
        return nullptr;
    }
    auto* mmd = entry->modelInstance != nullptr
        ? dynamic_cast<MmdRuntimeModel*>(
              entry->modelInstance->TryGetRuntime()
          )
        : nullptr;
    if (mmd == nullptr)
    {
        TrySetError(
            &context,
            "stable v1 requires an MMD-compatible runtime"
        );
    }
    return mmd;
}

IModelRuntimeDriver* RequireStableRuntime(
    Context& context,
    WisteriaEntity handle
)
{
    StableEntityEntry* entry = FindStableEntity(context, handle);
    if (entry == nullptr)
    {
        TrySetError(&context, "unknown stable entity handle");
        return nullptr;
    }
    return entry->modelInstance != nullptr
        ? entry->modelInstance->TryGetRuntime()
        : nullptr;
}

RuntimeCompatibilityProfile MapCompatibilityProfile(
    std::uint32_t compatibility
)
{
    switch (compatibility)
    {
        case WISTERIA_PROFILE_ID_RAW:
            return RuntimeCompatibilityProfile::Raw;
        case WISTERIA_PROFILE_ID_COMMUNITY:
            return RuntimeCompatibilityProfile::Community;
        case WISTERIA_PROFILE_ID_ADAPTIVE:
            return RuntimeCompatibilityProfile::Adaptive;
    }
    throw std::invalid_argument("unknown compatibility profile id");
}
}  // namespace

extern "C"
{
std::uint32_t wisteria_stable_context_create(
    WisteriaStableContext* out_context
)
{
    if (out_context == nullptr)
        return WISTERIA_STATUS_INVALID_ARGUMENT;
    try
    {
        *out_context = RegisterContext();
        return WISTERIA_STATUS_OK;
    }
    catch (...)
    {
        *out_context = 0U;
        return WISTERIA_STATUS_INTERNAL;
    }
}

std::uint32_t wisteria_stable_context_destroy(
    WisteriaStableContext context
)
{
    return InvokeAbi(context, [&](Context&)
    {
        return UnregisterContext(context)
            ? WISTERIA_STATUS_OK
            : WISTERIA_STATUS_NOT_FOUND;
    });
}

std::uint32_t wisteria_stable_context_info(
    WisteriaStableContext context,
    WisteriaStableContextInfoV1* info
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (info == nullptr)
            return StableInvalidArgument(&ctx, "info must not be null");
        if (info->struct_version != 1U ||
            info->struct_size < sizeof(*info))
        {
            return StableInvalidArgument(
                &ctx,
                "unsupported context info struct version/size"
            );
        }
        info->struct_size = sizeof(*info);
        info->struct_version = 1U;
        info->abi_version = WISTERIA_STABLE_RUNTIME_ABI_VERSION;
        for (std::uint32_t& reserved : info->reserved)
            reserved = 0U;
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_create(
    WisteriaStableContext context,
    const WisteriaRuntimeCreationOptionsV1* options,
    const char* model_path_utf8,
    WisteriaEntity* out_entity
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (options == nullptr || model_path_utf8 == nullptr ||
            out_entity == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "options, model_path_utf8 and out_entity must be non-null"
            );
        }
        if (options->struct_size < sizeof(*options) ||
            options->struct_version != 1U)
        {
            return StableInvalidArgument(
                &ctx,
                "unsupported runtime creation options version"
            );
        }
        if (options->physics_enabled != 0 &&
            options->physics_enabled != 1)
        {
            return StableInvalidArgument(
                &ctx,
                "physics_enabled must be 0 or 1"
            );
        }
        if (options->reserved != 0U)
        {
            return StableInvalidArgument(
                &ctx,
                "reserved fields must be zero"
            );
        }
        for (const std::uint32_t value : options->reserved2)
        {
            if (value != 0U)
            {
                return StableInvalidArgument(
                    &ctx,
                    "reserved fields must be zero"
                );
            }
        }
        if (options->fixed_time_step <= 0.0f ||
            options->max_sub_steps <= 0 ||
            !std::isfinite(options->fixed_time_step) ||
            !std::isfinite(options->gravity[0]) ||
            !std::isfinite(options->gravity[1]) ||
            !std::isfinite(options->gravity[2]))
        {
            return StableInvalidArgument(
                &ctx,
                "physics settings must be finite and positive"
            );
        }

        RuntimeCreationOptions coreOptions;
        try
        {
            coreOptions.compatibility =
                MapCompatibilityProfile(options->compatibility);
        }
        catch (const std::invalid_argument& error)
        {
            return StableInvalidArgument(&ctx, error.what());
        }
        coreOptions.physics.fixedTimeStep = options->fixed_time_step;
        coreOptions.physics.maxSubSteps = options->max_sub_steps;
        coreOptions.physics.gravity = glm::vec3(
            options->gravity[0],
            options->gravity[1],
            options->gravity[2]
        );
        coreOptions.physics.enabled = options->physics_enabled != 0;

        const std::filesystem::path modelPath =
            PathFromUtf8(model_path_utf8);
        if (modelPath.empty())
        {
            return StableInvalidArgument(
                &ctx,
                "model path must not be empty"
            );
        }

        // R1.9 Phase 0B: backend selection is engine-owned and driven by the
        // imported result, never by a caller-facing backend selector.
        const bool isPmx = LowerAsciiExtension(modelPath) == ".pmx";
        ImportedModelData imported = isPmx
            ? SabaMmdImporter().Import(modelPath)
            : ModelImporter().Import(modelPath);
        ModelBackendKind backendKind = ModelBackendKind::Static;
        if (isPmx)
        {
            backendKind = ModelBackendKind::SabaMmd;
        }
        else if (imported.skeleton.has_value() ||
            !imported.morphs.empty() ||
            !imported.animations.empty())
        {
            backendKind = ModelBackendKind::WisteriaGeneric;
        }

        auto asset = std::make_unique<ModelAsset>(model_path_utf8);
        asset->SetSourceDescriptor(ModelSourceDescriptor{
            modelPath,
            backendKind
        });
        asset->SetBackendKind(backendKind);
        if (imported.skeleton.has_value())
            asset->SetSkeleton(std::move(*imported.skeleton));
        if (imported.mmdPhysics.has_value())
            asset->SetMmdPhysics(std::move(*imported.mmdPhysics));
        if (!imported.morphs.empty())
            asset->SetMorphs(std::move(imported.morphs));
        for (AnimationClip& clip : imported.animations)
            asset->AddAnimationClip(std::move(clip));

        // Parts carry mesh topology into the deterministic fingerprint.
        // A single default material is enough: the fingerprint folds parts,
        // meshes and morph targets, not material contents, and stable
        // runtime entities never render.
        auto entry = std::make_unique<StableEntityEntry>();
        entry->material = std::make_unique<Material>(MaterialData{});
        std::vector<std::optional<std::uint32_t>> morphMaterialIndices;
        entry->meshes.reserve(imported.meshes.size());
        morphMaterialIndices.reserve(imported.meshes.size());
        for (ImportedMeshData& meshData : imported.meshes)
        {
            morphMaterialIndices.push_back(meshData.morphMaterialIndex);
            entry->meshes.push_back(
                std::make_unique<Mesh>(
                    std::move(meshData.data),
                    meshData.requiredBoneCount,
                    std::move(meshData.morphTargets),
                    std::move(meshData.sourceVertexIndices),
                    nullptr
                )
            );
        }
        for (const ImportedPartData& part : imported.parts)
        {
            if (part.meshIndex >= entry->meshes.size())
            {
                return StableInvalidArgument(
                    &ctx,
                    "imported part references an invalid mesh index"
                );
            }
            asset->AddPart(
                *entry->meshes[part.meshIndex],
                *entry->material,
                part.localTransform,
                morphMaterialIndices[part.meshIndex]
            );
        }

        std::unique_ptr<IModelRuntimeDriver> runtime;
        try
        {
            runtime = ctx.stable->backends.CreateRuntime(
                *asset,
                coreOptions
            );
        }
        catch (const std::exception& error)
        {
            TrySetError(&ctx, error.what());
            return WISTERIA_STATUS_INITIALIZATION;
        }
        if (runtime == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "backend did not create a runtime"
            );
        }

        entry->asset = std::move(asset);
        entry->modelInstance = std::make_unique<ModelInstance>(
            *entry->asset,
            std::move(runtime)
        );
        const WisteriaEntity handle =
            static_cast<WisteriaEntity>(AllocateOpaqueHandle());
        ctx.stable->entities.emplace(handle, std::move(entry));
        *out_entity = handle;
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_destroy(
    WisteriaStableContext context,
    WisteriaEntity entity
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (ctx.stable->entities.erase(entity) == 0U)
        {
            return StableNotFound(
                &ctx,
                "unknown stable entity handle"
            );
        }
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_capabilities(
    WisteriaStableContext context,
    WisteriaEntity entity,
    WisteriaRuntimeCapabilitiesV1* capabilities
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (capabilities == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "capabilities must not be null"
            );
        }
        if (capabilities->struct_version != 1U ||
            capabilities->struct_size < sizeof(*capabilities))
        {
            return StableInvalidArgument(
                &ctx,
                "unsupported capabilities struct version/size"
            );
        }
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;

        const ModelRuntimeCapabilities core = runtime->Capabilities();
        const bool isMmd =
            dynamic_cast<MmdRuntimeModel*>(runtime) != nullptr;
        const bool isGeneric =
            dynamic_cast<IDeterministicCheckpoint*>(runtime) != nullptr;
        capabilities->struct_size = sizeof(*capabilities);
        capabilities->struct_version = 1U;
        capabilities->capability_flags = 0U;
        if (core.deterministic.supportsExactFrameStepping)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_DETERMINISTIC_EXACT_FRAME;
        }
        if (core.physics.supportsSnapshotCapture)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_SNAPSHOT_CAPTURE;
        }
        if (core.physics.supportsSnapshotRestore)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_SNAPSHOT_RESTORE;
        }
        // R1.8: deterministic is authoritative; checkpoint is a mirror.
        if (core.deterministic.supportsCheckpointCapture)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_CHECKPOINT_CAPTURE |
                WISTERIA_CAP_SUPPORTS_CHECKPOINT_SERIALIZATION;
        }
        if (core.deterministic.supportsCheckpointRestore)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_CHECKPOINT_RESTORE;
        }
        if (core.deterministic.supportsReplayFromCheckpoint)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_REPLAY_FROM_CHECKPOINT;
        }
        capabilities->runtime_backend_id = isMmd
            ? WISTERIA_BACKEND_ID_SABA_MMD
            : (isGeneric
                   ? WISTERIA_BACKEND_ID_WISTERIA_GENERIC
                   : WISTERIA_BACKEND_ID_UNKNOWN);
        capabilities->runtime_backend_version = 1U;
        capabilities->deterministic_profile_id = isGeneric
            ? WISTERIA_DETERMINISTIC_PROFILE_GENERIC_V1
            : WISTERIA_DETERMINISTIC_PROFILE_COLD_STEP_V1;
        capabilities->checkpoint_payload_kind = isGeneric
            ? WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18
            : WISTERIA_CHECKPOINT_PAYLOAD_KIND_MMD_R12C;
        capabilities->structural_frame_limit = kStructuralFrameLimit;
        capabilities->max_deterministic_motion_frame =
            StableMaxFrameFor(*runtime);
        for (std::uint32_t& reserved : capabilities->reserved2)
            reserved = 0U;
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_set_morph_override(
    WisteriaStableContext context,
    WisteriaEntity entity,
    const char* morph_name_utf8,
    float weight
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (morph_name_utf8 == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "morph_name_utf8 must not be null"
            );
        }
        if (!std::isfinite(weight))
        {
            return StableInvalidArgument(
                &ctx,
                "morph override weight must be finite"
            );
        }
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        if (!runtime->SetMorphOverride(morph_name_utf8, weight))
        {
            TrySetError(
                &ctx,
                "morph override is not supported for this runtime/morph"
            );
            return WISTERIA_STATUS_UNSUPPORTED;
        }
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_clear_morph_override(
    WisteriaStableContext context,
    WisteriaEntity entity,
    const char* morph_name_utf8
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (morph_name_utf8 == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "morph_name_utf8 must not be null"
            );
        }
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        runtime->ClearMorphOverride(morph_name_utf8);
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_clear_all_morph_overrides(
    WisteriaStableContext context,
    WisteriaEntity entity
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        runtime->ClearAllMorphOverrides();
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_asset_fingerprint(
    WisteriaStableContext context,
    WisteriaEntity entity,
    std::uint64_t* out_fingerprint
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (out_fingerprint == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "out_fingerprint must not be null"
            );
        }
        StableEntityEntry* entry = FindStableEntity(ctx, entity);
        if (entry == nullptr)
        {
            TrySetError(&ctx, "unknown stable entity handle");
            return WISTERIA_STATUS_NOT_FOUND;
        }
        *out_fingerprint = entry->asset->DeterministicFingerprint();
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_load_motion(
    WisteriaStableContext context,
    WisteriaEntity entity,
    const char* vmd_path_utf8
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (vmd_path_utf8 == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "vmd_path_utf8 must not be null"
            );
        }
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        const std::filesystem::path vmdPath =
            PathFromUtf8(vmd_path_utf8);
        if (!mmd->LoadMotion(vmdPath))
        {
            TrySetError(&ctx, "failed to load VMD motion");
            return WISTERIA_STATUS_PARSE;
        }
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_unload_motion(
    WisteriaStableContext context,
    WisteriaEntity entity
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        mmd->ClearMotion();
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_prepare_frame_zero(
    WisteriaStableContext context,
    WisteriaEntity entity
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        if (auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime))
        {
            // The frozen deterministic profile forbids looping; entering the
            // deterministic timeline disables it before any boundary is
            // built (SetMotionLooping invalidates the current boundary, so
            // it must run before PrepareFrameZero, not at capture time).
            mmd->SetMotionLooping(false);
        }
        auto* stepper =
            dynamic_cast<IDeterministicFrameStepper*>(runtime);
        if (stepper == nullptr)
            return WISTERIA_STATUS_UNSUPPORTED_REPLAY_PROFILE;
        return MapTimelineStatus(stepper->PrepareFrameZero({}));
    });
}

std::uint32_t wisteria_stable_entity_step_exact(
    WisteriaStableContext context,
    WisteriaEntity entity,
    std::uint64_t frame
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        if (frame > StableMaxFrameFor(*runtime))
        {
            TrySetError(
                &ctx,
                "frame exceeds the backend exact deterministic domain"
            );
            return WISTERIA_STATUS_INVALID_STATE;
        }
        auto* stepper =
            dynamic_cast<IDeterministicFrameStepper*>(runtime);
        if (stepper == nullptr)
            return WISTERIA_STATUS_UNSUPPORTED_REPLAY_PROFILE;
        return MapTimelineStatus(
            stepper->StepMotionFrameExact(frame, {})
        );
    });
}

std::uint32_t wisteria_stable_entity_replay_exact(
    WisteriaStableContext context,
    WisteriaEntity entity,
    std::uint64_t target
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        if (target > StableMaxFrameFor(*runtime))
        {
            TrySetError(
                &ctx,
                "target exceeds the backend exact deterministic domain"
            );
            return WISTERIA_STATUS_INVALID_STATE;
        }
        if (auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime))
        {
            mmd->SetMotionLooping(false);
            return MapTimelineStatus(
                mmd->EvaluateTick(target, SeekPolicy::ReplayFromStart, {})
            );
        }
        auto* stepper =
            dynamic_cast<IDeterministicFrameStepper*>(runtime);
        if (stepper == nullptr)
            return WISTERIA_STATUS_UNSUPPORTED_REPLAY_PROFILE;
        TimelineStatus status = stepper->PrepareFrameZero({});
        if (status != TimelineStatus::Ok)
            return MapTimelineStatus(status);
        for (std::uint64_t frame = 1U; frame <= target; ++frame)
        {
            status = stepper->StepMotionFrameExact(frame, {});
            if (status != TimelineStatus::Ok)
                return MapTimelineStatus(status);
        }
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_entity_set_preview_frame(
    WisteriaStableContext context,
    WisteriaEntity entity,
    double frame
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        if (!std::isfinite(frame) || frame < 0.0)
        {
            return StableInvalidArgument(
                &ctx,
                "preview frame must be finite and non-negative"
            );
        }
        mmd->SetMotionFrame(frame);
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_checkpoint_create(
    WisteriaStableContext context,
    WisteriaEntity entity,
    WisteriaCheckpoint* out_checkpoint
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (out_checkpoint == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "out_checkpoint must not be null"
            );
        }
        IModelRuntimeDriver* runtime = RequireStableRuntime(ctx, entity);
        if (runtime == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        const WisteriaCheckpoint handle =
            static_cast<WisteriaCheckpoint>(AllocateOpaqueHandle());
        if (auto* generic =
                dynamic_cast<IDeterministicCheckpoint*>(runtime))
        {
            GenericRuntimeCheckpoint checkpoint;
            const std::uint32_t status =
                MapTimelineStatus(generic->CreateCheckpoint(checkpoint));
            if (status != WISTERIA_STATUS_OK)
                return status;
            if (!StableAssetIdentityValid(checkpoint))
            {
                TrySetError(
                    &ctx,
                    "asset identity is invalid; checkpoint capture rejected"
                );
                return WISTERIA_STATUS_INVALID_STATE;
            }
            ctx.stable->checkpoints.emplace(
                handle,
                std::move(checkpoint)
            );
        }
        else if (auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime))
        {
            if (mmd->IsMotionLooping())
                return WISTERIA_STATUS_INVALID_STATE;
            FrameCheckpoint checkpoint;
            const std::uint32_t status =
                MapTimelineStatus(mmd->CreateCheckpoint(checkpoint));
            if (status != WISTERIA_STATUS_OK)
                return status;
            if (!StableAssetIdentityValid(checkpoint))
            {
                TrySetError(
                    &ctx,
                    "asset identity is invalid; checkpoint capture rejected"
                );
                return WISTERIA_STATUS_INVALID_STATE;
            }
            ctx.stable->checkpoints.emplace(
                handle,
                std::move(checkpoint)
            );
        }
        else
        {
            return WISTERIA_STATUS_UNSUPPORTED_REPLAY_PROFILE;
        }
        *out_checkpoint = handle;
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_checkpoint_restore(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint,
    WisteriaEntity entity
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        const auto checkpointIterator =
            ctx.stable->checkpoints.find(checkpoint);
        if (checkpointIterator == ctx.stable->checkpoints.end())
        {
            return StableNotFound(
                &ctx,
                "unknown stable checkpoint handle"
            );
        }
        StableEntityEntry* entry = FindStableEntity(ctx, entity);
        if (entry == nullptr)
        {
            TrySetError(&ctx, "unknown stable entity handle");
            return WISTERIA_STATUS_NOT_FOUND;
        }
        const auto& value = checkpointIterator->second;
        if (StableCheckpointFrame(value) >
            StableMaxFrameFor(
                *entry->modelInstance->TryGetRuntime()
            ))
        {
            TrySetError(
                &ctx,
                "checkpoint frame exceeds the backend exact domain"
            );
            return WISTERIA_STATUS_INVALID_STATE;
        }
        if (std::holds_alternative<GenericRuntimeCheckpoint>(value))
        {
            auto* generic = dynamic_cast<IDeterministicCheckpoint*>(
                entry->modelInstance->TryGetRuntime()
            );
            if (generic == nullptr)
                return WISTERIA_STATUS_INVALID_CHECKPOINT;
            return MapTimelineStatus(
                generic->RestoreCheckpoint(
                    std::get<GenericRuntimeCheckpoint>(value)
                )
            );
        }
        MmdRuntimeModel* mmd =
            dynamic_cast<MmdRuntimeModel*>(
                entry->modelInstance->TryGetRuntime()
            );
        if (mmd == nullptr)
            return WISTERIA_STATUS_INVALID_CHECKPOINT;
        const FrameCheckpoint& frameCheckpoint =
            std::get<FrameCheckpoint>(value);
        return MapTimelineStatus(
            mmd->ReplayFromCheckpoint(
                frameCheckpoint,
                frameCheckpoint.frame
            )
        );
    });
}

std::uint32_t wisteria_stable_checkpoint_destroy(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (ctx.stable->checkpoints.erase(checkpoint) == 0U)
        {
            return StableNotFound(
                &ctx,
                "unknown stable checkpoint handle"
            );
        }
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_checkpoint_info(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint,
    WisteriaCheckpointInfoV1* info
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (info == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "info must not be null"
            );
        }
        if (info->struct_version != 1U ||
            info->struct_size < sizeof(*info))
        {
            return StableInvalidArgument(
                &ctx,
                "unsupported checkpoint info struct version/size"
            );
        }
        const auto iterator = ctx.stable->checkpoints.find(checkpoint);
        if (iterator == ctx.stable->checkpoints.end())
        {
            return StableNotFound(
                &ctx,
                "unknown stable checkpoint handle"
            );
        }
        const auto& value = iterator->second;
        const std::vector<std::uint8_t> wire =
            StableSerializeCheckpoint(value);
        const bool isGeneric =
            std::holds_alternative<GenericRuntimeCheckpoint>(value);
        info->struct_size = sizeof(*info);
        info->struct_version = 1U;
        info->wire_version = WISTERIA_CHECKPOINT_WIRE_VERSION;
        info->payload_schema = isGeneric
            ? WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_GENERIC_R18
            : WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_MMD_R12C;
        info->payload_kind = StableCheckpointPayloadKind(value);
        info->reserved = 0U;
        info->build_compatibility_id = CurrentBuildCompatibilityId();
        info->payload_size =
            static_cast<std::uint64_t>(
                wire.size() - CheckpointWireHeaderSize
            );
        info->frame = StableCheckpointFrame(value);
        info->physics_tick = isGeneric
            ? 0U
            : std::get<FrameCheckpoint>(value).physics.physicsTick;
        for (std::uint32_t& reserved : info->reserved2)
            reserved = 0U;
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_checkpoint_serialize(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint,
    std::uint8_t* bytes,
    std::uint64_t* in_out_size
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (in_out_size == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "in_out_size must not be null"
            );
        }
        const auto iterator = ctx.stable->checkpoints.find(checkpoint);
        if (iterator == ctx.stable->checkpoints.end())
        {
            return StableNotFound(
                &ctx,
                "unknown stable checkpoint handle"
            );
        }
        if (!StableAssetIdentityValid(iterator->second))
        {
            TrySetError(
                &ctx,
                "asset identity is invalid; checkpoint serialize rejected"
            );
            return WISTERIA_STATUS_INVALID_STATE;
        }
        const std::vector<std::uint8_t> wire =
            StableSerializeCheckpoint(iterator->second);
        if (bytes == nullptr)
        {
            // Size-query mode: report the required byte count.
            *in_out_size = static_cast<std::uint64_t>(wire.size());
            return WISTERIA_STATUS_OK;
        }
        if (*in_out_size < wire.size())
        {
            *in_out_size = static_cast<std::uint64_t>(wire.size());
            return StableInvalidArgument(
                &ctx,
                "serialize buffer is too small"
            );
        }
        std::memcpy(bytes, wire.data(), wire.size());
        *in_out_size = static_cast<std::uint64_t>(wire.size());
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_checkpoint_deserialize(
    WisteriaStableContext context,
    const std::uint8_t* bytes,
    std::uint64_t size,
    WisteriaCheckpoint* out_checkpoint
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (bytes == nullptr || out_checkpoint == nullptr)
        {
            return StableInvalidArgument(
                &ctx,
                "bytes and out_checkpoint must not be null"
            );
        }
        const std::uint32_t payloadKind =
            StableWirePayloadKind(bytes, size);
        if (payloadKind == WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18)
        {
            GenericRuntimeCheckpoint decoded;
            const TimelineStatus status =
                DeserializeGenericCheckpoint(
                    bytes,
                    static_cast<std::size_t>(size),
                    {},
                    decoded
                );
            if (status != TimelineStatus::Ok)
                return MapTimelineStatus(status);
            if (decoded.frame > kGenericExactFrameLimit)
            {
                TrySetError(
                    &ctx,
                    "wire checkpoint frame exceeds the generic exact domain"
                );
                return WISTERIA_STATUS_INVALID_CHECKPOINT;
            }
            const WisteriaCheckpoint handle =
                static_cast<WisteriaCheckpoint>(
                    AllocateOpaqueHandle()
                );
            ctx.stable->checkpoints.emplace(
                handle,
                std::move(decoded)
            );
            *out_checkpoint = handle;
            return WISTERIA_STATUS_OK;
        }
        if (payloadKind == WISTERIA_CHECKPOINT_PAYLOAD_KIND_MMD_R12C)
        {
            FrameCheckpoint decoded;
            const TimelineStatus status = DeserializeCheckpoint(
                bytes,
                static_cast<std::size_t>(size),
                {},
                decoded
            );
            if (status != TimelineStatus::Ok)
                return MapTimelineStatus(status);
            if (!StableFrameWithinExactDomain(decoded.frame))
            {
                TrySetError(
                    &ctx,
                    "wire checkpoint frame exceeds the MMD exact domain"
                );
                return WISTERIA_STATUS_INVALID_CHECKPOINT;
            }
            const WisteriaCheckpoint handle =
                static_cast<WisteriaCheckpoint>(
                    AllocateOpaqueHandle()
                );
            ctx.stable->checkpoints.emplace(
                handle,
                std::move(decoded)
            );
            *out_checkpoint = handle;
            return WISTERIA_STATUS_OK;
        }
        return StableInvalidArgument(
            &ctx,
            "unsupported checkpoint wire payload kind"
        );
    });
}

const char* wisteria_stable_last_error(
    WisteriaStableContext context
)
{
    const char* message = "unknown context";
    const enum WisteriaStatus status = InvokeAbi(
        context,
        [&](Context& ctx)
        {
            message = ctx.lastError;
            return WISTERIA_OK;
        }
    );
    return status == WISTERIA_OK ? message : "unknown context";
}
}  // extern "C"
