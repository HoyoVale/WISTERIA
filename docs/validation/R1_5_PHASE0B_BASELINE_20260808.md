# R1.5 Phase 0B — Procedural Canary 实现基线（2026-08-08）

> 状态：**COMPLETED**。契约：
> `docs/architecture/R1_5_SECOND_DYNAMIC_RUNTIME_CONTRACT.md`
> （CONTRACT FROZEN，2026-08-08 最终契约级审查闭合）。

## 1. 一句话

用 test-only `ProceduralTestBackend`（复用 `WisteriaGeneric`）证明
`IModelRuntimeDriver / ModelInstance / ModelBackendRegistry` 不是 Saba
特制：vertex-only、1-bone、root-motion 三种形态全部走
`Registry → Runtime → ModelInstance → Entity → Snapshot` 链并通过验收。

## 2. 代码改动

### 生产接口（步骤 1–6）

1. `ModelBackendKind::WisteriaGeneric = 2`（数值冻结，不重排）。
2. `ModelAsset` 独立 backend 权威：
   - 新增 `SetBackendKind / HasExplicitBackendKind` 与独立
     `backendKind` 成员；
   - `BackendKind()` 以显式 `backendKind` 为唯一权威；
     `sourceDescriptor->backend` 退化为 Phase 0C 前保留的 legacy
     fallback，不再有权当 identity。
3. `IModelRuntimeDriver` 接口可选化：
   - `GetPose()` 纯虚改为 `TryGetPose()`（noexcept）+ 便捷
     `GetPose()`（无 Pose 抛 `std::logic_error`）；
   - 新增默认 `nullptr` 的 `TryGetMorphState() / TryGetAnimator()`；
   - 新增默认 identity 的 `ConsumeRootMotion()`；
   - `ProduceFrameView()` 改用 `TryGetPose()`。
4. `SabaMmdRuntimeModel` 改实现 `TryGetPose()`（内部 `GetPose()` 语义
   不变）。
5. `ModelInstance`：
   - `Update()` 返回 `RootMotionDelta`，固定顺序：
     runtime.Update → publish frame view/metadata → ConsumeRootMotion →
     return delta；
   - `CapturePose()`：无 Pose 的 Runtime → `pose.captured = false`，不抛；
   - `CaptureGeometry()`：冻结 zero-value 语义——
     positions/normals 双空 → `geometry.captured = false`；
     size 不一致 → `std::logic_error`。
6. `Entity`：
   - `TryGetPose / TryGetMorphState / TryGetAnimator` 全部
     Runtime-first，fallback legacy standalone 状态；
   - `Entity::Update` 对 Runtime 路径应用 ModelInstance 返回的
     RootMotionDelta（exactly-once）。

### 测试 canary（步骤 7–9）

新增 `tests/procedural_canary.hpp`（test-only，不进 wisteria_core）：

```text
procedural-vertex-canary
  TryGetPose / MorphState / Animator 全 nullptr
  NeedsDynamicVertexUpload = true，三角顶点随 sin(time) 变化
  Snapshot: valid=true, pose=false, morphs=false, geometry=true

procedural-one-bone-canary
  1-bone Skeleton + Pose + Animator（HasSkeleton → Animator exists）
  Snapshot: pose=true（revision 随帧推进）, geometry=false

procedural-root-motion-canary
  无 Pose/geometry；每次 dt>0 的 Update 产生 0.5*dt 的 X 平移
  ModelInstance::Update 消费一次；Entity 应用一次
```

Canary 变体选择走 `ModelAsset::Name()` 约定（test-only 捷径），
不占用 `RuntimeCreationOptions`（其 Generic 语义已冻结）。

## 3. 验收结果（2026-08-08，Windows/MSVC Release）

新增 runtime 测试三项全部 [PASS]：

```text
R1.5 procedural vertex canary
R1.5 procedural one-bone canary
R1.5 procedural root motion exactly once
```

四套矩阵（CTest 8/8）全绿：

```text
wisteria.unit        Passed
wisteria.runtime     Passed（含 3 项新 canary）
wisteria.integration Passed（Saba / R1.2–R1.4 矩阵不变）
wisteria.render-fbo  Passed
wisteria.abi-safety-matrix     Passed
wisteria.abi-c-smoke           Passed
wisteria.checkpoint-cross-process        Passed
wisteria.stable-checkpoint-cross-process Passed
```

## 4. 明确的边界（Phase 0B 不做）

- 不实现 `WisteriaGenericRuntimeDriver`（Phase 0C）；
- 不删 `Scene::InstantiateModel` 双路径（Phase 0D）；
- 不动 Renderer（R1.5 只改取数来源）；
- 不给 Generic 伪造 VMD 30Hz motionFrame（§5.6）；
- canary 不注册进 `RegisterDefaultModelBackends`，只存在于测试。

## 5. 下一步

Phase 0C：`WisteriaGenericRuntimeDriver`（迁入 Pose/MorphState/Animator，
`animated_triangle.gltf` 迁移等价、morph fixture、root-motion 复用）。
