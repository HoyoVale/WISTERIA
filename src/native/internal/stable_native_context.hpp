#pragma once

#include "native_context.hpp"
#include "wisteria/native/wisteria_stable_runtime.h"
#include "wisteria/runtime/checkpoint.hpp"
#include "wisteria/runtime/model_backend.hpp"

#include <memory>
#include <unordered_map>

namespace wisteria::native
{
struct StableEntityEntry
{
    // The Saba adapter stores a pointer to the ModelAsset inside the
    // runtime (SetAsset). The entry must own the asset so the pointer never
    // dangles after entity_create returns. Declared first so it is destroyed
    // LAST (C++ destroys members in reverse declaration order): the runtime
    // destructor runs before the asset destructor.
    std::unique_ptr<wisteria::ModelAsset> asset;
    std::unique_ptr<wisteria::IModelRuntimeDriver> runtime;
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

    ModelBackendRegistry backends;
    std::unordered_map<
        WisteriaEntity,
        std::unique_ptr<StableEntityEntry>
    > entities;
    std::unordered_map<
        WisteriaCheckpoint,
        wisteria::FrameCheckpoint
    > checkpoints;
};
}  // namespace wisteria::native
