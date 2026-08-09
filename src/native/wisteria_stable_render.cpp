/*
 * R1.9 Phase 0D: Stable Render C ABI implementation.
 *
 * Same library, same Context/error/handle conventions as the stable runtime
 * surface. The render session wraps the engine HeadlessRenderSession
 * (provider: EGL on Linux, GLFW hidden window elsewhere/fallback) and
 * renders the EXACT runtime state of a stable entity by temporarily adopting
 * its ModelInstance into a Scene (borrow -> render -> return).
 */

#include "wisteria/native/wisteria_stable_render.h"

#include "internal/native_context.hpp"
#include "internal/stable_native_context.hpp"
#include "wisteria/platform/headless_context.hpp"
#include "wisteria/rendering/headless_render_session.hpp"
#include "wisteria/rendering/offline_render.hpp"
#include "wisteria/scene/entity.hpp"
#include "wisteria/scene/offline_frame_sequence.hpp"
#include "wisteria/scene/scene.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

using namespace wisteria;
using namespace wisteria::native;

namespace
{
std::uint32_t StableInvalidArgumentRender(
    Context* context,
    const char* message
)
{
    TrySetError(context, message);
    return WISTERIA_STATUS_INVALID_ARGUMENT;
}

std::uint32_t StableNotFoundRender(
    Context* context,
    const char* message
)
{
    TrySetError(context, message);
    return WISTERIA_STATUS_NOT_FOUND;
}

bool ValidateSessionOptions(
    const WisteriaRenderSessionOptionsV1* options
) noexcept
{
    if (options == nullptr ||
        options->struct_version != 1U ||
        options->struct_size < sizeof(*options))
    {
        return false;
    }
    if (options->force_software != 0U && options->force_software != 1U)
        return false;
    for (std::uint32_t reserved : options->reserved)
    {
        if (reserved != 0U)
            return false;
    }
    return true;
}

bool ValidateCamera(const WisteriaRenderCameraV1* camera) noexcept
{
    if (camera == nullptr ||
        camera->struct_version != 1U ||
        camera->struct_size < sizeof(*camera))
    {
        return false;
    }
    for (std::uint32_t reserved : camera->reserved)
    {
        if (reserved != 0U)
            return false;
    }
    if (!std::isfinite(camera->vertical_fov_degrees) ||
        camera->vertical_fov_degrees <= 0.0f ||
        camera->vertical_fov_degrees >= 180.0f ||
        !std::isfinite(camera->near_clip) ||
        !std::isfinite(camera->far_clip) ||
        camera->near_clip <= 0.0f ||
        camera->far_clip <= camera->near_clip)
    {
        return false;
    }
    for (std::size_t index = 0U; index < 3U; ++index)
    {
        if (!std::isfinite(camera->position[index]) ||
            !std::isfinite(camera->target[index]) ||
            !std::isfinite(camera->up[index]))
        {
            return false;
        }
    }
    return true;
}

bool ValidateSequenceOptions(
    const WisteriaSequenceOptionsV1* options
) noexcept
{
    if (options == nullptr ||
        options->struct_version != 1U ||
        options->struct_size < sizeof(*options))
    {
        return false;
    }
    if (options->width == 0U || options->height == 0U ||
        options->overwrite_policy > 2U ||
        (options->write_png != 0U && options->write_png != 1U) ||
        (options->write_raw != 0U && options->write_raw != 1U) ||
        (options->write_png == 0U && options->write_raw == 0U))
    {
        return false;
    }
    for (std::uint32_t reserved : options->reserved)
    {
        if (reserved != 0U)
            return false;
    }
    return true;
}

OfflineRenderRequest MakeRenderRequest(
    const WisteriaRenderCameraV1* camera,
    std::uint32_t width,
    std::uint32_t height
)
{
    OfflineRenderRequest request;
    request.width = width;
    request.height = height;
    request.camera = Camera(CameraParam{
        .Position = glm::vec3(
            camera->position[0],
            camera->position[1],
            camera->position[2]
        ),
        .Target = glm::vec3(
            camera->target[0],
            camera->target[1],
            camera->target[2]
        ),
        .Up = glm::vec3(
            camera->up[0],
            camera->up[1],
            camera->up[2]
        ),
        .VerticalFovDegrees = camera->vertical_fov_degrees,
        .NearClip = camera->near_clip,
        .FarClip = camera->far_clip
    });
    request.projection = glm::perspective(
        glm::radians(camera->vertical_fov_degrees),
        static_cast<float>(width) / static_cast<float>(height),
        camera->near_clip,
        camera->far_clip
    );
    return request;
}

SequenceOverwritePolicy MapOverwritePolicy(std::uint32_t policy) noexcept
{
    switch (policy)
    {
    case 1U:
        return SequenceOverwritePolicy::Overwrite;
    case 2U:
        return SequenceOverwritePolicy::VerifySkip;
    default:
        return SequenceOverwritePolicy::Reject;
    }
}

// Borrows the stable entity's ModelInstance into a temporary Scene for the
// duration of one render/sequence call and returns it on destruction.
class EntityBorrowGuard
{
public:
    EntityBorrowGuard(
        StableEntityEntry& entry,
        Scene& scene,
        Entity& entity
    )
        : entry(entry),
          entity(entity)
    {
    }

