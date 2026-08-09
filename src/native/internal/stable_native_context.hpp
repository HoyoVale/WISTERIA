#pragma once

#include "native_context.hpp"
#include "wisteria/native/wisteria_stable_runtime.h"
#include "wisteria/native/wisteria_stable_render.h"
#include "wisteria/runtime/checkpoint.hpp"
#include "wisteria/runtime/generic_checkpoint.hpp"
#include "wisteria/runtime/model_backend.hpp"
#include "wisteria/runtime/model_instance.hpp"
#include "wisteria/rendering/headless_render_session.hpp"
#include "wisteria/rendering/material.hpp"
#include "wisteria/rendering/mesh.hpp"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <variant>

#include <cstdio>
#include <exception>

namespace wisteria::native
{
struct StableEntityEntry
{
    // RenderPart references inside ModelAsset point at these mesh/material
    // objects, so they are declared FIRST and destroyed LAST (C++ destroys
    // members in reverse declaration order).
    std::vector<std::unique_ptr<wisteria::Mesh>> meshes;
    std::vector<std::unique_ptr<wisteria::Material>> materials;
    // The Saba adapter stores a pointer to the ModelAsset inside the
    // runtime (SetAsset). The entry must own the asset so the pointer never
    // dangles after entity_create returns.
    std::unique_ptr<wisteria::ModelAsset> asset;
    // R1.9 Phase 0D: the entry owns a ModelInstance so the stable render
    // session can temporarily adopt it into a Scene (borrow -> render ->
    // return) without losing the exact runtime state.
    std::unique_ptr<wisteria::ModelInstance> modelInstance;
    // R1.9 Final Fix: the first render session that adopts this entity is
    // its exclusive GPU owner. Attached GPU objects may only be released
    // while that session's context is current.
    std::optional<WisteriaRenderSession> ownerRenderSession;
};

// R1.4 Phase 0B: Stable C ABI v1 context-owned state (contract §4). The
// Context registry is shared with the legacy v0.7 facade; only the stable
// maps below belong to the frozen v1 surface.
struct StableContextState
{
    StableContextState()
    {
        wisteria::RegisterDefaultModelBackends(backends);
    }

    ~StableContextState()
    {
        // R1.9 Final Fix: destroy in GPU-safe order. renderSessions is
        // declared before entities so entities are destroyed first; before
        // that happens, make the owning session's context current so
        // attached meshes/materials release through a valid context.
        for (auto& [handle, entry] : this->entities)
        {
            if (entry == nullptr ||
                !entry->ownerRenderSession.has_value())
            {
                continue;
            }
            const auto session = this->renderSessions.find(
                *entry->ownerRenderSession
            );
            if (session == this->renderSessions.end() ||
                session->second == nullptr ||
                session->second->session == nullptr)
            {
                continue;
            }
            try
            {
                session->second->session->MakeCurrent();
            }
            catch (const std::exception&)
            {
                // R1.9 Final Micro Fix: fail-stop, matching the frozen
                // HeadlessRenderSession teardown rule. Without the owning
                // context current, attached stable meshes/materials would
                // call glDelete* with no valid context.
                std::fprintf(
                    stderr,
                    "[stable] FATAL: render session MakeCurrent failed "
                    "during context teardown; aborting without GL teardown\n"
                );
                std::terminate();
            }
        }
    }

    ModelBackendRegistry backends;
    // R1.9 Phase 0D: stable render sessions (engine HeadlessRenderSession)
    // with the last sequence cursor observed through the stable surface.
    struct RenderSessionEntry
    {
        std::unique_ptr<wisteria::HeadlessRenderSession> session;
        std::optional<std::uint64_t> lastCommittedFrame;
        bool sequenceFailed = false;
    };
    std::unordered_map<
        WisteriaRenderSession,
        std::unique_ptr<RenderSessionEntry>
    > renderSessions;
    std::unordered_map<
        WisteriaEntity,
        std::unique_ptr<StableEntityEntry>
    > entities;
    std::unordered_map<
        WisteriaCheckpoint,
        std::variant<
            wisteria::FrameCheckpoint,
            wisteria::GenericRuntimeCheckpoint
        >
    > checkpoints;
};

inline StableEntityEntry* FindStableEntity(
    Context& context,
    WisteriaEntity handle
) noexcept
{
    const auto iterator = context.stable->entities.find(handle);
    return iterator == context.stable->entities.end()
        ? nullptr
        : iterator->second.get();
}
}  // namespace wisteria::native
