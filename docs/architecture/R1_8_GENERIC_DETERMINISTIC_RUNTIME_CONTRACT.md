# R1.8 — Generic Deterministic Runtime（契约草案）

> 状态：**DRAFT v0.1（2026-08-09，待用户审查冻结）**
> 前置：R1.7 Phase 0A–0D CLOSED，0E native-Linux gate 进行中；
> R1.6 确定性/checkpoint 基础设施已冻结。
> 方向：把 Saba MMD 特有能力（exact step / checkpoint / restore / replay）
> 升格为 WISTERIA Runtime 标准能力。

## 1. 一句话

R1.8 让 `WisteriaGenericRuntimeDriver` 获得与 Saba 同级的确定性契约：
`PrepareFrameZero / StepMotionFrameExact / snapshot-restore / checkpoint /
replay`，并通过 capability 查询由引擎统一编排，最终让
`OfflineFrameSequence` 不再依赖 `MmdRuntimeModel` 具体类型。

```text
IModelRuntimeDriver
  ├─ SabaMmdRuntimeModel      exact step ✅ checkpoint ✅（现状）
  └─ WisteriaGenericRuntime   exact step ❌ checkpoint ❌（现状）
        ↓ R1.8
IDeterministicFrameStepper（引擎级标准）
  ├─ Saba MMD
  └─ Generic（动画/Morph，无物理）
```

## 2. 现状盘点（2026-08-09）

```text
Saba：
  IDeterministicFrameStepper（PrepareFrameZero / StepMotionFrameExact）✅
  PhysicsSnapshot capture/restore ✅
  Checkpoint wire（R1.4 envelope + payload kind 1 = MMD R12C）✅
  Capabilities 全量上报 ✅

Generic：
  IModelRuntimeDriver 基础通道（Pose / MorphState / Animator / root motion）✅
  IDeterministicFrameStepper ❌（未实现）
  snapshot / checkpoint / restore / replay ❌
  Capabilities 全部默认 false ✅（诚实，但能力缺失）

编排层：
  OfflineFrameSequence 构造签名硬编码 MmdRuntimeModel& —— 非通用
  ModelInstance::PublishCurrentRuntimeFrame 已经后端无关 ✅
  Checkpoint envelope（wire version / build compatibility）后端无关 ✅
  payload 编解码目前只有 MMD R12C 一种
```

## 3. R1.8 范围

### 3.1 必须交付

```text
1. Generic deterministic timeline：
   PrepareFrameZero / StepMotionFrameExact（无物理，纯动画/Morph）
2. Generic 帧域冻结（推荐 30Hz canonical，见决策 1）
3. Generic snapshot / restore：
   animator 状态（active clip、时间、循环语义）+ morph override
   + pending root motion + frame index
4. Checkpoint payload kind 2（Generic R1.8），复用 R1.4 envelope；
   Saba R12C payload 不动
5. Capability 查询扩展：
   supportsExactFrameStepping / supportsDeterministicReplay
   （ModelRuntimeCapabilities 增加确定性域，C ABI 不受影响）
6. OfflineFrameSequence 运行时无关化：
   接受 IModelRuntimeDriver& + capability 门控（见决策 3）
7. 测试：
   Generic exact-step 等价矩阵（两实例逐步一致）
   Generic checkpoint 往返 + restore + replay
   Generic 序列 RenderRange/Resume（零窗口 session）
   capability 门控（不支持的后端明确拒绝）
8. 四矩阵验证 + Final Closure
```

### 3.2 明确不做（R1.8 边界）

```text
Generic 物理（Bullet）快照 —— Generic 无物理，不造物理
统一后端无关 checkpoint 数据模型（Saba 保留 R12C）
Stable Runtime/Render C ABI（R1.9）
VRM / 新后端
RenderDevice / RenderGraph / Vulkan（R2.x）
```

## 4. 关键设计草案

### 4.1 Generic 帧域（决策 1）

