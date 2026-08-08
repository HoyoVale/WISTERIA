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
Imported asset:
  PMX → SabaMmd
  otherwise inspect imported result:
    Skeleton || Morph || AnimationClip → WisteriaGeneric
    otherwise → Static

Programmatic / procedural asset:
  may explicitly assign BackendKind before exposure;
  explicit assignment bypasses automatic imported-asset classification.
```

一个完全静态的 GLB 仍必须是 `Static`；`animated_triangle.gltf` 因带
Skeleton + AnimationClip 而进入 `WisteriaGeneric`。

**`ModelAsset::backendKind` 是唯一权威 backend identity。**
`ModelSourceDescriptor` 只描述 source；过渡期即使其旧 `backend` 字段
仍存在，`BackendKind()` 也**不得**再以它为权威来源，否则会出现两个
backend identity 不一致。

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
`sourceDescriptor->backend` 的寄生关系在 Phase 0C 移除；在此之前，
显式赋值的 `backendKind` 优先于任何自动分类（见 §3 优先级）。

## 5. Runtime contract 修正（冻结）

### 5.1 Optional Pose（最重要）

当前 `IModelRuntimeDriver::GetPose()` 是纯虚且 `ProduceFrameView()` 默认
强制 `&GetPose()`——这隐含“所有 Runtime 必须有 Skeleton Pose”，对
vertex-only / morph-only / physics-only Runtime 不成立。修正：

```cpp
virtual Pose* TryGetPose() noexcept = 0;
virtual const Pose* TryGetPose() const noexcept = 0;

Pose& GetPose();          // convenience；TryGetPose() == nullptr 时
const Pose& GetPose() const;  // 抛 std::logic_error（已定义行为，非 UB）
```

`ProduceFrameView()` 使用 `TryGetPose()`，允许 `pose == nullptr`。
`Entity::TryGetPose()` 改为转发 `TryGetPose()`（当前直接 `&runtime->GetPose()`，
无 Pose 的 Runtime 会误报存在）。

### 5.2 Optional MorphState（含 neutral snapshot bridge）

```cpp
virtual MorphState* TryGetMorphState() noexcept { return nullptr; }
virtual const MorphState* TryGetMorphState() const noexcept { return nullptr; }
```

- Saba：`nullptr`（Saba 内部自管 morph，不暴露 WISTERIA MorphState）；
- WisteriaGeneric：`&ownedMorphState`；
- `Entity::TryGetMorphState()` 增加 Runtime 转发，fallback 到 legacy
  Entity morphState；Renderer 继续只问“这个 Entity 当前有什么可渲染状态”，
  架构不改。

`TryGetMorphState()` 只服务 Renderer / Entity API。**neutral
`ModelFrameSnapshot` 仍由 `MorphCount() / DescribeMorph() /
ReadMorphState() / MorphRevision()` 驱动**：WisteriaGenericRuntimeDriver
必须把 owned MorphState 桥接到这四个 neutral 接口，
`ModelInstance::CaptureMorphs()` 不得出现
`if GenericRuntime ...` 之类的 backend 特判。

### 5.3 Optional Animator

```cpp
virtual Animator* TryGetAnimator() noexcept { return nullptr; }
virtual const Animator* TryGetAnimator() const noexcept { return nullptr; }
```

- Saba：`nullptr`；
- WisteriaGeneric：`&ownedAnimator`；
- `Entity::HasAnimator() / TryGetAnimator() / GetAnimator()` 与 Pose/Morph
  一致：Runtime first → fallback legacy standalone Entity animator。

**迁移默认播放必须继承**：`Scene::InstantiateModel()` 旧分支对带
AnimationClip 的模型自动 `Animator.Play(AnimationClipAt(0))`；删除旧分支后，
`WisteriaGenericRuntimeDriver` 初始化必须继承该默认行为：

```text
if Animator exists && AnimationClipCount > 0
    Play(AnimationClipAt(0))
