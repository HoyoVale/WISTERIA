# WISTERIA Structure Stabilization R0

## Decision

The MMD path is feature-complete enough to freeze expansion temporarily. The
next milestone is not another rendering feature or a larger C ABI. It is a
boundary-stabilization pass.

> 更新（2026-08，R1 收口后）：边界稳定化已完成并通过全栈审查。C 门户
> （`wisteria_native`）从冻结状态解除，进入全面导出阶段：`WISTERIA_BUILD_NATIVE`
> 默认开启，导出面按 FULL_STACK_AUDIT_R1.md 的优先级推进（物理预设 →
> 渲染配置 → MMD 控制）。新导出函数仍需引擎级用例 + 回归测试，v0.x 不承诺
> ABI 稳定。

The native facade remains available as an explicit opt-in experiment, but v0.x is not a stable
SDK. Fresh builds leave `WISTERIA_BUILD_NATIVE=OFF` unless a binding test explicitly enables it. New exported functions require an engine-level use case and a regression
test. Frontends must not dictate core ownership or frame scheduling.

## Problems found in WISTERIA(17)

### P0: OpenGL program cache crossed context boundaries

The material program cache was process-global and keyed only by shader paths.
OpenGL object names are valid only inside their context share group. A second
`Application` could therefore reuse a program name created by an unrelated
context. Mesa commonly exposes this as invalid operations, black/garbled
frames, or inconsistent first-frame behavior.

R0 makes the cache owned by `ResourceManager`, which is owned by one
`Application` and therefore one OpenGL share group.

### P0: GLFW lifetime was treated as per-Application state

Every `Application` called `glfwInit` and `glfwTerminate`. Destroying one native
Context could terminate GLFW while another Context still owned windows. R0
adds process-wide reference counting while keeping window/resource ownership
inside each Application.

### P0: frame API had the wrong ownership semantics

`wisteria_window_poll_and_render(window, dt)` validated one window but advanced
and rendered every window owned by the Context. A frontend iterating windows
would update animation and physics multiple times per visible frame.

R0 adds `wisteria_poll_and_render(context, dt_seconds)`. The old symbol remains
as a compatibility wrapper and is documented as context-wide.

### P1: resource lookup depended on Windows separators and process CWD

Shader and texture defaults built paths with `\\`. Other demo resources were
resolved directly from `current_path()`. R0 uses `std::filesystem` throughout
and supports `WISTERIA_ASSET_ROOT` as an explicit deployment override.

This is a stabilization mechanism, not the final packaged-resource service.
The final service should resolve from an application configuration object or
installed resource manifest rather than global environment state.

### P1: the native adapter was a 1000-line mixed-responsibility file

The file combined registry/lifetime, model simulation, windowing, input and
camera logic. R0 splits it internally without changing the public umbrella
header.

### P1: Linux configuration pulled both window backends and Saba's viewer

GLFW 3.4 can require both X11 and Wayland development stacks by default. The
Saba integration also built its standalone OpenGL viewer despite claiming to
build only the core runtime. R0 selects X11 by default (`WAYLAND` and `BOTH`
remain opt-in) and makes the Saba viewer opt-in.

## Target architecture

更新（R1 收尾）：`wisteria_core` 已拆出 GLFW 依赖。当前结构为两个库：

```text
wisteria_core
  动画 / 资源 / 物理 / 渲染(OpenGL) / MMD / Scene
  不包含、不链接 GLFW；Timer 使用 std::chrono

wisteria_platform
  Application 帧循环 / Window / WindowManager / Input(GLFW) / demo_scene
  链接 wisteria_core + glfw
```

后续若出现第二个后端（Vulkan、软件渲染器）或需要独立 MMD 运行时，再按
下面的细粒度方向拆分；当前不继续机械切分。