    ~EntityBorrowGuard()
    {
        this->entry.modelInstance = this->entity.TakeModelInstance();
    }

    EntityBorrowGuard(const EntityBorrowGuard&) = delete;
    EntityBorrowGuard& operator=(const EntityBorrowGuard&) = delete;

private:
    StableEntityEntry& entry;
    Entity& entity;
};
}  // namespace

extern "C"
{
std::uint32_t wisteria_stable_render_session_create(
    WisteriaStableContext context,
    const WisteriaRenderSessionOptionsV1* options,
    WisteriaRenderSession* out_session
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (!ValidateSessionOptions(options) || out_session == nullptr)
        {
            return StableInvalidArgumentRender(
                &ctx,
                "invalid render session options or out_session"
            );
        }
        if (!ctx.stable->renderSessions.empty())
        {
            TrySetError(
                &ctx,
                "R1.9 v1 allows at most one active render session per "
                "stable context"
            );
            return WISTERIA_STATUS_ALREADY_EXISTS;
        }
        HeadlessContextOptions headlessOptions;
        headlessOptions.forceSoftware = options->force_software != 0U;
        auto provider = CreateHeadlessContext(headlessOptions);
        if (provider == nullptr)
        {
            TrySetError(
                &ctx,
                "no headless context provider available"
            );
            return WISTERIA_STATUS_INITIALIZATION;
        }
        auto entry = std::make_unique<StableContextState::RenderSessionEntry>();
        entry->session = std::make_unique<HeadlessRenderSession>(
            std::move(provider)
        );
        const WisteriaRenderSession handle =
            static_cast<WisteriaRenderSession>(AllocateOpaqueHandle());
        ctx.stable->renderSessions.emplace(handle, std::move(entry));
        *out_session = handle;
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_render_session_destroy(
    WisteriaStableContext context,
    WisteriaRenderSession session
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        const auto sessionIterator =
            ctx.stable->renderSessions.find(session);
        if (sessionIterator == ctx.stable->renderSessions.end())
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable render session handle"
            );
        }
        // R1.9 Final Fix: a session with bound rendered entities cannot be
        // destroyed without orphaning their GPU objects.
        for (const auto& [entityHandle, entry] : ctx.stable->entities)
        {
            if (entry != nullptr &&
                entry->ownerRenderSession == session)
            {
                TrySetError(
                    &ctx,
                    "render session has bound rendered entities; destroy "
                    "entities first"
                );
                return WISTERIA_STATUS_INVALID_STATE;
            }
        }
        if (ctx.stable->renderSessions.erase(session) == 0U)
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable render session handle"
            );
        }
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_render_session_render(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    WisteriaEntity entity,
    const WisteriaRenderCameraV1* camera,
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t* rgba,
    std::uint64_t* in_out_size
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (in_out_size == nullptr || !ValidateCamera(camera))
        {
            return StableInvalidArgumentRender(
                &ctx,
                "invalid camera or in_out_size"
            );
        }
        if (width == 0U || height == 0U ||
            width > std::numeric_limits<std::uint32_t>::max() / height ||
            static_cast<std::uint64_t>(width) *
                    static_cast<std::uint64_t>(height) >
                std::numeric_limits<std::uint64_t>::max() / 4U)
        {
            return StableInvalidArgumentRender(
                &ctx,
                "invalid render dimensions"
            );
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height) * 4U;
        // Handle validation comes before the size query so a garbage
        // session/entity never observes STATUS_OK (status is authoritative).
        const auto sessionIterator =
            ctx.stable->renderSessions.find(session);
        if (sessionIterator == ctx.stable->renderSessions.end())
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable render session handle"
            );
        }
        StableEntityEntry* entry = FindStableEntity(ctx, entity);
        if (entry == nullptr || entry->modelInstance == nullptr)
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable entity handle"
            );
        }
        if (rgba == nullptr)
        {
            *in_out_size = required;
            return WISTERIA_STATUS_OK;
        }
        if (*in_out_size < required)
        {
            *in_out_size = required;
            return StableInvalidArgumentRender(
                &ctx,
                "render buffer is too small"
            );
        }
        if (entry->ownerRenderSession.has_value() &&
            *entry->ownerRenderSession != session)
        {
            TrySetError(
                &ctx,
                "entity is bound to a different render session"
            );
            return WISTERIA_STATUS_INVALID_STATE;
        }
        entry->ownerRenderSession = session;

        auto scene = std::make_unique<Scene>();
        scene->CreateDirectionalLight(DirectionalLightData{
            .Direction = {-0.35f, -0.75f, -0.45f},
            .Color = glm::vec3(1.0f, 0.96f, 0.92f),
            .Intensity = 1.0f
        });
        Entity& renderEntity = scene->CreateEntity();
        renderEntity.SetModelInstance(std::move(entry->modelInstance));
        Scene::BindModelInstanceParts(
            renderEntity,
            renderEntity.GetModelInstance()
        );
        EntityBorrowGuard guard(*entry, *scene, renderEntity);
        ModelInstance& instance = renderEntity.GetModelInstance();
        // R1.9 Final Micro Fix: exact stepping changed the runtime state
        // without touching the render cache; publish before rendering.
        if (instance.TryGetRuntime() != nullptr)
            instance.PublishCurrentRuntimeFrame();

        const Rgba8Frame frame =
            sessionIterator->second->session->RenderOffline(
                *scene,
                MakeRenderRequest(camera, width, height)
            );
        if (frame.pixels.size() != required)
        {
            return WISTERIA_STATUS_INTERNAL;
        }
        std::memcpy(rgba, frame.pixels.data(), required);
        *in_out_size = required;
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_render_session_sequence_range(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    WisteriaEntity entity,
    const WisteriaRenderCameraV1* camera,
    const char* output_dir_utf8,
    const WisteriaSequenceOptionsV1* options,
    std::uint64_t* out_last_committed
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (!ValidateSequenceOptions(options) ||
            !ValidateCamera(camera) ||
            output_dir_utf8 == nullptr ||
            out_last_committed == nullptr)
        {
            return StableInvalidArgumentRender(
                &ctx,
                "invalid sequence arguments"
            );
        }
        if (options->start_frame > options->end_frame)
        {
            return StableInvalidArgumentRender(
                &ctx,
                "sequence start must not exceed end"
            );
        }
        const auto sessionIterator =
            ctx.stable->renderSessions.find(session);
        if (sessionIterator == ctx.stable->renderSessions.end())
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable render session handle"
            );
        }
        StableEntityEntry* entry = FindStableEntity(ctx, entity);
        if (entry == nullptr || entry->modelInstance == nullptr)
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable entity handle"
            );
        }

        auto scene = std::make_unique<Scene>();
        scene->CreateDirectionalLight(DirectionalLightData{
            .Direction = {-0.35f, -0.75f, -0.45f},
            .Color = glm::vec3(1.0f, 0.96f, 0.92f),
            .Intensity = 1.0f
        });
        Entity& renderEntity = scene->CreateEntity();
        renderEntity.SetModelInstance(std::move(entry->modelInstance));
        Scene::BindModelInstanceParts(
            renderEntity,
            renderEntity.GetModelInstance()
        );
        EntityBorrowGuard guard(*entry, *scene, renderEntity);
        ModelInstance& instance = renderEntity.GetModelInstance();
        IModelRuntimeDriver* runtime = instance.TryGetRuntime();
        if (runtime == nullptr)
            return WISTERIA_STATUS_INVALID_STATE;
        const ModelRuntimeCapabilities capabilities =
            runtime->Capabilities();
        if (!capabilities.deterministic.supportsExactFrameStepping ||
            !capabilities.deterministic.supportsCheckpointCapture ||
            !capabilities.deterministic.supportsCheckpointRestore ||
            !capabilities.deterministic.supportsReplayFromCheckpoint)
        {
            TrySetError(
                &ctx,
                "stable sequence requires the full deterministic surface"
            );
            return WISTERIA_STATUS_UNSUPPORTED;
        }
        if (entry->ownerRenderSession.has_value() &&
            *entry->ownerRenderSession != session)
        {
            TrySetError(
                &ctx,
                "entity is bound to a different render session"
            );
            return WISTERIA_STATUS_INVALID_STATE;
        }
        entry->ownerRenderSession = session;

        OfflineFrameSequenceConfig config;
        config.outputDirectory = PathFromUtf8(output_dir_utf8);
        config.renderRequest =
            MakeRenderRequest(camera, options->width, options->height);
        config.overwritePolicy = MapOverwritePolicy(
            options->overwrite_policy
        );
        config.writePng = options->write_png != 0U;
        config.writeRaw = options->write_raw != 0U;

        HeadlessRenderSession* renderSession =
            sessionIterator->second->session.get();
        OfflineFrameSequence sequence(
            *scene,
            renderSession->GetRenderer(),
            *runtime,
            instance,
            config
        );
        renderSession->MakeCurrent();
        try
        {
            sequence.RenderRange(
                options->start_frame,
                options->end_frame
            );
        }
        catch (...)
        {
            sessionIterator->second->lastCommittedFrame =
                sequence.LastCommittedFrame();
            sessionIterator->second->sequenceFailed = sequence.Failed();
            throw;
        }
        sessionIterator->second->lastCommittedFrame =
            sequence.LastCommittedFrame();
        sessionIterator->second->sequenceFailed = sequence.Failed();
        *out_last_committed =
            sessionIterator->second->lastCommittedFrame.value_or(0U);
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_render_session_sequence_resume(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    WisteriaEntity entity,
    const WisteriaRenderCameraV1* camera,
    const char* output_dir_utf8,
    const WisteriaSequenceOptionsV1* options,
    std::uint64_t* out_last_committed
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (!ValidateSequenceOptions(options) ||
            !ValidateCamera(camera) ||
            output_dir_utf8 == nullptr ||
            out_last_committed == nullptr)
        {
            return StableInvalidArgumentRender(
                &ctx,
                "invalid sequence arguments"
            );
        }
        const auto sessionIterator =
            ctx.stable->renderSessions.find(session);
        if (sessionIterator == ctx.stable->renderSessions.end())
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable render session handle"
            );
        }
        StableEntityEntry* entry = FindStableEntity(ctx, entity);
        if (entry == nullptr || entry->modelInstance == nullptr)
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable entity handle"
            );
        }

        auto scene = std::make_unique<Scene>();
        scene->CreateDirectionalLight(DirectionalLightData{
            .Direction = {-0.35f, -0.75f, -0.45f},
            .Color = glm::vec3(1.0f, 0.96f, 0.92f),
            .Intensity = 1.0f
        });
        Entity& renderEntity = scene->CreateEntity();
        renderEntity.SetModelInstance(std::move(entry->modelInstance));
        Scene::BindModelInstanceParts(
            renderEntity,
            renderEntity.GetModelInstance()
        );
        EntityBorrowGuard guard(*entry, *scene, renderEntity);
        ModelInstance& instance = renderEntity.GetModelInstance();
        IModelRuntimeDriver* runtime = instance.TryGetRuntime();
        if (runtime == nullptr)
            return WISTERIA_STATUS_INVALID_STATE;
        const ModelRuntimeCapabilities capabilities =
            runtime->Capabilities();
        if (!capabilities.deterministic.supportsExactFrameStepping ||
            !capabilities.deterministic.supportsCheckpointCapture ||
            !capabilities.deterministic.supportsCheckpointRestore ||
            !capabilities.deterministic.supportsReplayFromCheckpoint)
        {
            TrySetError(
                &ctx,
                "stable sequence requires the full deterministic surface"
            );
            return WISTERIA_STATUS_UNSUPPORTED;
        }
        if (entry->ownerRenderSession.has_value() &&
            *entry->ownerRenderSession != session)
        {
            TrySetError(
                &ctx,
                "entity is bound to a different render session"
            );
            return WISTERIA_STATUS_INVALID_STATE;
        }
        entry->ownerRenderSession = session;

        OfflineFrameSequenceConfig config;
        config.outputDirectory = PathFromUtf8(output_dir_utf8);
        config.renderRequest =
            MakeRenderRequest(camera, options->width, options->height);
        config.overwritePolicy = MapOverwritePolicy(
            options->overwrite_policy
        );
        config.writePng = options->write_png != 0U;
        config.writeRaw = options->write_raw != 0U;

        HeadlessRenderSession* renderSession =
            sessionIterator->second->session.get();
        OfflineFrameSequence sequence(
            *scene,
            renderSession->GetRenderer(),
            *runtime,
            instance,
            config
        );
        renderSession->MakeCurrent();
        try
        {
            sequence.Resume(options->end_frame);
        }
        catch (...)
        {
            sessionIterator->second->lastCommittedFrame =
                sequence.LastCommittedFrame();
            sessionIterator->second->sequenceFailed = sequence.Failed();
            throw;
        }
        sessionIterator->second->lastCommittedFrame =
            sequence.LastCommittedFrame();
        sessionIterator->second->sequenceFailed = sequence.Failed();
        *out_last_committed =
            sessionIterator->second->lastCommittedFrame.value_or(0U);
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_render_session_sequence_last_committed(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    std::uint64_t* out_frame
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (out_frame == nullptr)
        {
            return StableInvalidArgumentRender(
                &ctx,
                "out_frame must not be null"
            );
        }
        const auto sessionIterator =
            ctx.stable->renderSessions.find(session);
        if (sessionIterator == ctx.stable->renderSessions.end())
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable render session handle"
            );
        }
        *out_frame =
            sessionIterator->second->lastCommittedFrame.value_or(0U);
        return WISTERIA_STATUS_OK;
    });
}

std::uint32_t wisteria_stable_render_session_sequence_failed(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    std::int32_t* out_failed
)
{
    return InvokeAbi(context, [&](Context& ctx)
    {
        if (out_failed == nullptr)
        {
            return StableInvalidArgumentRender(
                &ctx,
                "out_failed must not be null"
            );
        }
        const auto sessionIterator =
            ctx.stable->renderSessions.find(session);
        if (sessionIterator == ctx.stable->renderSessions.end())
        {
            return StableNotFoundRender(
                &ctx,
                "unknown stable render session handle"
            );
        }
        *out_failed = sessionIterator->second->sequenceFailed ? 1 : 0;
        return WISTERIA_STATUS_OK;
    });
}
}  // extern "C"
