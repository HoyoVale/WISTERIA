#pragma once

// WISTERIA Stable C ABI SDK, C++ convenience layer.
//
// These headers are header-only RAII wrappers over the frozen stable C ABI.
// They do not expose third-party types and do not introduce an additional
// binary ABI: compatibility continues to be governed by
// wisteria_stable_runtime.h / wisteria_stable_render.h.

#include "wisteria/sdk/checkpoint.hpp"
#include "wisteria/sdk/context.hpp"
#include "wisteria/sdk/entity.hpp"
#include "wisteria/sdk/render_session.hpp"
#include "wisteria/sdk/status.hpp"
