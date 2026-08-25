#pragma once

// WISTERIA product/SDK version.
//
// The product version advances with every release. It is independent from
// the C ABI versions declared in:
//   wisteria/native/wisteria_stable_runtime.h  (runtime ABI v1)
//   wisteria/native/wisteria_stable_render.h   (render ABI v1)
// C ABI versions only change additively within a major version.

#define WISTERIA_VERSION_MAJOR 1
#define WISTERIA_VERSION_MINOR 2
#define WISTERIA_VERSION_PATCH 0

#define WISTERIA_VERSION_STRING "1.2.0"
