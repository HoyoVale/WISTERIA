# R1.4 — Stable Runtime Boundary 契约

> 状态：**Phase 0A Contract Frozen（2026-08-07）**。R1.3 Phase 0A/B
> 完成（2026-08-07）之后的主线。
>
> Entry Audit（2026-08-07）结论：R1.2/R1.3 留下的内部基础足够好，
> 可进入 R1.4；但**不能把现有 C ABI v0.7 与 FrameCheckpoint 内存结构
> 原样冻结成 v1**。本契约在冻结前必须先解决两个 P0 边界问题，并把
> 稳定性要求写入契约。

## 0. 阶段定位

```text
R1.2：确定性时间线与状态所有权（完成）
R1.3 Phase 0A/B：MMD 物理兼容与后端治理 / 社区对照（完成）
R1.4：稳定 Runtime 接口、序列化与外部集成边界
      （本契约：Checkpoint 序列化、C ABI、帧转换边界）
```

R1.4 Phase 0A 一句话定义：

> 在现有 opaque-handle / Context / InvokeAbi 底座上，冻结一个明确的
> **Stable Runtime v1 subset**（runtime identity、capabilities、
> deterministic frame、checkpoint、serialization），并保证未来的
> MMD_COMMUNITY v2 等有效行为变化不需要破坏 ABI。

## 1. Entry Audit 结论（继承与禁止）

### 继承（KEEP，不重新发明）

```text
opaque uint64 handles + 全局单调句柄空间 + destroyed 不复用
Context ownership（shared_ptr lease 防止调用中销毁）
Scene/Window 父级销毁级联
InvokeAbi 统一 C++ exception barrier
lastError[512] 固定容量
ABI safety matrix（CTest 门禁）
R1.2 checkpoint 语义 / Cold deterministic timeline
Capabilities 架构（ModelRuntimeCapabilities）
```

### 禁止原样冻结（Must NOT freeze as-is）

```text
整个 native v0.7 surface（94 exports，含 Window/Input/Light 等）
现有未版本化 C struct（如 WisteriaPhysicsPreset 只有 reserved[]）
double MotionFrame 作为 deterministic API
FrameCheckpoint C++ 内存布局（vector/string/pair/bool/glm）
GLM / STL / C++ bool 作为 wire layout
当前有限的 WisteriaStatus（装不下 TimelineStatus）
人类可读 BackendName 作为唯一 identity
```

## 2. 四件事必须先冻结

### A. Stable C ABI v1 subset

```text
稳定面只包含：
  runtime identity / capabilities
  deterministic frame（preview 与 exact 分开）
  checkpoint（create / restore / destroy / serialize / deserialize）
  serialization 边界
  runtime creation options（Initialize 前语义选择）

保持 experimental：
  Window / Input / Camera / Render / Light / Primitive /
  Legacy Model / Entity runtime 等 v0.7 既有面
  （继续可用，但不承诺 ABI 永久不变）

struct 规则：
  每个可扩展 public struct 固定：
    uint32_t struct_size;
    uint32_t struct_version;
  公开 enum 禁止依赖 C/C++ enum ABI size：
    status / capability / backend id 使用明确宽度整数 + 固定数值常量

版本分层（四层分离）：
  C ABI version
  Checkpoint Wire version
  Checkpoint Payload schema
  Deterministic Profile version
  （backend/config fingerprint 是另一回事）
```

### B. Runtime creation / pre-init options（P0 边界）

