#pragma once

namespace wisteria
{
// R1.7 Phase 0C: opaque identity of one OpenGL share group.
//
// A share group is the unit of GPU-object ownership, not a single native
// context handle. Multiple native contexts (GLFWwindow / EGLContext / future
// WGL contexts) that share resources must map to the same token. The token
// is intentionally not a native context handle: the platform layer owns the
// native-context -> share-group mapping.
using GraphicsShareGroupToken = const void*;
}  // namespace wisteria
