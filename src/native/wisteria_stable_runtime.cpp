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
#include "wisteria/runtime/checkpoint_serialization.hpp"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/runtime/model_backend.hpp"
#include "wisteria/runtime/runtime_creation_options.hpp"

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

StableEntityEntry* FindStableEntity(
    Context& context,
    WisteriaEntity handle
) noexcept
{
    const auto iterator = context.stable->entities.find(handle);
    return iterator == context.stable->entities.end()
        ? nullptr
        : iterator->second.get();
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
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(entry->runtime.get());
    if (mmd == nullptr)
    {
        TrySetError(
            &context,
            "stable v1 requires an MMD-compatible runtime"
        );
    }
    return mmd;
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

        ModelAsset asset(model_path_utf8);
        ModelSourceDescriptor descriptor;
        descriptor.sourcePath = modelPath;
        descriptor.backend = ModelBackendKind::SabaMmd;
        asset.SetSourceDescriptor(descriptor);

        std::unique_ptr<IModelRuntimeDriver> runtime;
        try
        {
            runtime = ctx.stable->backends.CreateRuntime(
                asset,
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

        auto entry = std::make_unique<StableEntityEntry>();
        entry->runtime = std::move(runtime);
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
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;

        const ModelRuntimeCapabilities core =
            mmd->Capabilities();
        capabilities->struct_size = sizeof(*capabilities);
        capabilities->struct_version = 1U;
        capabilities->capability_flags =
            WISTERIA_CAP_SUPPORTS_DETERMINISTIC_EXACT_FRAME;
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
        if (core.checkpoint.supportsCheckpointCapture)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_CHECKPOINT_CAPTURE |
                WISTERIA_CAP_SUPPORTS_CHECKPOINT_SERIALIZATION;
        }
        if (core.checkpoint.supportsCheckpointRestore)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_CHECKPOINT_RESTORE;
        }
        if (core.checkpoint.supportsReplayFromCheckpoint)
        {
            capabilities->capability_flags |=
                WISTERIA_CAP_SUPPORTS_REPLAY_FROM_CHECKPOINT;
        }
        capabilities->runtime_backend_id = WISTERIA_BACKEND_ID_SABA_MMD;
        capabilities->runtime_backend_version = 1U;
        capabilities->deterministic_profile_id =
            WISTERIA_DETERMINISTIC_PROFILE_COLD_STEP_V1;
        capabilities->reserved = 0U;
        capabilities->structural_frame_limit = kStructuralFrameLimit;
        capabilities->max_deterministic_motion_frame =
            kSabaExactFrameLimit;
        for (std::uint32_t& reserved : capabilities->reserved2)
            reserved = 0U;
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
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        // The frozen deterministic profile forbids looping; entering the
        // deterministic timeline disables it before any boundary is built
        // (SetMotionLooping invalidates the current boundary, so it must
        // run before PrepareFrameZero, not at capture time).
        mmd->SetMotionLooping(false);
        auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(mmd);
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
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(mmd);
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
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        mmd->SetMotionLooping(false);
        return MapTimelineStatus(
            mmd->EvaluateTick(target, SeekPolicy::ReplayFromStart, {})
        );
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
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        if (mmd->IsMotionLooping())
            return WISTERIA_STATUS_INVALID_STATE;
        FrameCheckpoint checkpoint;
        const std::uint32_t status =
            MapTimelineStatus(mmd->CreateCheckpoint(checkpoint));
        if (status != WISTERIA_STATUS_OK)
            return status;
        const WisteriaCheckpoint handle =
            static_cast<WisteriaCheckpoint>(AllocateOpaqueHandle());
        ctx.stable->checkpoints.emplace(handle, std::move(checkpoint));
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
        MmdRuntimeModel* mmd = RequireStableMmd(ctx, entity);
        if (mmd == nullptr)
            return WISTERIA_STATUS_NOT_FOUND;
        const FrameCheckpoint& value = checkpointIterator->second;
        return MapTimelineStatus(
            mmd->ReplayFromCheckpoint(value, value.frame)
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
        const auto iterator = ctx.stable->checkpoints.find(checkpoint);
        if (iterator == ctx.stable->checkpoints.end())
        {
            return StableNotFound(
                &ctx,
                "unknown stable checkpoint handle"
            );
        }
        const FrameCheckpoint& value = iterator->second;
        const std::vector<std::uint8_t> wire =
            SerializeCheckpoint(value);
        info->struct_size = sizeof(*info);
        info->struct_version = 1U;
        info->wire_version = WISTERIA_CHECKPOINT_WIRE_VERSION;
        info->payload_schema = WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_MMD_R12C;
        info->payload_kind = WISTERIA_CHECKPOINT_PAYLOAD_KIND_MMD_R12C;
        info->reserved = 0U;
        info->build_compatibility_id = CurrentBuildCompatibilityId();
        info->payload_size =
            static_cast<std::uint64_t>(wire.size());
        info->frame = value.frame;
        info->physics_tick = value.physics.physicsTick;
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
        const std::vector<std::uint8_t> wire =
            SerializeCheckpoint(iterator->second);
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
        FrameCheckpoint decoded;
        const TimelineStatus status = DeserializeCheckpoint(
            bytes,
            static_cast<std::size_t>(size),
            {},
            decoded
        );
        if (status != TimelineStatus::Ok)
            return MapTimelineStatus(status);
        const WisteriaCheckpoint handle =
            static_cast<WisteriaCheckpoint>(AllocateOpaqueHandle());
        ctx.stable->checkpoints.emplace(handle, std::move(decoded));
        *out_checkpoint = handle;
        return WISTERIA_STATUS_OK;
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
