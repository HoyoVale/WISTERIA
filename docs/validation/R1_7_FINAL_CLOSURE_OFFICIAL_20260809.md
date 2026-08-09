# R1.7 Final Closure — True Headless Context Provider

**Status: FROZEN / IMPLEMENTED / VALIDATED / CLOSED**

R1.7 establishes a window-system-free OpenGL context provider owned by
WISTERIA, plus the engine-owned composition root that lets the complete
offline pipeline (RenderOffline and OfflineFrameSequence) run with no
window, no present, no swap buffers, and no desktop environment.

## 1. Final Architecture

```text
CreateHeadlessContext(options)
  ├─ EGL surfaceless (Mesa)        primary
  ├─ EGL device-hardware           fallback
  └─ EGL device-software           forceSoftware / last resort
        ↓
   IHeadlessContext
  (ContextToken + ShareGroupToken)
        ↓
HeadlessRenderSession
  ├─ GraphicsDevice
  ├─ ResourceManager
  └─ Renderer
        ↓
RenderOffline / OfflineFrameSequence
        ↓
RGBA8 / PNG / manifest / checkpoint
```

## 2. Frozen Invariants

```text
1. CreateHeadlessContext returns with no native context current and no
   tracker registered.
2. MakeCurrent is the single lifecycle transaction:
   native current → ContextToken → ShareGroupToken → FlushPendingDeletes.
3. Shared objects (Texture/Buffer/Renderbuffer) are governed by the
   ShareGroupToken; context-local objects (VertexArray/Framebuffer) are
   governed by the owning ContextToken. A sibling context in the same
   share group must never flush a context-local queue entry.
4. Destroying the current native context clears both trackers.
5. forceSoftware verifies GL_RENDERER (llvmpipe/softpipe/swrast);
   D3D12 always fails the strict gate.
6. Ownership validation failure during teardown is fail-stop.
```

## 3. Validation Matrix (2026-08-09)

```text
Windows CORE (MSVC RelWithDebInfo)             8/8  PASS
Windows FULL (MSVC + full assets)              9/9  PASS
Linux CORE (GCC RelWithDebInfo, WSL)           9/9  PASS
Linux FULL (GCC + full assets, WSL)           10/10 PASS
```

Linux runs in WSL use `LIBGL_ALWAYS_SOFTWARE=1` (llvmpipe), matching the
recorded compatibility policy: WSL Mesa D3D12 is unreliable and requires
software fallback; native Linux hardware is the release baseline. The
headless-smoke test (EGL lifecycle + FBO + session + sequence) is part of
the Linux matrices only; Windows keeps the GLFW hidden-window regression
baseline.

## 4. Closed Phases

```text
0A  Contract and decisions        CLOSED
0B  EGL provider                  CLOSED
0C  Dual ownership model          CLOSED
0D  HeadlessRenderSession         CLOSED
0E  Matrices and closure          CLOSED
```

## 5. Frozen Boundaries

```text
Windows WGL PBuffer / ANGLE
OSMesa provider
Stable Render C Portal
Application zero-window mode
Multi-threaded rendering
Video encoding / FFmpeg / Audio
Cross-platform pixel identity
```

## 6. Next Steps

```text
R1.8  Generic Deterministic Runtime
R1.9  Stable Runtime / Render C ABI
R2.x  RenderDevice / RenderTarget / RenderGraph / multi-backend
```

R1.7 is closed. Future work must consume the frozen ownership model and
factory invariants rather than reopen them without concrete counterevidence.