```text
现状：Scene 创建路径 InstantiateModel → CreateRuntime → Initialize
      → 才交给 Entity；调用者没有机会在 Initialize 前选择
      effective behavior 不同的 Profile（如未来 COMMUNITY v2）。

R1.4 冻结的创建顺序：
  InstantiateModel
    + RuntimeCreationOptions
        ↓
  IModelBackend::CreateRuntime(asset, options)
        ↓
  apply authoritative runtime configuration（Initialize 前）
        ↓
  Initialize()
        ↓
  返回 Runtime

要求：
  - options 必须包含 authoritative MmdPhysicsConfiguration（或等价
    中立配置）与 backends 选择；
  - 任何 future COMMUNITY v2 的 checkpoint restore 必须先创建
    COMMUNITY v2 Runtime 再 restore；Checkpoint 不偷偷重建
    backend configuration；
  - 具体 struct 设计在 Phase 0A Contract 冻结，但创建顺序必须现在
    写进契约。

范围限制（拍板：APPROVED WITH SCOPE LIMIT）：
  RuntimeCreationOptionsV1 只暴露 WISTERIA 治理的稳定语义：
    semantic preset（MMD_RAW / MMD_COMMUNITY / WISTERIA_ADAPTIVE）
    stable runtime physics settings（fixedTimeStep / maxSubSteps /
      gravity / enabled）
    reserved / flags
  不镜像内部 MmdPhysicsConfiguration（originPreset、诊断字段、
    adaptive 预留、backend/baseline 字符串等都不是 stable user
    semantics）；
  **RuntimeCreationOptions selects WISTERIA-governed semantic
    profiles, not concrete backend implementations.**
  backend identity 是查询与 checkpoint compatibility identity，
  不是上层选择物理实现的控制旋钮（Saba executes; WISTERIA governs）。
```

### C. Checkpoint wire envelope + payload schema

```text
WISTERIA Checkpoint Envelope
│
├─ wire format identity（wire version）
├─ build/runtime compatibility identity
├─ backend identity
├─ deterministic profile identity
├─ payload kind
├─ payload size
├─ checksum
│
└─ payload：
     MMD R1.2C Checkpoint v1
     （未来 glTF / VRM backend 使用新的 payload kind）

Wire schema 规则：
  little-endian fixed integers
  IEEE-754 float raw bits（含 +0.0 / -0.0 区分，禁止 decimal text）
  UTF-8 byte string + explicit length
  bool → uint8 0/1
  position → 3 explicit float32
  basis → 9 explicit float32
  禁止 JSON 作为 canonical checkpoint 格式

完整性：
  wire 全部字段编码 → 全部解码 → 交给现有 ValidateCheckpointStatic()
  （不得“读取一个字段替另一个自动填值”，否则绕掉 tamper 检测）

反序列化把 byte stream 当不可信输入：
  validate size → validate count → checked arithmetic → bounds check
  → 才 allocate；禁止 read(count) 后直接 vector.resize(count)

构建兼容性：
  v1 serialized checkpoint = portable bytes ≠ portable deterministic
  semantics；buildCompatibilityId mismatch → restore 前拒绝（不产生
  任何 mutation）；跨版本放宽需先通过跨版本等价性测试。
  A serialized checkpoint may identify the backend required for
  compatibility, but deserialization/restoration does not grant the
  caller authority to select or substitute that backend.
```

### D. Deterministic frame domain + status / capabilities

```text
Frame domain（Entry Guard 已落地，契约固化）：
  MotionFrameIndex storage = uint64_t；
  两层界限（拍板：MODIFY）：
    StructuralFrameLimit
      = UINT64_MAX / 4（底层防溢出 Guard，本次已实现）
    Backend-advertised ExactDeterministicFrameLimit
      = Saba MMD v1 = 2^24 = 16,777,216（包含本身；
        VMDAnimation::Evaluate(float) 的连续整数精确域）
  frame * 4 必须 checked（禁止溢出）；
  replay 循环禁止出现 ++frame wrap；
  统一 ValidateDeterministicFrameDomain()：
    ResetAtTarget / ReplayFromStart / StepMotionFrameExact /
    ReplayFromCheckpoint / Checkpoint deserialize /
    C ABI deterministic entries 全部走同一入口；
  Stable ABI 不硬编码全引擎统一上限，通过 capability 返回：
    uint64_t max_deterministic_motion_frame;
  （其他 backend 未来可以比 Saba 大）

交互帧与确定性帧分层（禁止合并）：
  entity_set_preview_frame(double)   → interactive（InvalidateBoundary）
  entity_replay_exact(uint64_t)       → deterministic canonical
  entity_prepare_frame_zero()
  entity_step_exact(uint64_t)
  ModelFrameMetadata.motionFrame(double) 仅 realtime/UI 快照，
  不得作为 serialized deterministic identity。

Status 映射：
  现有 C ABI status（OK/INVALID_ARGUMENT/.../INTERNAL）不足以表达
  TimelineStatus（InvalidCheckpoint / UnsupportedReplayProfile /
  InvalidState / NonSequentialFrame / DeterminismViolation /
  SnapshotMismatch / InvalidSnapshot / Poisoned）；
  R1.4 冻结明确 numeric mapping，禁止压成 INTERNAL。

Capabilities 增加（基于实际 Entity → Runtime → Capabilities()）：
  supportsDeterministicExactFrame
  maxDeterministicMotionFrame
  checkpointPayloadKind
  checkpointSerialization
  runtimeBackendId / runtimeBackendVersion
  deterministicProfileId

Threading：
  Context creator-thread-affine；
  所有 stable v1 调用在创建线程上执行；
  headless deterministic runtime 的 thread migration 留待证据后再开放。

Error 语义：
  lastError 是 best-effort sticky diagnostic；
  status code 是权威；
  successful call 不保证清空 lastError；
  lastError 不得被程序逻辑解析（禁止 strstr(lastError, ...) 判断）；
  stable v1 不承诺“每个 non-OK 都解释本次失败”。
```

