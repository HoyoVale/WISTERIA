# R1.8 Final Closure — Generic Deterministic Runtime

**Status: FROZEN / IMPLEMENTED / VALIDATED / CLOSED**

R1.8 promotes deterministic capability from a Saba MMD specialty to the
standard WISTERIA runtime contract: exact frame stepping, snapshot/restore,
checkpoint (payload kind 2) and replay now work for the Generic runtime, and
OfflineFrameSequence is backend-neutral.

## 1. Final Architecture

```text
IModelRuntimeDriver
  ├─ SabaMmdRuntimeModel    exact step + checkpoint kind 1
  └─ WisteriaGenericRuntime exact step + checkpoint kind 2
        ↓
DeterministicBackendCapabilities (authoritative)
        ↓
OfflineFrameSequence(IModelRuntimeDriver&, ...)
  ├─ capability gate (exact + checkpoint capture/restore/replay)
  ├─ IDeterministicFrameStepper (capability/interface divergence fails)
  ├─ checkpoint dispatch by payload kind
  └─ zero-window RenderRange / Resume on HeadlessRenderSession
```

## 2. Frozen Semantics

```text
1. MotionFrameIndex is the engine-owned 30Hz canonical coordinate; source
   clips remain continuous-time assets sampled at N/30.
2. StepMotionFrameExact(N) is absolute-coordinate evaluation; frozen config
   drift (including loopMotion) returns DeterminismViolation.
3. One deterministic root-motion delta per exact boundary goes to pending
   state; orchestration consumes it at most once.
4. Generic Deterministic Mode v1 subset rejects crossfade / state machine /
   triggers / speed != 1 / paused / IK overrides explicitly.
5. Checkpoint semantic validation is identical in-memory and over the wire.
6. assetFingerprint covers the whole ModelAsset (parts, mesh topology, mesh
   morph offsets, skeleton, morph definitions, clips/keys).
7. OfflineFrameSequence v1 rejects enabled generic root motion until
   Entity/world-transform checkpoints exist.
```

## 3. Validation Matrix (2026-08-09)

```text
Windows CORE (MSVC RelWithDebInfo)             8/8  PASS
Windows FULL (MSVC + full assets)              9/9  PASS
Linux CORE (GCC / WSL, llvmpipe)               9/9  PASS
Linux FULL (GCC / WSL + full assets)          10/10 PASS
```

The Linux matrices use the WSL software-rendering compatibility policy.
headless-smoke verifies the zero-window Generic sequence:
`RenderRange(0..2)` -> fresh `Resume(4)` -> from-start `0..4` equivalence,
with the manifest reporting `wisteria-generic`.

## 4. Closed Phases

```text
0A  Contract and decisions        CLOSED
0B  Generic canonical timeline    CLOSED
0C  Generic checkpoint kind 2     CLOSED
0D  Backend-neutral sequence      CLOSED
0E  Matrices and closure          CLOSED
```

## 5. Frozen Boundaries

```text
Saba migration to IDeterministicCheckpoint
Entity/world-transform checkpoint (sequence root motion rejected in v1)
VRM / new backends
Stable Runtime/Render C ABI (R1.9)
RenderDevice / RenderGraph / Vulkan (R2.x)
```

## 6. Relationship to R1.7

The R1.7 native-Linux hardware release gate (real Linux machine,
`script/verify_r17_native_linux.sh`) remains a shared outstanding item;
R1.8 is CPU/runtime work and does not depend on it, but the R1 series
sign-off requires it.

## 7. Next Steps

```text
R1.9  Stable Runtime / Render C ABI
R2.x  RenderDevice / RenderTarget / RenderGraph / multi-backend
```

R1.8 is closed. Future work must consume the frozen deterministic
capability model, GenericR18 payload, asset fingerprint and sequence
root-motion boundary rather than reopen them without counterevidence.

