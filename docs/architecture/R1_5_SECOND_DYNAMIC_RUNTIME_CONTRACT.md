# R1.5 Phase 0A — Second Dynamic Runtime Contract

> 状态：**FROZEN DRAFT（2026-08-08，待契约级审查）**。
> 一句话：**证明 `IModelRuntimeDriver / ModelInstance / ModelBackendRegistry`
> 不是为 Saba 特制的。**

## 1. 背景

R1.0–R1.4 已证明 Saba 可以被 WISTERIA 管起来：backend registry、中性
frame state、deterministic timeline、checkpoint wire、跨进程 restore、
Stable C ABI v1 全部成立。但当前真实动态 Runtime 只有 Saba；glTF/Assimp
动态模型仍走 `Entity` 内旧 Animator/Pose/MorphState 路径
（`Scene::InstantiateModel` 的 `if (!backendDriven)` 分支），因此
**Runtime 架构的通用性尚未被证明**。R1.5 只做这一件事。

## 2. 核心原则

```text
1. BackendKind 表达运行时需求，不是文件扩展名。
2. ModelAsset 的 backend identity 与 file source identity 是两个概念。
3. Runtime 是 mutable evaluation-state 的唯一 owner；
   ModelInstance 是聚合 owner（拥有 Runtime 与快照），不另建状态副本。
4. Renderer 不感知任何 backend（0 个 Saba 类型、0 个 WisteriaGeneric 类型）。
5. 非模型 standalone Entity 的 Animator/Pose/MorphState 兼容保留；
   R1.5 只要求“Model-backed 动态状态”不再由 Entity 直接拥有。
```

## 3. Backend 分类（冻结）

```cpp
enum class ModelBackendKind : std::uint8_t
{
    Static = 0,          // 真正无逐帧状态；ModelInstance + no Runtime
    SabaMmd = 1,         // 不变
    WisteriaGeneric = 2  // 新增：Skeleton/Morph/AnimationClip 通用动态模型
};
```

数值冻结，不得重排。资产分类语义（非扩展名规则）：

```text
PMX
→ SabaMmd

非 PMX 且 (HasSkeleton || HasMorphs || AnimationClipCount > 0)
→ WisteriaGeneric

其余
→ Static
```

一个完全静态的 GLB 仍必须是 `Static`；`animated_triangle.gltf` 因带
Skeleton + AnimationClip 而进入 `WisteriaGeneric`。

当前 `ResourceManager::LoadModel()` 按扩展名（`.pmx` 与否）分类，是
R1.5 要修正的已知缺口：分类必须基于导入结果（Phase 0C 落地）。

## 4. Asset / backend 所有权（冻结方向）

目标形态：

```text
ModelAsset
├─ BackendKind          （独立身份，不寄生在 SourceDescriptor）
└─ optional SourceDescriptor
```

允许无 `sourcePath` 的 procedural asset（如 canary），仍携带
`WisteriaGeneric` backend 身份。当前 `ModelAsset::BackendKind()` 返回
`sourceDescriptor->backend` 的寄生关系在 Phase 0C 移除。

## 5. Runtime contract 修正（冻结）

### 5.1 Optional Pose（最重要）

当前 `IModelRuntimeDriver::GetPose()` 是纯虚且 `ProduceFrameView()` 默认
强制 `&GetPose()`——这隐含“所有 Runtime 必须有 Skeleton Pose”，对
vertex-only / morph-only / physics-only Runtime 不成立。修正：

```cpp
virtual Pose* TryGetPose() noexcept = 0;
virtual const Pose* TryGetPose() const noexcept = 0;

// convenience，建立在 TryGetPose() != nullptr 之上
Pose& GetPose();
const Pose& GetPose() const;
```

`ProduceFrameView()` 使用 `TryGetPose()`，允许 `pose == nullptr`。
`Entity::TryGetPose()` 改为转发 `TryGetPose()`（当前直接 `&runtime->GetPose()`，
无 Pose 的 Runtime 会误报存在）。

### 5.2 Optional MorphState

```cpp
virtual MorphState* TryGetMorphState() noexcept { return nullptr; }
virtual const MorphState* TryGetMorphState() const noexcept { return nullptr; }
```

- Saba：`nullptr`（Saba 内部自管 morph，不暴露 WISTERIA MorphState）；
- WisteriaGeneric：`&ownedMorphState`；
- `Entity::TryGetMorphState()` 增加 Runtime 转发，fallback 到 legacy
  Entity morphState；Renderer 继续只问“这个 Entity 当前有什么可渲染状态”，
  架构不改。

### 5.3 Capabilities 诚实

Generic Runtime 的 `Capabilities()` 如实返回
`physics = false`、`checkpoint = false`。R1.5 不为 Generic Runtime 增加：

```text
Checkpoint / ReplayFromCheckpoint
Deterministic Exact Frame
Physics Snapshot / Restore
Stable C ABI
```

## 6. Animator / Pose / MorphState 所有权（冻结）