## 3. P1 待办（serialization release 前）

```text
1. HashFileBytes → status + hash（或显式 hashValid）；
   serialized checkpoint 不允许在 asset identity invalid 时创建；
2. Envelope 内显式 build/backend/profile identity
   （WISTERIA deterministic compatibility revision、Saba/Bullet
     code revision、compiler/build compatibility）；
3. untrusted-byte parser limits（size/count/length 上限）；
4. threading 语义（见 §2D）；
5. C status numeric mapping 冻结；
6. pure-C ABI 验证层：tests/native_abi_c_smoke.c
   （纯 C include + link + create/destroy + struct sizeof/offsetof +
     fixed constant assertions）。
```

## 4. Checkpoint 所有权

```text
Checkpoint 是 Context-owned value object：
  Context └─ map<WisteriaCheckpoint, FrameCheckpoint>

C ABI 概念面：
  checkpoint create（来自 compatible entity）
  checkpoint restore（到 compatible entity）
  checkpoint destroy
  checkpoint serialize to bytes
  checkpoint deserialize from bytes

Checkpoint 不随源 Entity 销毁失效（R1.2 已证明实例间 restore 合法）。
  source Entity destroyed ≠ checkpoint invalid；
  Context destroyed → checkpoint handles invalid（与 opaque-handle
  ownership 一致）。
```

## 5. 推荐执行顺序

```text
1. 本契约评审（冻结 Phase 0A 四件事）
2. Frame domain 公布值确定（Saba float 上限 + checked 常量）
3. RuntimeCreationOptions + IModelBackend::CreateRuntime 改造
4. Stable C ABI v1 subset 头文件（版本化 struct + status/capability 常量）
5. Checkpoint wire envelope + payload v1 序列化/反序列化
6. untrusted parser limits + build identity
7. pure-C smoke 测试 + ABI safety matrix 扩展
8. 四套矩阵 + checkpoint 跨实例/跨进程回归
```

## 6. 拍板裁决（Frozen decisions，2026-08-07）

```text
1. Stable subset
   APPROVED：冻结 runtime identity / capabilities / deterministic
   timeline / checkpoint lifecycle / serialization / runtime creation
   options；Window / Input / Light / legacy Model 等保持 experimental
2. Frame domain
   MODIFY 已采纳：StructuralFrameLimit（UINT64_MAX/4，防溢出 Guard）
   与 ExactDeterministicFrameLimit（Saba v1 = 2^24，含本身）分离；
   capability 返回 max_deterministic_motion_frame，不硬编码全局上限
3. RuntimeCreationOptions
   APPROVED WITH SCOPE LIMIT：只暴露 semantic preset + stable physics
   settings + reserved；不镜像内部配置；不提供 backend 选择旋钮；
   “Saba executes; WISTERIA governs.”
4. Context-owned opaque checkpoint
   APPROVED：WisteriaCheckpoint = uint64_t，Context 持有；
   source Entity 销毁 ≠ checkpoint invalid；Context 销毁 → handle 失效
5. Error 语义
   APPROVED：WisteriaStatus 权威；lastError best-effort sticky、
   成功不清空、禁止程序逻辑解析
6. Threading
   APPROVED：creator-thread-affine；stable v1 调用必须在创建线程；
   未来 headless 迁移通过新 capability/context 类型开放，不改 v1 承诺
```
