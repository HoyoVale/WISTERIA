# R1.5 Phase 0D — Runtime Authority Integration 实现基线（2026-08-08）

> 状态：**COMPLETED（四矩阵全绿）**。
> 契约：`docs/architecture/R1_5_SECOND_DYNAMIC_RUNTIME_CONTRACT.md`
> （CONTRACT FROZEN）。

## 1. 一句话

把“手动显式 Generic Runtime 能正确工作”升级为“正常导入的 Generic 模型
自动走 Runtime”：ResourceManager 按导入结果分类、`BackendKind()` 权威
收口、Scene 删除最后的双轨状态所有权。

## 2. 代码改动

### ResourceManager 分类（审查步骤 1–2）

`LoadModel()` 不再按扩展名一刀切：

```text
PMX                         → SabaMmd
非 PMX 且有 Skeleton/Morph/AnimationClip → WisteriaGeneric
其余                         → Static
```

导入完成后 `model->SetBackendKind(backendKind)`，
`ModelAsset::backendKind` 成为唯一权威；`ModelSourceDescriptor.backend`
仍写入，但身份是 **legacy informational metadata / compatibility
residue**——真正 source identity 是 `sourcePath`，该字段不再参与
Runtime 选择。

### BackendKind 权威收口（审查步骤 3）

`ModelAsset::BackendKind()` 删除 `sourceDescriptor->backend` fallback：

```cpp
return backendKind.has_value() ? *backendKind : ModelBackendKind::Static;
```

所有手动构造资产并依赖 descriptor 的调用方迁移到显式
`SetBackendKind`：

- `src/native/wisteria_stable_runtime.cpp`（Stable C ABI PMX 路径）
- R1.4 runtime creation options 两个测试
- unit 测试的程序化 animated model

### Scene 双路径删除（审查步骤 4–5）

`Scene::InstantiateModel` 删除：

```cpp
if (!backendDriven) { SetMorphSet / SetSkeleton / Animator.Play }
backendDriven ? std::nullopt : part.MorphMaterialIndex()
```

现在统一：

```text
CreateRuntime（Static → nullptr）
→ ModelInstance
→ Entity.SetModelInstance
→ 每个 Part 永远保留 MorphMaterialIndex
```

无 MorphState 的 backend 由 `EvaluateMaterialMorphs(part, nullptr)`
天然落到 base material；Saba material morph 数据为 R1.6 保留。

### Entity 直接访问审计（审查步骤 6）

```text
SetSkeleton / SetMorphSet / legacy Update branch / fallback storage
  → 保留（standalone Entity compatibility）

SolveAfterPhysicsPose
  → 改为 TryGetAnimator()（Runtime-first）：
    standalone → legacy Animator
    Generic Runtime → runtime-owned Animator
    Saba Runtime → nullptr（不误调）
```

其余 `this->pose / this->animator / this->morphState` 访问点逐一核对，
均为 standalone 语义或 fallback，不做机械删除。

## 3. 回归与新增验证

### 自然路由（审查步骤 7）

`TestAnimatedModelImporter`（原“Animated model importer”）现在直接断言：

```text
LoadModel(animated_triangle.gltf)
→ BackendKind == WisteriaGeneric（无需手工 SetBackendKind）
→ Scene::InstantiateModel
→ runtime 存在且 BackendName == "wisteria-generic"
→ scene.Update(0.25) 后 Pose x == 0.5
```

### 静态回归（审查步骤 8）

新增 `TestStaticModelClassification`（box-glb）：

```text
BackendKind == Static
Scene 实例 Runtime == nullptr
RenderPart 完整保留
无 Pose / Animator / MorphState 伪造
MorphMaterialIndex 逐 part 与 asset 一致
```

### PMX 回归（审查步骤 9）

既有 `TestR1EngineOwnedMmdInstances` 断言
`BackendKind == SabaMmd` + runtime 为 `saba-mmd`，继续全绿；
Stable C ABI PMX 路径补显式 `SetBackendKind(SabaMmd)`。

### Phase 0C 测试迁移

`TestWisteriaGenericAnimatedTriangleEquivalence` 与
`TestWisteriaGenericMultiInstance` 删除手工 `SetBackendKind`，
改断言自然分类，四条 Generic 测试继续通过。

## 4. 四套矩阵（2026-08-08 实测）

```text
Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）。FULL 覆盖 rigged GLB 新分类路径、
production PMX 跨进程 E2E 与 R1.2–R1.4 全部回归。

## 5. 尚未处理（明确记录）

1. **Generic Reset() playback 语义未冻结**（审查非阻塞 follow-up）：
   当前实现为 `Stop(true) + bind + morph reset`（语义 A），不是
   “恢复 fresh Initialize（clip0 自动播放）”（语义 B）。R1.5 Final
   Validation 前需二选一并补测试；当前行为不违反冻结契约。
2. `ModelSourceDescriptor.backend` 字段保留但已无权威作用；若后续
   ABI 稳定面收紧可删除，不在 R1.5 范围。

## 6. 下一步

Phase 0E — Final Validation：

```text
Static asset:                 Runtime == nullptr
Procedural vertex runtime:    no Pose、geometry 变化、multi-instance 独立
Procedural skeletal runtime:  Pose 变化、snapshot 可用
Generic GLTF:                 animated_triangle 自然迁移等价
Generic Morph:                weights + renderer + snapshot
RootMotion:                   exactly-once 应用
Saba:                         R1.2–R1.4 矩阵不变
Renderer:                     0 Saba 类型、0 WisteriaGeneric 类型
四套矩阵：                    全绿
```