```

否则 `animated_triangle.gltf` 迁移后 Runtime 存在却不默认播放，“迁移等价”
会失败。

### 5.4 Snapshot optional channels

`CaptureSnapshot(All)` 不代表每个 channel 都必须存在：

```text
no Pose runtime:
  snapshot.metadata.valid = true
  snapshot.pose.captured = false
  no exception

no morph runtime:
  snapshot.morphs.captured = false

no CPU-deformed geometry:
  snapshot.geometry.captured = false
```

Snapshot channel 是 optional capability，不是所有 Runtime 必须填满的固定
结构。

### 5.5 Capabilities 与 CreationOptions 语义

Generic Runtime 的 `Capabilities()` 冻结为精确字段：

```text
所有 PhysicsBackendCapabilities 字段     = false
所有 CheckpointBackendCapabilities 字段  = false
PhysicsInfo.available                   = false
PhysicsInfo.ownsSimulationStep          = false
```

`Reset()` 仍是 Runtime 基础接口，不等于 physics `supportsReset`。

`RuntimeCreationOptions` 对 Generic backend 的语义冻结：

```text
WisteriaGeneric 不解释 RuntimeCompatibilityProfile 与
RuntimePhysicsSettings；改变这些不受支持字段的合法取值，
不得改变 Generic Runtime 行为。
```

R1.5 不为 Generic Runtime 增加：

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
   ├─ optional Pose
   ├─ optional MorphState
   └─ optional Animator
```

存在性规则：

```text
HasSkeleton → Pose exists
HasMorphs   → MorphState exists
Pose exists → Animator may exist
```

R1.5 不为 morph-only 模型制造 dummy Skeleton/Pose。若资产有 morph
animation clip 但没有 Skeleton，而当前 Animator 无法 pose-less playback，
则 **R1.5 不要求支持该组合；`Initialize()` 必须诚实拒绝，而不是伪造
Pose**（Animator 泛化留待后续）。

- Runtime 从 `ModelAsset` immutable data 初始化，不重新读文件；
- `Animator(Pose&, MorphState*)` 的所有指针均在 Runtime 内部，无跨 owner
  指针；
- ModelInstance 不直接拥有 Pose/MorphState/Animator
  （与 Saba Runtime owns Pose 对称）。
- 初始化继承旧 Scene 路径的默认播放行为（§5.3）。

## 7. RootMotion single-consumer（冻结）

RootMotion 是 Update A→B 的 delta，不是 frame B 的持久状态；必须恰好被
消费一次。契约：

```cpp
virtual RootMotionDelta ConsumeRootMotion() noexcept { return {}; }
```

```text
ConsumeRootMotion() → 返回 pending delta，并原子清空
Reset()             → pending root motion 变为 identity
RootMotion 不属于 ModelFrameView / ModelFrameSnapshot
```

```text
Runtime::Update()
  ↓ 产生 RootMotionDelta
ModelInstance::Update(dt) 固定顺序：
  runtime.Update(dt)
  → publish current frame view / metadata
  → ConsumeRootMotion()
  → return delta
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

Phase 0B canary 验收（optional channel 语义直接验证）：

```text
Procedural vertex-only runtime:
  TryGetPose() == nullptr
  TryGetMorphState() == nullptr
  TryGetAnimator() == nullptr
  CaptureSnapshot(All):
    metadata.valid == true
    pose.captured == false
    morphs.captured == false
    geometry.captured == true
  geometry changes with time
  two instances independent

Procedural 1-bone runtime:
  TryGetPose() != nullptr
  TryGetAnimator() != nullptr
  pose.captured == true
  geometry may be absent

Generic Morph:
  TryGetMorphState() != nullptr
  MorphCount/Describe/Read/Revision 与 owned MorphState 一致
  MorphSnapshot captured
  Renderer 看到同一个 MorphState
```

迁移等价中的“render output 一致”措辞冻结为：

```text
renderer-visible state / existing render smoke remains equivalent
```

R1.5 不引入 pixel-exact render 等价概念（顺延 R1.6）。

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