```text
ModelInstance
└─ WisteriaGenericRuntimeDriver
   ├─ Pose
   ├─ MorphState
   └─ Animator
```

- Runtime 从 `ModelAsset` immutable data 初始化，不重新读文件；
- `Animator(Pose&, MorphState*)` 的所有指针均在 Runtime 内部，无跨 owner
  指针；
- ModelInstance 不直接拥有 Pose/MorphState（与 Saba Runtime owns Pose 对称）。

## 7. RootMotion single-consumer（冻结）

RootMotion 是 Update A→B 的 delta，不是 frame B 的持久状态；必须恰好被
消费一次。契约：

```cpp
virtual RootMotionDelta ConsumeRootMotion() noexcept { return {}; }
```

```text
Runtime::Update()
  ↓ 产生 RootMotionDelta
ModelInstance::Update(dt) 在同一 Update 中返回并消费一次
  ↓
Entity 应用 Transform.ApplyLocalMotion(rootMotion)
```

Runtime 不得持有 `Entity*` / `Transform*`。`ModelInstance::Update` 签名
从 `void` 改为返回 `RootMotionDelta`。

## 8. Scene 双路径删除（冻结目标）

Phase 0D 后，`Scene::InstantiateModel` 统一为：

```text
auto runtime = modelBackends.CreateRuntime(model, options);   // Static → nullptr
auto instance = ModelInstance(model, std::move(runtime));
entity.SetModelInstance(...);
for (part : model.Parts())
    entity.AddRenderPart(..., part.MorphMaterialIndex());     // 永远保留
```

- `backendDriven ? std::nullopt : part.MorphMaterialIndex()` 删除；
  asset 语义不被篡改（`EvaluateMaterialMorphs(part, nullptr)` 天然返回
  base material），并为 R1.6 修复 Saba material morph 保留正确数据；
- `if (!backendDriven) { SetMorphSet / SetSkeleton / Animator.Play }`
  删除：Generic 模型走 WisteriaGenericRuntimeDriver，Static 无 runtime。

## 9. Phase 计划与验收

### Phase 0A — Contract Frozen（本文档）

### Phase 0B — Procedural Canary

test-only `ProceduralTestBackend`，复用 `ModelBackendKind::WisteriaGeneric`
（不新增 backend kind）。至少两类：

```text
vertex-only / no-Pose runtime（三角形顶点 sin(t)）
1-bone Pose runtime（每帧旋转）
```

验证链：`Registry → Runtime → ModelInstance → Entity → Snapshot`；
Renderer smoke 另外做。顺序上 Snapshot 是 Runtime/ModelInstance 输出，
不依赖 Renderer。

### Phase 0C — WisteriaGenericRuntimeDriver

迁入 Pose/MorphState/Animator，fixture 拆分：

```text
Generic skeletal fixture  animated_triangle.gltf（Pose/timeline/multi-instance/snapshot）
Generic morph fixture     极小 procedural MorphSet（MorphState ownership / Renderer / MorphSnapshot）
Root motion               复用 Animator root-motion 单元数据
```

迁移等价：迁移前后 frame 0/.25/.5/1.0 的 Pose、Morph weights、RootMotion、
render output 一致。

### Phase 0D — Scene Debt Removal

见 §8。

### Phase 0E — Final Validation

```text
Static asset:                    Runtime == nullptr
Procedural vertex runtime:       no Pose、geometry 变化、multi-instance 独立
Procedural skeletal runtime:     Pose 变化、snapshot 可用
Generic GLTF:                    animated_triangle 迁移等价
Generic Morph:                   weights + renderer + snapshot
RootMotion:                      exactly-once 应用
Saba:                            R1.2–R1.4 矩阵不变
Renderer:                        0 Saba 类型、0 WisteriaGeneric 类型
四套矩阵：                       全绿
```

## 10. R1.6 Debt Ledger（只记录，不在 R1.5 处理）

```text
1. Framebuffer move 丢 GraphicsDevice*（moved-to 退化为直接 glDeleteFramebuffers）
2. FlushPendingDeletes() 未在 glfwMakeContextCurrent 后自动调用
3. SceneFramebuffer::Release() 的 colorTexture/depthRenderbuffer 直接
   glDelete*，不走 GraphicsDevice
4. Saba UV/Material morph 视觉断链（VertexFrame 无 UV +
   backendDriven 丢弃 MorphState/MorphMaterialIndex）
5. Offline Output（PresentationFrame、OffscreenRenderTarget、RGBA8
   readback、Camera/Light authority、frame sequence、manifest、
   checkpoint resume）
```

## 11. 明确不做

```text
- Generic Runtime 的 checkpoint / replay / deterministic exact frame /
  physics snapshot / Stable C ABI（见 §5.3）
- 彻底删除 standalone Entity 的 Animator/Pose/MorphState 接口
- 任何 renderer 重构（Renderer 只改取数来源，不引入 backend 类型）
- 性能优化（SimpleBroadphase 等，留给 R1.5 末尾 benchmark 后再定）
```
