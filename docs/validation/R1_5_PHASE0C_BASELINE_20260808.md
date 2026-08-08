# R1.5 Phase 0C — WisteriaGenericRuntimeDriver 实现基线（2026-08-08）

> 状态：**FIRST HALF COMPLETED（Driver 独立验证）**。
> 契约：`docs/architecture/R1_5_SECOND_DYNAMIC_RUNTIME_CONTRACT.md`
> （CONTRACT FROZEN）。
> 本轮范围：Driver 自身行为 + fixture 等价验证；**不做 Scene 双路径删除**
> （Phase 0D）、不做 ResourceManager 分类迁移（Phase 0D）。

## 1. 一句话

把已经成熟的 WISTERIA Animator/Pose/MorphState 原样迁入
`WisteriaGenericRuntimeDriver` 的 ownership，并证明
`animated_triangle.gltf` 迁移前后行为等价。

## 2. 新增/改动

### 新增

```text
include/wisteria/runtime/wisteria_generic_runtime_driver.hpp
src/runtime/wisteria_generic_runtime_driver.cpp
```

### 生产改动

- `CMakeLists.txt`：`WISTERIA_RUNTIME_SOURCES` 加入 driver。
- `model_backend.cpp`：新增 `WisteriaGenericBackend`
  （Kind=`WisteriaGeneric`，Name=`wisteria-generic`），
  `RegisterDefaultModelBackends()` 在 Saba 之后注册。

## 3. Driver 行为（对应审查 14 步）

```text
1.  WisteriaGenericRuntimeDriver 新建完成
2.  只从 ModelAsset immutable state 构造（不重读文件）
3.  HasSkeleton → owned Pose + Animator
4.  HasMorphs   → owned MorphState
5.  Animator 构造时连接 owned MorphState
6.  Initialize 继承默认播放：Animator 存在 && ClipCount>0
    → Play(AnimationClipAt(0))
7.  Update：animator.Update(dt) → pendingRootMotion 取走
    Reset：Stop(true) + Pose bind + MorphState.Reset + pending identity
8.  TryGetPose / TryGetAnimator / TryGetMorphState 全部实现
9.  neutral morph bridge：
    MorphCount / DescribeMorph / ReadMorphState / MorphRevision
    + SetMorphWeight / MorphWeight 全部桥接 owned MorphState
    （读与写同源，无第二套入口）
10. animated_triangle.gltf 迁移等价（见 §4）
11. 极小 procedural MorphSet fixture（见 §4）
12. RootMotion fixture（见 §4）
13. multi-instance 独立性（见 §4）
14. 四套矩阵（见 §5）
```

存在性规则按契约 §6：

```text
HasSkeleton → Pose exists → Animator exists
HasMorphs   → MorphState exists
!HasSkeleton && HasMorphs → MorphState only（Pose/Animator 为 null）
AnimationClipCount > 0 && !HasSkeleton → Initialize() 返回 false
   （ModelAsset::AddAnimationClip 在资产层已拒绝该组合，此守卫是
     纵深防御）
```

`Capabilities()` 保持默认全 false，`PhysicsInfo()` 因无 PhysicsInstance
返回 `available=false / ownsSimulationStep=false`，符合契约 §5.5。

## 4. 验证（Windows CORE，新增 4 项 integration 测试）

### animated_triangle 迁移等价

同一 `ModelAsset`（显式 `WisteriaGeneric`）两条路径：

```text
旧路径：Entity::SetSkeleton + Animator::Play
新路径：Registry → Runtime → ModelInstance → Entity
```

采样 t = 0 / 0.25 / 0.5 / 1.0：

```text
Pose 全骨骼 LocalMatrix 逐元素相等
Animator Time 相等
默认 clip playback（CurrentClip == clip0 && IsPlaying）成立
ModelFrameSnapshot.pose 与旧路径 Pose 一致
运动峰值 |x| == 1.0（fixture 关键帧 (0,0)→(1.0,2.0)，t=1 时循环回绕）
```

### Morph fixture

```text
TryGetMorphState / TryGetPose / TryGetAnimator 存在性正确
Entity::TryGetMorphState == runtime->TryGetMorphState（Renderer 取数同源）
neutral bridge：MorphCount / Describe / Read / Revision / Set / Get 一致
unknown name 读/写返回 false/nullopt（不抛）
MorphSnapshot captured 且 rawWeight 正确
两实例 MorphState 独立
morph-only 资产：MorphState 存在，Pose/Animator == nullptr
```

### RootMotion fixture

```text
旧路径 vs 新路径：SetRootMotionBone + SetRootMotionEnabled(true)
Update(0.5) 后 Transform.x 均为 0.5
零时长再 Update 不再位移（exactly-once）
```

### multi-instance

```text
同资产两个 ModelInstance：Runtime 对象不同
不同 dt 更新后 Pose 至少一根骨骼不同
Animator Time 各自独立（0.5 vs 0.25）
```

## 5. 四套矩阵（2026-08-08 实测）

```text
Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed（含 FULL 跨进程 E2E）
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed（含 FULL 跨进程 E2E）
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）。

## 6. 明确的边界（Phase 0C 第一半不做）

- 不删 `Scene::InstantiateModel` 的 `if (!backendDriven)`（Phase 0D）；
- 不改 `ResourceManager::LoadModel` 的扩展名分类（Phase 0D）；
- `ModelAsset::BackendKind()` 的 `sourceDescriptor->backend` legacy
  fallback 保留（Phase 0D 与分类迁移一起移除；当前 LoadModel 尚未
  `SetBackendKind`，立即移除会破坏 PMX → Saba 路由）；
- 不改 Renderer；不做 checkpoint / deterministic exact frame /
  Stable C ABI / SimpleBroadphase；
- 不修 Saba UV/material morph（R1.6 debt ledger）。

## 7. 下一步

Phase 0D：

```text
ResourceManager 分类改为基于导入结果（Skeleton/Morph/Clip → Generic）
+ ModelAsset::BackendKind 移除 legacy fallback
+ Scene::InstantiateModel 删除 backendDriven 双分支
+ Entity 内部 this->animator / this->pose / this->morphState
   直接访问审计（保留 standalone Entity 兼容，不机械删除）
```
