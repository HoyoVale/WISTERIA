# Phase R1 — Engine-Owned Model Runtime Architecture

## 1. Purpose

R1 establishes the ownership boundary for every model backend supported by WISTERIA.

The invariant is:

> WISTERIA owns assets, scene instances, mutable per-instance state, render resources, update order, C handles and destruction. A format-specific library only evaluates its private model semantics and publishes results through WISTERIA interfaces.

Saba is the first backend proving this contract. It remains responsible for PMX/VMD-specific evaluation, IK, morph rules and MMD physics compatibility. It is not the owner of the Scene, Entity, GPU mesh, render passes or public C ABI.

## 2. Runtime topology

```text
ModelAsset (shared and immutable)
    ├─ Mesh / Material / Texture / RenderPart
    ├─ Skeleton / Morph / Animation / Physics metadata
    └─ ModelSourceDescriptor + ModelBackendKind
                 │
                 ▼
ModelBackendRegistry
    └─ SabaMmdBackend::CreateRuntime(ModelAsset)
                 │
                 ▼
ModelInstance (owned by one Entity)
    ├─ IModelRuntimeDriver
    ├─ instance-local dynamic Mesh clones
    ├─ backend-neutral Pose / ModelVertexFrame
    └─ model asset reference
                 │
                 ▼
Entity → Scene update → WISTERIA Renderer / Shadow passes / C ABI
```

The renderer never receives a Saba object. It renders WISTERIA `RenderPart`, `Mesh`, `Material`, `Transform` and `Pose` objects.

## 3. New ownership rules

1. `ModelAsset` is shared and immutable after resource creation.
2. Every mutable model execution belongs to one `ModelInstance`.
3. `Entity` owns its `ModelInstance`; `Scene` owns the `Entity`.
4. Runtime-generated vertices never write back into a shared asset mesh.
5. A backend is selected through `ModelBackendKind` and `ModelBackendRegistry`.
6. `Scene::Update()` is the authoritative per-frame scheduler.
7. Saba types remain inside backend/runtime implementation code and the optional demo.
8. Public C callers control the same `WisteriaEntity` that is rendered.

## 4. Main implementation changes

### Backend-neutral runtime contract

`IModelRuntimeDriver` now provides:

- initialization, update and reset;
- authoritative WISTERIA `Pose`;
- backend-neutral `ModelVertexFrame`;
- optional WISTERIA `PhysicsInstance` ownership marker;
- named morph control;
- a stable backend name for diagnostics.

`MmdRuntimeModel` extends this contract only with MMD semantic operations such as VMD playback, MMD IK and MMD physics settings.

### Instance-local render data

`Mesh::CloneForInstance()` and `ModelInstance` prevent two entities from sharing mutable dynamic geometry. The same PMX can now be instantiated more than once with independent runtime, morph, timeline and physics ownership.

### Scene integration

`Scene::InstantiateModel()` creates the runtime through `ModelBackendRegistry`, gives it to a WISTERIA-owned `ModelInstance`, resolves instance-local meshes and attaches the instance to the Entity. Demo-only manual vertex-provider wiring is no longer the primary path.

### WISTERIA-owned pose

`SabaMmdRuntimeModel` materializes Saba's node hierarchy into a WISTERIA `Skeleton` and updates a WISTERIA `Pose` each frame. Scene and export code no longer see the old single-root placeholder pose.

### C ABI v0.7

The preferred renderable flow is:

```text
create_context
→ create_window / hidden_window
→ scene_create
→ scene_load_model
→ scene_instantiate_model
→ entity_load_motion / entity_set_motion_frame / entity_set_physics_settings
→ poll_and_render / read_pixels
→ entity_bone_* / entity_vertex_bounds
→ entity_destroy / scene_destroy / window_destroy
```

The original `WisteriaModel` headless functions remain as compatibility APIs, but are explicitly marked legacy. Their owner type is now the generic `MmdRuntimeModel`, not `SabaMmdRuntimeModel`.

## 5. Lifecycle and ABI corrections included in R1

- Destroying an old Scene no longer detaches a newer Scene bound to the same Window.
- Destroying a Window invalidates its Scene entries instead of leaving dangling raw Window pointers.
- `wisteria_physics_reset` resets physics only; it no longer resets the motion frame.
- New entity runtime functions use a common exception guard.
- Transform, Camera and light mutation functions now map C++ validation failures into `WisteriaStatus` instead of allowing exceptions to cross the C boundary.
- Camera pose replacement is atomic, avoiding invalid intermediate camera states.

## 6. R1 acceptance tests

`R1 engine-owned MMD instances` uses the repository PMX fixture and requires:

- one shared `ModelAsset`;
- two distinct runtime drivers;
- two distinct instance Meshes;
- two independent morph states;
- two independent vertex frames;
- real WISTERIA Pose publication;
- destruction of one instance without invalidating the other.

`R1 project MMD instance` discovers the supplied project assets recursively and requires:

- a real multi-bone PMX Pose with finite matrices;
- two independent Saba runtime instances;
- two independent physics ownership objects;
- two independent dynamic vertex frames;
- one real VMD loaded into both instances with distinct frame positions.

The test runner now reports `PASS`, `FAIL` and `SKIP` separately. Missing optional assets and unavailable window backends no longer appear as false passes.

## 7. Deliberate R1 boundaries

R1 establishes logical runtime ownership, but does not claim that all future engine work is complete.

Still pending:

1. Move Saba/Bullet from a mandatory core link dependency to a selectable build-time backend module.
2. Replace Saba camera/light methods that directly accept WISTERIA objects with backend-neutral camera/light samples.
3. Export complete rigid-body, joint, contact and physics snapshot state.
4. Add deterministic seek policies and replay/checkpoint-based offline evaluation.
5. Avoid re-reading the PMX source when a Saba runtime is created by retaining an opaque backend asset payload.
6. Add instance-level material overrides instead of replacing an imported material with a plain PBR material.
7. Add explicit Context asset/shader/cache roots instead of relying on process working directory.
8. Expand render export beyond synchronous RGBA8 to depth, normal, object ID and asynchronous readback.
9. Remove the legacy headless model facade after frontend migration and an ABI deprecation window.

These are follow-up phases. They do not invalidate the R1 ownership boundary.

## 8. Rule for future backends

A future glTF, VRM, FBX or procedural runtime must integrate by implementing the same backend/runtime contracts. It must not add format-specific pointers or branches to `Scene`, `Entity`, `Renderer` or the public C ABI.
