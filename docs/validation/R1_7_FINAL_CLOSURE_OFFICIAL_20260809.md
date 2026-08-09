# R1.7 Final Closure — True Headless Context Provider

**Status: FROZEN / IMPLEMENTED / VALIDATED / 0A–0D CLOSED;
0E FINAL CLOSURE PENDING (native-Linux hardware release gate)**

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
Windows regression (MSVC RelWithDebInfo):
  CORE 8/8 PASS
  FULL (full assets) 9/9 PASS

WSL compatibility (GCC, LIBGL_ALWAYS_SOFTWARE=1 -> llvmpipe):
  CORE 9/9 PASS (includes headless-smoke)
  FULL (full assets) 10/10 PASS

Native Linux hardware release gate (real Linux, hardware EGL):
  PENDING — at minimum headless-smoke (session + sequence probes);
  CORE/FULL recommended.
  Run: ./script/verify_r17_native_linux.sh [--core|--full]
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
0E  Matrices and closure          PENDING (native-Linux hardware gate)
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

R1.7 phases 0A–0D are closed. 0E becomes CLOSED after the native-Linux
hardware release gate passes on a real machine. Future work must consume
the frozen ownership model, factory invariants and fail-stop teardown
semantics rather than reopen them without concrete counterevidence.

## 7. Closure Fix (final review)

```text
1. HeadlessRenderSession destructor is fail-stop: if MakeCurrent() fails
   during teardown the process terminates before any renderer.Release() /
   ReleaseAll() glDelete* call.
2. The native-Linux hardware baseline is verified with
   script/verify_r17_native_linux.sh on a real Linux machine (no
   LIBGL_ALWAYS_SOFTWARE); the smoke must report software=no and pass
   both session and sequence probes.
```