```text
wisteria_foundation
  math-neutral utilities, transforms, diagnostics, file/path abstractions

wisteria_animation
  skeleton, pose, animation, morph evaluation

wisteria_mmd_runtime
  PMX/VMD/Saba/Bullet simulation, no GLFW, GLAD, Renderer or GPU upload

wisteria_rendering_gl
  OpenGL resources and render passes; consumes immutable render packets

wisteria_platform_glfw
  GLFW lifetime, windows, input and context/share-group management

wisteria_scene
  orchestration that connects runtime snapshots to rendering

wisteria_native_headless
  C ABI for model/motion/physics/diagnostics only

wisteria_native_desktop (optional)
  C ABI for windows and rendering; depends on platform + scene + rendering
```

## Required dependency inversion before target extraction

`SabaMmdRuntimeModel` currently implements rendering-oriented methods and
includes `Mesh`, `Camera` and `DirectionalLight`. Moving the same file into a
new library would not create a real headless boundary.

Introduce host-neutral values first:

```cpp
struct MmdFrameSnapshot
{
    Pose pose;
    std::span<const MmdSkinnedVertex> vertices;
    CameraTrackSample camera;
    LightTrackSample light;
    MmdPhysicsDiagnostics physics;
};
```

The runtime produces a snapshot. A separate GL adapter uploads it to `Mesh`.
The scene applies camera/light samples. Only after this inversion should CMake
extract `wisteria_mmd_runtime`.

## Large-file decomposition plan

### `src/assets/importer.cpp`

```text
assets/import/
  importer_dispatch.cpp
  assimp_scene_reader.cpp
  static_mesh_builder.cpp
  skeleton_builder.cpp
  animation_builder.cpp
  material_builder.cpp
  texture_resolver.cpp
  mmd_metadata_reader.cpp
```

Keep transaction/validation policy in one coordinator. Do not split by merely
copying helper functions into arbitrary files.

### `src/physics/physics_world.cpp`

```text
physics/bullet/
  bullet_world.cpp
  bullet_body_factory.cpp
  bullet_constraint_factory.cpp
  bullet_stepper.cpp
  bullet_debug_draw.cpp
  bullet_statistics.cpp
  bullet_recovery.cpp
```

Generic physics interfaces remain above Bullet. MMD compatibility/adaptive
policy remains in `mmd/physics`, not in the generic world.

### `src/rendering/renderer.cpp`

```text
rendering/passes/
  opaque_pass.cpp
  weighted_oit_pass.cpp
  present_pass.cpp
  physics_debug_pass.cpp
  skinning_upload.cpp
  morph_upload.cpp
```

`Renderer` becomes orchestration plus per-context cache ownership. Passes must
receive explicit inputs and may not find resources through global state.

## Native ABI rules for v0.x

1. Opaque integer handles only; no C++ object layout crosses the boundary.
2. Every output pointer is initialized to a neutral value before work starts.
3. Time units appear in names/comments and are seconds.
4. Frame stepping is owned by Context/Application, never by one Window.
5. No implicit current working directory contract.
6. No process-global OpenGL object cache.
7. Destruction must not invalidate unrelated Contexts.
8. New structs will start with `struct_size` and `api_version` before ABI
   stabilization.
9. Add a capability query before introducing optional desktop/headless
   variants.
10. Keep the public ABI thin; behavior/policy belongs to typed C++ interfaces.

## Next milestone: R1

R1 should be a pure dependency-inversion milestone:

1. Extract `MmdFrameSnapshot` and a GL upload adapter.
2. Make Saba runtime compile without renderer/platform headers.
3. Create `wisteria_mmd_runtime` and a genuinely headless native target.
4. Add a Linux headless CI job that does not install X11/Wayland/OpenGL.
5. Add a desktop Linux job under Xvfb/WSLg with GL vendor/version and first
   120-frame diagnostics.
6. Only then decide whether the desktop C ABI should remain, be redesigned, or
   be replaced by a higher-level IPC boundary.
