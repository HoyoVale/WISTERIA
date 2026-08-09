# R1.8 — Generic Deterministic Runtime（契约草案）

> 状态：**FROZEN v1.0（2026-08-09，五项决策 + deterministic subset 已拍板）；
> Phase 0A CLOSED**
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

### 4.1 Generic 帧域（决策 1，已冻结）

```text
冻结：30Hz canonical（MotionFrameIndex = WISTERIA 确定性编排坐标）
  PrepareFrameZero       → 帧 0：active clip t=0（或 bind pose）
  StepMotionFrameExact(N) → 求值 animator 于绝对时间 N/30
  clip 循环：按 clip loop 语义（loop → wrap；不 loop → clamp end）
  30Hz 是编排时钟，不是要求源 clip 重采样成 30fps；
  AnimationClip 保持连续秒时间域，在 N/30 处采样
  无 physics tick；ReplayConfig.physicsHz 忽略但必须为 120 或默认

Generic 确定性帧域上限（float 时间精度边界）：
  MotionFrameIndex <= 2^20（1,048,576 帧 ≈ 9.7 小时 @30Hz），
  保证 N/30 在 float 下仍可分辨相邻帧；超出 → 明确拒绝
```

### 4.2 Generic checkpoint payload（决策 2，已冻结）

```text
CheckpointPayloadKindGenericR18 = 2
CheckpointPayloadSchemaGenericR18 = 1
CheckpointBackendIdWisteriaGeneric = 2

GenericR18Payload v1（0C 冻结精确字节布局）：
  identity
  ├─ asset fingerprint
  ├─ clip identity/index
  └─ deterministic configuration
  timeline
  ├─ MotionFrameIndex
  ├─ canonical time
  ├─ looping
  ├─ playing/paused（v1 = playing，不暂停）
  └─ clip terminal/clamp state
  morph
  └─ sorted user overrides（复用 UserOverrideState 排序契约）
  root motion
  ├─ enabled
  ├─ root bone identity/index
  └─ pending delta
  validation
  └─ reject unsupported Animator transient state

Saba R12C payload（kind 1）序列化/反序列化保持不变。
```

### Phase 0C 范围（已批准）

```text
1. GenericRuntimeCheckpoint 中性状态（timeline / morph overrides /
   root motion 配置 + pending delta / asset fingerprint）
2. IDeterministicCheckpoint 后端无关接口
   （Saba 保留 R1.2C FrameCheckpoint 路径，0D 按 capability 分派）
3. payload kind 2 codec：复用 R1.4 envelope（EnvelopeWriter /
   ReadEnvelopeHeader 抽取），MMD R12C 字节布局不变
4. Generic CreateCheckpoint / RestoreCheckpoint / ReplayFromCheckpoint：
   subset gate、帧域、fingerprint、canonicalTime 一致性校验
5. 持久 morph override：IModelRuntimeDriver 新增默认 false 接口；
   Generic 每次 exact step / Update 后重放
6. capability：deterministic checkpoint 三比特 + 镜像打开
7. 测试：round trip + restore/replay 与 from-start 等价；
   wire 篡改/截断/build 身份/语义拒绝；out-of-subset capture 拒绝
```

### 4.3 确定性 capability（已冻结）

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
    DeterministicBackendCapabilities deterministic;  // 新增，authoritative
    // 迁移期镜像：checkpoint 域只 mirror deterministic，禁止双真相源
    CheckpointBackendCapabilities checkpoint;
};
```

规则：`deterministic` 是唯一 authoritative source；
旧 `checkpoint` 域只是镜像，出现不一致视为 backend contract violation。

### 4.4 OfflineFrameSequence 通用化（决策 3，已冻结）

```text
现状：OfflineFrameSequence(scene, renderer, MmdRuntimeModel&, instance, cfg)
目标：OfflineFrameSequence(scene, renderer, IModelRuntimeDriver&, instance, cfg)
      + capability 门控：
        supportsExactFrameStepping == false → 构造/运行明确失败
      + checkpoint 路径按 payload kind 分派

不改：Renderer、SceneFramebuffer、PNG/manifest 事务、
      PublishCurrentRuntimeFrame 语义
```

### 4.5 Root motion（决策 5，已冻结）

```text
每个 exact frame boundary 产生恰好一个 deterministic root-motion delta；
delta 进入 runtime pending state（StepMotionFrameExact 内部不消费），
由编排层消费至多一次；pending root-motion 状态 + 配置属于 checkpoint。

PrepareFrameZero        → pendingRootMotion = identity
Frame N > 0             → delta 来自 canonical interval [(N-1)/30, N/30]
                          绝不依赖"上一次实际调用时的 Animator time"
loop 跨界（29→30）      → 按 clip loop 语义计算跨界 delta

同一资产 + 同一 deterministic config + 同一 checkpoint state + 同一 N
→ 同一 evaluated terminal runtime state（坐标式求值，非累积模拟）
```

### 4.6 Generic Deterministic Mode v1 subset（额外冻结）

```text
SUPPORTED：
  - single active AnimationClip
  - canonical 30Hz exact timeline
  - loop / non-loop
  - pose
  - animation-driven morph
  - user morph overrides
  - root-motion configuration + pending delta
  - checkpoint / restore / replay（0C 起）

NOT SUPPORTED（进入 sequence/checkpoint 时检测到 → 明确
UnsupportedDeterministicState，绝不悄悄丢状态）：
  - active CrossFade / transition
  - active AnimationStateMachine（states/transitions 非空）
  - trigger-in-flight / float/bool parameters 非空
  - speed != 1.0
  - paused
  - MMD IK overrides 非空
  - 其他 payload schema 未捕获的可变 Animator 状态
```

## 5. 阶段计划

```text
Phase 0A  契约（本文档）——五项决策 + subset 冻结          ✅ CLOSED
Phase 0B  Generic PrepareFrameZero / StepMotionFrameExact + capability
          ✅ CLOSED
Phase 0C  Generic snapshot/restore + checkpoint payload kind 2          ✅ CLOSED
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

## 7. 已拍板决策（2026-08-09）

```text
1. Generic 帧域 = canonical 30Hz（编排坐标）；源 clip 保持连续时间域
2. Checkpoint = R1.4 envelope + payload kind 2；MMD R12C 保持兼容
3. OfflineFrameSequence = IModelRuntimeDriver& + capability/interface 门控；
   禁止 Generic 并行序列类
4. StepMotionFrameExact(N) = 绝对边界 N/30，禁止每步从 0 重放
5. 每个 exact boundary 一个 deterministic root delta → pending state →
   编排层消费至多一次；checkpoint 保存 pending + 配置
6. Generic deterministic v1 subset（见 4.6）；
   不支持状态显式失败，禁止部分 checkpoint
```