```text
推荐：冻结 30Hz canonical（MotionFrameIndex = 30Hz 绝对帧）
  PrepareFrameZero       → 帧 0：active clip t=0（或 bind pose）
  StepMotionFrameExact(N) → 求值 animator 于绝对时间 N/30
  clip 循环：按 clip loop 语义（loop → wrap；不 loop → clamp end）
  root motion：每个 exact step 边界消费一次（不跨步累积）
  无 physics tick；ReplayConfig.physicsHz 忽略但必须为 120 或默认

备选：clip-native fps（每个 clip 自带帧率）
  —— 更贴近 glTF 语义，但与 R1.2A 冻结的 30Hz 主循环不一致
```

### 4.2 Generic checkpoint payload（决策 2）

```text
CheckpointPayloadKindGenericR18 = 2
CheckpointPayloadSchemaGenericR18 = 1
CheckpointBackendIdWisteriaGeneric = 2

payload 字段（草案）：
  frame（MotionFrameIndex）
  activeClipIndex（或 none）
  animatorTimeSeconds（double，canonical = frame / motionFps 或其 wrap）
  morphOverrides（sorted，复用 UserOverrideState 排序契约）
  pendingRootMotion（linear + angular）
  assetFingerprint（mesh/clip 哈希 + build compatibility id）

Saba R12C payload（kind 1）序列化/反序列化保持不变。
```

### 4.3 确定性 capability（决策 5 附录）

```cpp
struct DeterministicBackendCapabilities
{
    bool supportsExactFrameStepping = false;
    bool supportsCheckpointCapture = false;   // 从 checkpoint 域迁入
    bool supportsCheckpointRestore = false;
    bool supportsReplayFromCheckpoint = false;
};
struct ModelRuntimeCapabilities
{
    PhysicsBackendCapabilities physics;
    DeterministicBackendCapabilities deterministic;  // 新增
    // CheckpointBackendCapabilities 保留为兼容别名/迁移期字段
};
```

### 4.4 OfflineFrameSequence 通用化（决策 3）

```text
现状：OfflineFrameSequence(scene, renderer, MmdRuntimeModel&, instance, cfg)
目标：OfflineFrameSequence(scene, renderer, IModelRuntimeDriver&, instance, cfg)
      + capability 门控：
        supportsExactFrameStepping == false → 构造/运行明确失败
      + checkpoint 路径按 payload kind 分派

不改：Renderer、SceneFramebuffer、PNG/manifest 事务、
      PublishCurrentRuntimeFrame 语义
```

## 5. 阶段计划

```text
Phase 0A  契约（本文档）——冻结帧域、checkpoint 形态、sequence 通用化
Phase 0B  Generic PrepareFrameZero / StepMotionFrameExact + capability
Phase 0C  Generic snapshot/restore + checkpoint payload kind 2
Phase 0D  OfflineFrameSequence 运行时无关化 + 零窗口 Generic 序列
Phase 0E  四矩阵 + Final Closure
```

## 6. 成功标准

```text
1. Generic 两实例从 start 逐步 exact-step 到 N，pose/morph hash 一致
2. Generic checkpoint 序列化 → 反序列化 → restore → 继续逐步，
   与 from-start 完全一致（同环境）
3. OfflineFrameSequence 用 Generic 运行时跑 RenderRange/Resume，
   PNG/manifest/checkpoint 全部落盘（headless session）
4. capability 门控：不支持 exact step 的后端明确拒绝 sequence
5. Saba R1.2C checkpoint 回归零破坏（payload kind 1 不变）
6. 四矩阵全绿
```

## 7. 开放决策（需要用户拍板）

```text
1. Generic 帧域：30Hz canonical（推荐）还是 clip-native fps？
2. Checkpoint 形态：新增 payload kind 2（推荐）还是统一后端无关模型？
3. OfflineFrameSequence：改为 IModelRuntimeDriver + capability 门控
   （推荐）还是保留 MmdRuntimeModel 签名 + 新增 Generic 并行类？
4. exact step 时间原点：绝对时间 N/30 + clip 循环语义（推荐）
   还是每次从 clip 0 重放？
5. root motion：exact step 边界消费一次并纳入 checkpoint（推荐）
   还是 exact step 不消费 root motion？
```

