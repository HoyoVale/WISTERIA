#pragma once

namespace wisteria
{
// R1.7 Final Fix: dual-context ownership model.
//
// OpenGL has two ownership domains:
//   - Shared objects (buffer / texture / renderbuffer / shader / program)
//     belong to a share group and may be used and deleted from any context
//     of that group.
//   - Context-local objects (vertex array / framebuffer / transform
//     feedback / query / program pipeline) live in one context's namespace
//     and must be deleted from that exact context.
// The platform layer maps every native context to both identities.

// Opaque identity of one native GL context (e.g. a GLFWwindow* or an EGL
// context handle). Two different contexts always have different tokens.
using GraphicsContextToken = const void*;

// Opaque identity of one OpenGL share group. Multiple native contexts that
// share resources map to the same token. The token is intentionally not a
// native context handle: the platform layer owns the
// native-context -> share-group mapping.
using GraphicsShareGroupToken = const void*;
}  // namespace wisteria
