# R1.6 Final Closure — Deterministic Offline Output Pipeline

**Status: FROZEN / IMPLEMENTED / VALIDATED / CLOSED**

This is the official English closure text as reviewed and stamped by ChatGPT on 2026-08-09.

R1.6 establishes WISTERIA's complete deterministic offline output path, extending the already-frozen runtime and checkpoint foundations through renderer-facing visual state, explicit presentation authority, canonical offscreen pixels, and resumable deterministic frame-sequence persistence.

R1.6 is complete. Phases 0A through 0E are closed.

## 1. Final Architecture

The completed pipeline is:

```text
Model Asset
    ↓
Runtime Backend
    ├─ Saba MMD Runtime
    └─ WISTERIA Generic Runtime
    ↓
ModelInstance
    ↓
ModelRenderFrameView
    ├─ Pose
    ├─ Geometry
    ├─ Dynamic UV
    └─ Evaluated Material State
    ↓
Entity / Scene
    ↓
Explicit Presentation Authority
    ├─ Camera
    ├─ Projection
    └─ Scene Lights
    ↓
Renderer
    ↓
SceneFramebuffer
    ↓
Canonical RGBA8 Readback
    ↓
Deterministic Frame Sequence
    ├─ PNG
    ├─ optional raw RGBA8
    ├─ checkpoint wire state
    └─ append-only manifest
```

The architectural rule remains:

> **Backends execute; WISTERIA governs.**

Renderer remains independent of Saba, VMD timeline control, checkpoints, PNG encoding, manifest persistence, and frame-sequence orchestration.

---

## 2. Phase 0A — Output Contract

**CLOSED**

Phase 0A froze the output boundary and distinguished several concepts that must not be conflated:

```text
Exact Runtime State ≠ Cross-platform Pixel Exactness
Offscreen Rendering ≠ True Headless Rendering
SceneColor ≠ Presented/Post-processed Output
```

R1.6 v1 canonical pixels are:

```text
Renderer::Render
→ SceneFramebuffer SceneColor
→ pre-Present
→ pre-FXAA
→ top-left tightly packed RGBA8
```

The v1 alpha policy is `OpaqueOnly`.

True platform-independent headless context ownership remains outside R1.6.

---

## 3. Phase 0B — Offscreen SceneColor and RGBA8 Readback

**CLOSED**

Phase 0B completed the engine-owned offscreen pixel boundary:

```text
Scene
→ Renderer
→ SceneFramebuffer
→ ReadbackRgba8
→ canonical CPU RGBA8 frame
```

The readback contract freezes:

```text
channels: RGBA
storage: uint8
stride: width * 4
layout: tightly packed
origin: top-left
row order: top → bottom
conversion: GL float → UNORM8
```

The implementation preserves the explicitly tracked WISTERIA OpenGL boundary state and uses exception-safe RAII restoration.

Framebuffer attachment lifetime and queued resource deletion were integrated into `GraphicsDevice`.

---

## 4. Phase 0C — Renderer-Facing Visual State Completeness

**CLOSED**

Phase 0C completed the missing Runtime → Renderer visual-state bridge.

`ModelRenderFrameView` provides a backend-neutral transient view containing:

```text
geometry
dynamic UV
evaluated material overrides
Pose
MorphState
```

Saba MMD now publishes:

```text
dynamic position
dynamic normal
dynamic UV
evaluated PMX material terminal state
```

PMX texture material morph state preserves independent Multiply and Add terms for:

```text
base texture
sphere texture
toon texture
```

Global Saba vertex state is remapped to per-Mesh local vertex domains through the same `SourceVertexIndices` mapping used by geometry.

Material identity is resolved through the stable `MorphMaterialIndex → runtime material slot` relationship rather than mutable Entity RenderPart vector order.

Renderer resolves material state once and uses the same result for both alpha bucketing and drawing.

Renderer contains no Saba- or Generic-specific backend logic.

---

## 5. Phase 0D — Explicit Presentation Authority

**CLOSED**

Phase 0D removed ambiguity around camera and light authority.

The final presentation rule is:

```text
Window render
→ Window/host supplies explicit Camera + Projection

Offline render
→ OfflineRenderRequest supplies explicit Camera + Projection

MMD-driven render
→ orchestration samples frame N
→ applies MMD Camera / DirectionalLight state
→ renders using the explicit host presentation state
```

`Scene::ActiveCamera` remains a legacy/compatibility surface but is not an R1.6 presentation authority.

MMD Runtime provides neutral camera/light samples only.

Renderer:

```text
does not read VMD
does not advance the timeline
does not dynamic_cast to Saba
does not select presentation policy
```

`RenderOffline` provides the window-independent SceneFramebuffer → RGBA8 path and preserves the defined host OpenGL boundary state across the complete Clear / Render / Readback operation.

Orthographic MMD host-camera support remains a documented future capability.

---

## 6. Phase 0E — Deterministic Frame Sequence

**CLOSED**

Phase 0E composes the frozen deterministic runtime and offline rendering boundaries into a persistent frame sequence.

The final per-frame order is:

```text
Prepare / Step exact frame N
→ canonical runtime state N
→ PublishCurrentRuntimeFrame
→ presentation sample N
→ RenderOffline
→ canonical RGBA8
→ RGBA hash
→ encode/write artifacts
→ persist checkpoint
→ durable manifest commit
→ N+1
```

`PublishCurrentRuntimeFrame()` republishes the already-evaluated Runtime state into `ModelInstance` without:

```text
Runtime Update
timeline advancement
root-motion consumption
updateSerial advancement
```

This closes the exact-runtime-state → Renderer cache boundary.

### Deterministic timeline

R1.6 v1 sequence execution uses one deterministic MMD runtime driver.

From-start rendering performs the required sequential pre-roll:

```text
PrepareFrameZero
→ Step 1
→ Step 2
→ ...
→ startFrame
```

No arbitrary jump is substituted for the frozen R1.2 deterministic state machine.

The supported Saba sequence frame domain is bounded to the exact integer range required by the current presentation/runtime path.

### Presentation exactness

Camera and light sampling always uses the same `MotionFrameIndex` as the runtime frame.

Perspective MMD camera samples that are actually applied rebuild the explicit projection using the applied host Camera FOV and output aspect ratio.

No-track and orthographic-fallback cases preserve the host fallback projection.

### Persistent output

Canonical determinism evidence is the RGBA8 byte stream and its FNV-1a 64 fingerprint.

Persistent outputs are:

```text
PNG        standard output
raw RGBA8  optional
checkpoint binary wire
manifest.jsonl
```

PNG encoding is an independent utility and has no Renderer dependency.

### Manifest

The manifest is an append-only JSONL commit log.

The manifest record, not mere artifact existence, is the authority for committed sequence state.

Crash residue outside a complete committed record is treated as uncommitted/orphan state.

A partial JSONL tail is repaired using durable in-place truncation to the final complete newline.

A zero-length manifest resulting from failure before the first session record is treated as a fresh session.

A non-empty malformed manifest fails closed.

### Transaction durability

Artifact writes use:

```text
temporary file
→ durable file flush
→ atomic replacement
→ parent-directory durability
```

Manifest creation/append is durably flushed, including parent-directory synchronization when the manifest is first created.

Durability failures fail the Session rather than being silently accepted.

### Checkpoint A/B invariant

Checkpoints use alternating persistent slots.

The next slot is determined from the latest committed manifest cursor, never from frame parity.

The checkpoint required by the previous committed frame cannot be destroyed before the next manifest commit establishes a new authoritative resume anchor.

Historical Overwrite/VerifySkip does not move the manifest cursor or checkpoint slot.

A new uncommitted frame at or behind the committed cursor cannot be inserted into history.

Overwrite of the current resume anchor requires the regenerated checkpoint wire hash to match the committed checkpoint wire hash.

### Resume

Resume performs:

```text
read committed manifest tail
→ validate session/build identity
→ locate persisted checkpoint
→ validate checkpoint wire hash
→ deserialize checkpoint
→ restore checkpoint
→ continue exact stepping
```

Checkpoint validation occurs even when no additional frame needs to be rendered.

Resume pixel-equivalence claims apply only within the same compatible build/render environment; R1.6 does not claim cross-platform or cross-GPU pixel identity.

### Failure semantics

Any sequence transaction failure places the sequence into fail-stop state.

No later frame is advanced or committed through that failed Session instance.

---

## 7. Overwrite Policies

R1.6 v1 freezes three frame policies:

```text
Reject
Overwrite
VerifySkip
```

The policies apply to committed frame records/artifacts, not to the manifest container itself.

`VerifySkip` still deterministically evaluates and renders the requested frame, compares canonical `rgbaHash`, validates the existing artifact file hashes, and skips only when the committed output is intact and equivalent.

Orphan artifacts without a committed manifest record are not trusted by Reject or VerifySkip.

---

## 8. Validation

Final validation matrix:

```text
Windows CORE   8/8 PASS
Windows FULL   9/9 PASS

Linux CORE     8/8 PASS
Linux FULL     9/9 PASS
```

Validation covers, among other cases:

```text
Static offscreen rendering
Generic runtime offscreen rendering
Saba dynamic UV/material rendering
ModelInstance publication after exact stepping
MMD camera/light presentation
FOV-driven offline projection
PNG RGBA8 round-trip
from-start deterministic sequences
sequential pre-roll
checkpoint resume equivalence
Reject / Overwrite / VerifySkip
corrupt and missing committed artifacts
orphan artifacts
JSONL crash-tail recovery
empty-manifest fresh-session recovery
A/B checkpoint alternation
historical cursor preservation
append-only forward history
latest checkpoint resume-anchor protection
```

---

## 9. Frozen Boundaries

R1.6 does **not** include:

```text
true headless EGL/OSMesa/surfaceless context provider
GraphicsDevice multi-context/share-group redesign
Stable Render C Portal
video encoding / FFmpeg
audio synchronization
multi-runtime deterministic sequence orchestration
orthographic host Camera type
cross-platform pixel-exact guarantee
```

These items remain explicitly outside the R1.6 closure boundary.

---

## 10. Final Status

```text
R1.6 Phase 0A — CLOSED
R1.6 Phase 0B — CLOSED
R1.6 Phase 0C — CLOSED
R1.6 Phase 0D — CLOSED
R1.6 Phase 0E — CLOSED
```

Therefore:

# R1.6 — Deterministic Offline Output Pipeline

**FROZEN + IMPLEMENTED + VALIDATED + CLOSED**

The engine now possesses a continuous WISTERIA-owned path from runtime state to deterministic persistent frame output:

```text
Asset
→ Runtime
→ Deterministic State
→ Renderer-Facing Visual State
→ Explicit Presentation
→ Renderer
→ Offscreen SceneColor
→ Canonical RGBA8
→ Deterministic Persistent Sequence
```

Future work must consume these frozen boundaries rather than reopen them without concrete counterevidence.
