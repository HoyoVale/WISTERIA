# R1.2 — 确定性时间线与物理回放契约（终版）

> 状态：**R1.2A 已冻结（2026-08-06）**；**R1.2B 已冻结并实现**；
> **R1.2C 已冻结并实现（2026-08-06）**
> （契约：
> [R1_2B_RESTORE_STATE_CONTRACT.md](R1_2B_RESTORE_STATE_CONTRACT.md)
> v4.1.1；实现与基线见
> [R1_2B_BASELINE_20260806.md](../validation/R1_2B_BASELINE_20260806.md)）；
> R1.2C（Checkpoint）**契约已冻结，实现已完成**（精简编排稿 v2：
> [R1_2C_FRAME_CHECKPOINT_CONTRACT.md](R1_2C_FRAME_CHECKPOINT_CONTRACT.md)）。
> R1.2 目标：让 WISTERIA
> 可以可靠地逐帧导出、seek、回放；相同输入（资产 + 动作 + 参数 + 帧 +
> 固定步配置）产生一致的 Pose/Physics/Vertex 结果。本阶段**不碰 Bullet
> 高级参数**（solver iterations、CCD、margin、damping 等仍属 #5 社区
> 矩阵）。

## 1. 现状问题（已核实）

- `SetMotionFrame(frame)` 只改数字，不立即求值；
- `Update(deltaTime)` 才执行完整相位（Evaluate → Morph → IK → Physics →
  Pose），物理用 `stepSimulation(dt, maxSubSteps, 1/fps)` 固定步；
- **`ResetPhysics()` 内部隐藏一次 `physics->Update(1.0f/60.0f)`**
  （`PMXModel.cpp:202` 起，已核实）——Replay 声称 120Hz 固定步，但
  reset 阶段偷偷跑 1/60，破坏确定性基线；
- 无 checkpoint、无物理快照、无状态 hash。

因此从不同历史状态跳到同一帧结果可能不同，离线导出不可靠。

## 2. 时间模型（整数 Tick，修正量纲）

```cpp
using TimelineTick = std::uint64_t;         // 物理固定步索引
using MotionFrameIndex = std::uint64_t;     // VMD 动作帧索引（主循环单位）

struct ReplayConfig
{
    // R1.2A 严格冻结为 30Hz 动作 / 120Hz 物理。只有
    // motionFps == 30 && physicsHz == 120 被接受；其他配置一律返回
    // UnsupportedReplayProfile。字段保留便于未来扩展，但第一版不启用
    // 任意频率。
    std::uint32_t motionFps = 30;
    std::uint32_t physicsHz = 120;
    // R1.2A 禁用预热：warmupFrames 必须为 0（否则 UnsupportedReplayProfile）。
    // 预热语义（动画是否推进、是否进入 fingerprint）尚未定义，不在确定性
    // 基线中留下未定义参数。
    std::uint32_t warmupFrames = 0;
    // R1.2A 禁用循环：loopMotion 必须为 false（否则 UnsupportedReplayProfile）。
    // 循环语义（周期是否含末关键帧、边界取帧 0 还是末帧）尚未定义，不在
    // 确定性第一版中依赖 Saba 当前可能存在的隐式 fmod 行为。
    bool loopMotion = false;
};
```

关系（默认配置）：

```text
physicsTick = VMD frame × physicsHz / motionFps
1 VMD 帧 = 4 个 120Hz tick；frame 300 = tick 1200
```

内部时间一律用整数。`double frame` 仅作为 C ABI 输入/输出边界；核心
C++ 接口只接受 `MotionFrameIndex`（见 §12）。未来支持任意频率时引入
有理数相位累积器；本版本不做。

**超出动作末帧的行为（R1.2A 唯一规则）**：

```text
目标帧超过动作末帧（且 loopMotion == false）：
动画保持末帧姿态
物理仍按 30Hz 每帧 4 个 1/120 子步推进到目标帧

无 VMD 时：
动画姿态保持初始状态
物理仍按目标帧数量推进
```

`PhysicsSnapshot` 同时记录 `motionFrame` 与 `physicsTick` 两个字段，
避免「第 N 帧」歧义（见 §7）。

## 3. 求值模式：Saba-compatible deterministic replay（冻结，唯一算法）

```text
PrepareFrameZero()：                          // 见 §4.2 顺序
    SetMotionFrame(0)
    → 求值帧 0 的动画/Morph/IK（不推进物理）
    → 同步帧 0 的 Kinematic target
    → ResetCanonical 到该姿态
    → 回写并生成 Pose/Vertex
    → 清空 accumulator/force
    → 得到 Canonical Frame 0

每推进一个 VMD 帧（MotionFrameIndex += 1）：
→ StepMotionFrameExact(frame, config)          // 唯一整帧步进接口
→ 内部完整执行：
    设置动作帧
    → 动画/Morph/IK 求值
    → 同步 Kinematic 刚体
    → 精确 4 个 Bullet 子步
    → Dynamic 刚体回写
    → Pose/Vertex 更新
    → 清空 accumulator/force
```

这是**唯一**参考算法，无「或」。上层永远不直接拼接 Saba 物理相位。
运动学刚体目标保持一个动作帧，不代表「动画和物理均按 120Hz 求值」。
未来更高精度采样（每 tick 采样四分之一 VMD 帧）需拆解 Saba 相位，
不在 R1.2。

**必须先验证**：Bullet `stepSimulation` 是否在每帧稳定执行恰好 4 个
1/120 子步（不依赖浮点累积器偶尔 3/4 变化）。若无法保证，在 vendored
Saba 增加窄接口：

```cpp
UpdateDeterministicMotionFrame(
    MotionFrameIndex frame,
    float motionDelta,               // 1/30
    std::uint32_t exactPhysicsSubsteps  // 4
);
```

由 Saba 内部按现有相位完成一次动画求值，再精确执行四个物理子步。
这不是 Bullet 参数控制层，只是确定性步进接口。

若验证稳定为四步，`StepMotionFrameExact()` 内部直接调用现有完整
`Update(1/30)`；否则调用 vendored 专用精确路径。上层不感知差异。

## 4. SeekPolicy 语义

```cpp
enum class SeekPolicy
{
    PreserveState,        // 保留物理，仅移动动作帧（交互预览）
    ResetAtTarget,        // 到目标帧后规范重置物理姿态
    ReplayFromStart,      // 从帧 0 固定步重放（确定性核心）
    ReplayFromCheckpoint  // 从显式 checkpoint 重放（R1.2C 后才承诺）
};
```

### PreserveState

```text
设 vmdFrame = target
执行一次完整 Update(0)
```

物理保留；同一目标帧在不同历史下结果可能不同。仅交互预览。

### ResetAtTarget

```text
求值到目标帧姿态（动画/Morph/IK，不推进物理）
同步 Kinematic target
ResetCanonical(CanonicalNoStep)   // 只重建状态，不推进模拟
回写并生成 Pose/Vertex
清空 accumulator/force
```

修复：不再使用 saba 自带 `ResetPhysics()`（隐藏 1/60 步）；且**先求值
目标姿态、再规范重置**——重置必须基于目标帧动画姿态，而非调用前旧骨骼
状态。

### ReplayFromStart（确定性，唯一算法）

```text
保存并持续应用调用前的用户覆盖配置（Morph override / IK override /
Physics enabled / Looping / ReplayConfig）
PrepareFrameZero()                              // 帧 0：先动画后重置

for (MotionFrameIndex frame = 1; frame <= targetFrame; ++frame)
    StepMotionFrameExact(frame, config)         // 唯一整帧步进

保留目标帧的已求值 Pose/Morph/Physics/Vertex 输出（不恢复调用前结果）
```

**不恢复调用前的已求值状态**——`EvaluateTick` 的目的就是让模型停留在
目标帧。保存的是「用户覆盖配置」，不是「求值输出」。

### ReplayFromCheckpoint（R1.2C 后才开放）

```text
调用方传入显式 FrameCheckpoint
恢复 checkpoint（含 canonicalization）
从 checkpoint 帧按 ReplayFromStart 重放到 target
```

在证明「恢复 + 清理历史缓存 + 继续重放 == from start」之前，不承诺
确定性等价；先提供 Capture，不承诺 Restore 后的确定性恢复。

## 5. Canonical Frame Boundary（唯一快照/哈希边界）

所有 Snapshot 与 Hash 只在如下边界生成：

```text
动画/Morph/IK 求值完成
→ Kinematic target 已同步
→ 所有固定物理子步完成
→ Dynamic rigid body 已回写骨骼
→ Pose/Vertex 已更新
→ Bullet accumulator 为零
→ forces 已按约定清理
→ contact manifold 与 solver warm-start 已清理（冷边界）
```

**冷边界（Cold Canonical Boundary）是本阶段唯一支持的确定性 Profile**：
R1.2 的每次 `StepMotionFrameExact` 都是冷启动，即该帧开始前清除
contact manifold、接触累计冲量与关节 warm-start。这与 R1.2B 的
`ClearContactManifoldsDeterministic` / `ClearSolverHistoryDeterministic`
一致，也是 `restore → replay == from-start` 等价性成立的前提；否则
from-start 携带 warm-start 而 Restore 后为冷状态，两者在接触帧必然
分叉。保留 warm-start 的连续求解档位（`PreserveAcrossFrames`）是
后续扩展，**不在 R1.2 契约内**；调用方不得假设两档共存。

Tick 0 定义：动画帧 0 已求值、物理已 Canonical Reset、尚未执行任何物理
步。目标 `MotionFrameIndex = N` 表示「从 0 出发完成 N 帧的 30Hz Update
+ 每帧 4 个物理子步后的状态」。

### 循环边界（防 off-by-one）

```text
PrepareFrameZero();                        // 帧 0 求值，无物理步
for (MotionFrameIndex frame = 1; frame <= targetFrame; ++frame)
    StepMotionFrameExact(frame, config);   // 1 次 30Hz 求值 + 4 物理子步
```

`targetFrame = 0` 表示「已求值帧 0，尚未推进物理」，不是「已走 4 子步」。

### 用户覆盖的应用顺序（唯一，用于所有路径）

```text
VMD 动画求值
→ 应用用户 Morph override
→ 展开 Group Morph
→ 应用用户 IK override
→ IK 求解
→ 同步 Kinematic 刚体
→ 物理步进
→ Dynamic 刚体回写
→ Pose/Vertex 更新
```

同一套顺序必须用于：普通 Update、ReplayFromStart、ResetAtTarget、
checkpoint 恢复后的继续重放。若现有 Saba 真实顺序与此不同，以 Saba
现有顺序为准并记录差异，不重新发明。

## 6. 确定性接口分层

接口分层（R1.2A 已实现，类型定义以
`include/wisteria/runtime/determinism.hpp` 为准）：

```text
IDeterministicFrameStepper        PrepareFrameZero + StepMotionFrameExact
IDeterministicPhysicsObservation  CaptureState + ReadStepDiagnostics
IPhysicsStateAccess               RestoreState（R1.2B，见独立契约）
```

`TimelineStatus` 的完整枚举（含 R1.2A 状态机状态与 R1.2B 新增的
`SnapshotMismatch` / `InvalidSnapshot` / `Poisoned`）、`PhysicsSnapshot`、
`PmxRigidBodyMode` 与恢复语义的**唯一规范源**是
[R1_2B_RESTORE_STATE_CONTRACT.md](R1_2B_RESTORE_STATE_CONTRACT.md)；
本文档不再维护 R1.2B 的结构定义，避免两处权威文档漂移。

WISTERIA 定义接口，Saba Adapter 内部使用 Bullet，上层不见任何 `bt*`
类型。

## 7. PhysicsSnapshot（R1.2B）

`PhysicsSnapshot` / `RigidBodySnapshot` 的最终字段（schemaVersion、
layoutFingerprint、`PmxRigidBodyMode`、canonical claim 语义等）以
[R1_2B_RESTORE_STATE_CONTRACT.md](R1_2B_RESTORE_STATE_CONTRACT.md)
§2.5 为唯一规范源。

> 恢复 canonicalization 的旧表述（“能实现的必须执行，拿不到的标
> false”）**已作废**。R1.2B 的 Canonical Restore Sequence、`canonical`
> 可验证定义、失败语义与 R1.2B/R1.2C 边界全部由
> [R1_2B_RESTORE_STATE_CONTRACT.md](R1_2B_RESTORE_STATE_CONTRACT.md)
> 接管。

### R1.2B 与 R1.2C 的 canonical 边界（修订）

```text
R1.2B：定义并实现完整 Canonical Restore Sequence（含接触 manifold 与
       solver warm-start 清理）；Restore Sequence 成功
       → runtimeCanonicalBoundary=true
       → 后续 Capture 输出 snapshot.canonical=true。
R1.2C：不再重新实现 canonicalization；只增加 FrameCheckpoint 值对象、
       DeterminismFingerprint 与 restore→replay == from-start 的等价性
       证明，然后开放 ReplayFromCheckpoint。
```

## 8. 关节与求解历史

- 第一版**不序列化关节配置**（静态约束参数由刚体状态决定）；
- checkpoint 恢复时必须清理所有约束/接触求解历史（warm-start impulse、
  contact manifolds），使求解器从恢复后的刚体状态重新开始；
- 清理是 **R1.2B 硬性前置**：`RebuildCollisionWorldDeterministic` /
  `ClearSolverHistoryDeterministic` 必须满足 R1.2B 契约 §5 Phase 4 的
  语义并通过 T18 验收；**不存在“无法清除就降级承诺”的路径**。

## 9. 确定性 hash（双 hash）

```cpp
struct DeterminismHashes
{
    std::uint64_t exactHash;      // 原始 float 位模式，同构建严格验证
    std::uint64_t canonicalHash;  // 规范化/量化，快速诊断信号
};

// 状态 Hash：只 Hash 对应状态本身，不含输入配置或目标帧。
struct FrameStateHashes
{
    DeterminismHashes pose;
    DeterminismHashes vertex;
    DeterminismHashes physics;
};

// R1.2C 输入 Fingerprint（唯一结构，取代早期 AssetFingerprint 草案）：
// 资产身份 + 执行配置 + 覆盖状态 + 捕获状态 Hash。唯一规范源见
// R1_2C_FRAME_CHECKPOINT_CONTRACT.md §3。
struct AssetIdentity
{
    std::uint64_t pmxFileHash = 0;
    std::uint64_t vmdFileHash = 0;
    bool hasMotion = false;
    std::uint64_t layoutFingerprint = 0;
    std::uint64_t physicsConfigurationFingerprint = 0;
};

struct DeterminismFingerprint
{
    std::uint32_t schemaVersion = 1;
    MotionFrameIndex frame = 0;
    AssetIdentity asset;
    ReplayConfig config;
    UserOverrideState overrides;   // 见 §12
    FrameStateHashes state;
};
```

- `exactHash`：FNV-1a 64 位，little-endian 显式写出，同一平台/同一构建
  重复运行必须完全一致；
- `canonicalHash`：先做 `-0.0 → +0.0`、Quaternion 统一符号（w<0 取反）、
  NaN 规范化/拒绝、按约定精度量化（默认 1e-5）；
- **PhysicsHash 例外**：transform 旋转使用 `rotationBasis` 9 个 float
  分量直接序列化，**不执行 quaternion 转换或符号统一**（R1.2B 起快照
  持有 basis；把 basis 转回 quaternion 再 Hash 会把刚修复的位级问题
  请回来）。其他确实使用 quaternion 的通道（如 Pose）才执行规范化。
- 验收：
  - 同平台同构建重复运行 → exactHash 必须相同；
  - 跨平台 → 逐组件误差必须低于阈值；canonicalHash 作为快速诊断信号，
    不作为唯一硬门禁（量化分界线可能造成 hash 不同）。

Hash 契约补充：

- rigid body 按稳定 index 排序；
- quaternion 归一化和符号统一（仅限非 Physics 通道）；
- Matrix 明确列主序（glm 默认）；
- NaN 拒绝或规范化（同构建内一致）；
- Infinity 规范化；
- Hash schema version；
**状态 Hash 与输入 Fingerprint 完全分离**：

- `PoseHash` 只 Hash Pose；`VertexHash` 只 Hash Vertex；
  `PhysicsHash` 只 Hash Physics——两个不同目标帧若恰好产生相同 Pose，
  PoseHash 相同（正确语义）；
- `DeterminismFingerprint` 才包含资产、动作、配置、目标帧与三个状态
  Hash，用于 checkpoint 兼容校验；
- 恢复 checkpoint 时必须拒绝不同 PMX、不同刚体布局、不同 VMD、不同
  Replay Profile、不同 hash schema。

工具放 `src/mmd/mmd_determinism.cpp`。

## 10. 可重复性验收标准

### 10.1 同输入重复运行

```text
同一资产 + 同一 VMD + 同一参数 + 同一目标帧
ReplayFromStart 两次
→ exactHash（Pose/Vertex/Physics）完全一致
```

### 10.2 Seek 一致性

```text
直达 300 == 分段 150→300（均从帧 0 重放）
```

### 10.3 Checkpoint 一致性（R1.2C 验收）

```text
CreateCheckpoint(150) → ReplayFromCheckpoint(300)
== ReplayFromStart(300)
仅当 canonicalization 完整时可承诺
```

### 10.4 ResetAtTarget 验收（修订：不再用位移阈值）

```text
连续两次 EvaluateTick(300, ResetAtTarget)
→ Pose/Physics/Vertex exactHash 相同

Reset 后：
→ linear/angular velocity 为零
→ total force/torque 为零
→ accumulator 为零
→ snapshot canonical=true
```

视觉位移只作为 fixture 专用断言，不是通用契约（某些 PMX 刚体初始就有
合法穿插或预张力）。

## 11. Core 测试资产

复用现有 core fixture `pmx_physics.pmx`（3 刚体 + 6 关节，已有 Saba
导入对比断言）。测试启动时必须硬断言：

```text
dynamicBodyCount > 0
kinematicBodyCount > 0
jointCount > 0
```

还要验证：经过若干固定步后，至少一个动态刚体的位置或旋转确实变化——
否则 Physics Hash 一致仍可能是假绿。若断言不满足，再创建小型
`deterministic_chain.pmx`。

## 12. 落点接口（核心 C++ 只接受 MotionFrameIndex）

```cpp
// MmdRuntimeModel 新增（Saba 实现，R1.2A 先做 EvaluateTick；
// 内部委托 IDeterministicFrameStepper，不直接编排相位）
virtual TimelineStatus EvaluateTick(
    MotionFrameIndex target,
    SeekPolicy policy,
    const ReplayConfig& config = {}
) = 0;

// R1.2B（语义见 R1_2B_RESTORE_STATE_CONTRACT.md）
virtual TimelineStatus CapturePhysicsSnapshot(
    PhysicsSnapshot& output
) const = 0;
virtual TimelineStatus RestorePhysicsSnapshot(
    const PhysicsSnapshot& snapshot
) = 0;

// R1.2C（语义见 R1_2C_FRAME_CHECKPOINT_CONTRACT.md）
virtual TimelineStatus CreateCheckpoint(
    FrameCheckpoint& output
) const = 0;
virtual TimelineStatus RestoreCheckpoint(
    const FrameCheckpoint& checkpoint
) = 0;
virtual TimelineStatus ReplayFromCheckpoint(
    const FrameCheckpoint& checkpoint,
    MotionFrameIndex target
) = 0;
```

### FrameCheckpoint（R1.2C，只存覆盖配置，不存 VMD 派生状态）

```cpp
struct UserOverrideState
{
    // 稳定排序（UTF-8 名称字典序）、无重复名称；
    // 排序是序列化/Hash 契约的一部分。
    std::vector<std::pair<std::string, float>> morphOverrides;
    std::vector<std::pair<std::string, bool>> ikOverrides;
    bool physicsEnabled = true;
    bool loopMotion = false;
};

struct FrameCheckpoint
{
    MotionFrameIndex frame;
    PhysicsSnapshot physics;
    UserOverrideState overrides;
    ReplayConfig config;
    DeterminismFingerprint fingerprint;
};
```

VMD 派生的 Morph/Pose/Physics 通过 tick 重新求值，不作为覆盖配置恢复。
`DeterminismFingerprint` 唯一结构见 §9 / R1.2C 契约 §3。
`CreateCheckpoint` 捕获当前 canonical 帧（不接收 frame 参数）；失败不
修改 output。`ReplayFromCheckpoint` 内部先 `RestoreCheckpoint`，再从
`frame+1` 推进到 target；`EvaluateTick(ReplayFromCheckpoint)` 仅保留为
预留枚举，不是真实入口。

### C ABI 边界（R1.4 才开放）

```text
double VMD frame → 明确 FrameToTick 转换 → MotionFrameIndex
```

转换规则待 R1.4 定义：fractional frame 是否支持、0.5 帧映射、无法精确
表示时 round/floor/ceil/拒绝、负帧和超长处理。

## 13. 阶段拆分（修订）

### R1.2A：确定性从头重放

- MotionFrameIndex / ReplayConfig（冻结 30/120，整除校验）；
- PreserveState / ResetAtTarget / ReplayFromStart；
- `IDeterministicFrameStepper`（PrepareFrameZero + StepMotionFrameExact）
  ——提前，因为 No-Step Reset 至少需要操作刚体 transform/motion state/
  interpolation/velocity/force/activation/AABB/contact/accumulator，
  已属状态访问；上层不拼接物理相位；
- `IDeterministicPhysicsObservation`（只读 CaptureState +
  ReadStepDiagnostics）——提前到 A，否则 PhysicsHash/运动断言/
  Reset 后速度/力/accumulator 验收无法经中立接口完成；
- loopMotion 必须 false、warmupFrames 必须 0；
- 超出动作末帧：动画保持末帧姿态，物理继续推进；
- 验证 Bullet 每帧稳定 4 子步；必要时 vendored Saba 加
  `UpdateDeterministicMotionFrame`；
- exact/canonical hash；
- 复用 `pmx_physics.pmx` 做无 VMD 物理确定性测试（硬断言动态/运动学/
  关节/运动）；
- **不实现 checkpoint**。

### R1.2B：物理状态访问接口

- `IPhysicsStateAccess`（Restore；Capture 只读版已在 A）；
- 扩展 PhysicsSnapshot（schemaVersion / layoutFingerprint /
  physicsConfigurationFingerprint / `PmxRigidBodyMode` /
  `RigidTransformSnapshot`（position + 3×3 rotationBasis））；
- vendored saba 窄访问接口（先试 public API，缺什么补什么，清单见
  R1.2B 契约 §11）；
- **Restore 不承诺确定性等价**（等价性证明属于 R1.2C）；
- **只接受 canonical=true 快照**；非 canonical 返回 `InvalidSnapshot`；
- **动画前置是硬性条件**：当前动画帧与 FollowBone transforms 必须与
  快照一致，否则返回 `InvalidState`（R1.2B 不恢复动画）；
- **Restore 完成后 `deterministicPrepared=false`**（继续步进属于 R1.2C
  Checkpoint 语义）；
- 失败语义：校验拒绝不修改世界；写入期灾难性失败 → `Poisoned` 状态，
  实例可通过 `PrepareFrameZero` / `EvaluateTick(0, ResetAtTarget)` 重建
  （R1.2B 契约 §8）。

### R1.2C：Checkpoint

- 显式 `FrameCheckpoint` 值对象与唯一 `DeterminismFingerprint`（§9 /
  R1.2C 契约 §3）；
- 公开入口：`CreateCheckpoint(output)` / `RestoreCheckpoint` /
  `ReplayFromCheckpoint(checkpoint, target)`；
- 恢复顺序：只读校验 → 替换 config/overrides → 动画/Morph/IK 求值 →
  重建 Kinematic target → `RestorePhysicsSnapshot`（R1.2B 内部已完成
  after-physics/Pose/Vertex）→ 重算校验 Hash → `prepared=true`；
- off-by-one：物理已完成 N 帧，`expectedNextFrame=N+1`；`target<N` 拒绝、
  `target==N` 零步、`target==UINT64_MAX` 拒绝；
- **非法 target 在 Restore 前拒绝**：静态校验 → target 范围/溢出 →
  Restore → N+1..target；Restore 成功后重放循环任一 Step 失败 → Poisoned；
- `CreateCheckpoint` 要求 canonical 且三个状态 Hash 全部 valid，
  失败不修改 output；
- 等价性测试必须使用分叉实例 C（不同历史/overrides），禁止“同一实例停在
  N 再继续”的弱测试；
- 证明 restore→replay == from start 后开放
  `ReplayFromCheckpoint` 与三个 checkpoint 能力位。

## 14. 明确不做

- 不改 Bullet 参数（solver/CCD/margin/damping）；
- 不导出 C ABI（R1.4）；
- 不做社区矩阵对照（#5 暂停）；
- 不重写 saba 物理相位；
- 不实现 120Hz 动画采样（保持 Saba-compatible 30Hz）；
- 不实现任意频率 ReplayConfig（仅整数倍）。

## 15. R1.2A 实现与验收记录（2026-08-06）

### 15.1 四子步验证实验（第一步，已完成）

用真实 Bullet 3.25 静态库写探针（`btDiscreteDynamicsWorld` 子类直接读取
protected `m_localTime`），对 `stepSimulation(1/30, maxSubSteps=10,
1/120)` 统计真实执行子步数与帧边界 accumulator：

```text
120 帧：   substeps min=4 max=4 non4=0；nonzero accumulator=0
10000 帧： substeps min=4 max=4 non4=0；nonzero accumulator=0
结论：现有 Saba 相位稳定满足 30Hz→120Hz 精确 4 子步。
```

因此 `StepMotionFrameExact()` 内部直接调用现有完整
`UpdatePhysicsAnimation(1/30)`，**不需要** vendored
`UpdateDeterministicMotionFrame` 专用精确路径。子步数与 accumulator 仍
通过窄接口直接测量，不靠最终 Hash 反推。

### 15.2 Vendored Saba 窄接口（仅确定性所需，不是参数控制面）

- `MMDPhysics::Update(float)` 从 `void` 改为返回 `int`
  （`stepSimulation` 真实执行子步数）；现有调用方可忽略返回值；
- `MMDPhysics` 内部 world 改为
  `DeterministicDynamicsWorld : btDiscreteDynamicsWorld`，新增
  `GetSimulationTime()` / `ResetSimulationTime()`（读写 `m_localTime`）；
- `PMXModel::UpdatePhysicsAnimation` 与 `PMDModel::UpdatePhysicsAnimation`
  返回实际子步数。

未开放 solver/CCD/margin/damping 等任何 Bullet 参数。

### 15.3 新增中立接口与工具

- `include/wisteria/runtime/determinism.hpp`：
  `TimelineTick` / `MotionFrameIndex` / `ReplayConfig` / `SeekPolicy` /
  `TimelineStatus` / `PhysicsStepDiagnostics` / `RigidBodySnapshot` /
  `PhysicsSnapshot` / `IDeterministicFrameStepper` /
  `IDeterministicPhysicsObservation`；
- `include/wisteria/mmd/mmd_determinism.hpp` + `src/mmd/mmd_determinism.cpp`：
  FNV-1a 64 双 hash（exactHash 原始位模式、canonicalHash
  -0→+0/NaN/Inf 规范化/quaternion 符号统一/1e-5 量化），
  `Pose/Vertex/Physics` 状态 hash 与输入分离；
- `MmdRuntimeModel::EvaluateTick(target, policy, config)`；
- `SabaMmdRuntimeModel` 实现 `IDeterministicFrameStepper` 与
  `IDeterministicPhysicsObservation`；
- `Capabilities().physics.supportsSnapshotCapture = true`
  （`supportsSnapshotRestore` / `supportsCanonicalRestore` 仍为 false，Restore
  未开放）。

### 15.4 Canonical Reset（未使用 saba 自带 ResetPhysics）

`ResetCanonicalNoStep()` 按契约顺序执行：

```text
SetActivation(false)（绑定 KinematicMotionState）
ResetTransform()（把 active motion state 重置到当前动画姿态）
从 motion state 读世界变换写入 btRigidBody COM + interpolation transform
清零 interpolation velocity
Reset(physics)（零速度/力 + cleanProxyFromPairs）
ResetSimulationTime()（accumulator = 0）
```

不使用 `PMXModel::ResetPhysics()`，避免其隐藏的
`physics->Update(1/60)` 破坏确定性基线。

### 15.5 用户 Morph override 持久化

`SetMorphWeight` 现在会记录 `userMorphOverrides`，普通 `Update` 与
确定性求值均在 VMD `Evaluate` 之后、`UpdateMorphAnimation` 之前重新应用
（契约 §5 应用顺序），ReplayFromStart / ResetAtTarget 不再丢失用户覆盖。

### 15.6 验证结果（CORE 档）

Windows（MSVC RelWithDebInfo）与 Linux（WSL g++ + llvmpipe）均为：

```text
CTest 5/5 通过（unit / runtime / integration / abi-safety-matrix / render-fbo）
新增 8 个 R1.2A 集成测试全部 PASS
```

新增测试：

```text
R1.2A fixture physics sanity          （dynamic/kinematic/joint > 0）
R1.2A replay from start repeatable    （同输入两次 exactHash 一致）
R1.2A seek consistency                （直达 300 == 分段 150→300）
R1.2A ResetAtTarget canonical         （两次 hash 一致 + 速度/力/accumulator 零）
R1.2A step diagnostics                （executedSubsteps == 4，accumulator == 0）
R1.2A dynamic bodies move             （60 帧后至少一个动态刚体确实运动）
R1.2A unsupported profiles rejected   （60/120、warmup、loop 全部拒绝）
R1.2A out-of-range pose/physics       （无 VMD 时保持姿态、物理继续推进）
```

### 15.7 剩余边界

- R1.2B：`RestoreState` / `IPhysicsStateAccess`、Canonical Restore
  Sequence（契约见
  [R1_2B_RESTORE_STATE_CONTRACT.md](R1_2B_RESTORE_STATE_CONTRACT.md)）；
- R1.2C：`FrameCheckpoint` / `DeterminismFingerprint` /
  `ReplayFromCheckpoint`；
- 任意频率 ReplayConfig、warmup、loopMotion 语义仍未定义，保持拒绝；
- C ABI 仍不开放（R1.4）。

## 16. R1.2A Fixup 记录（2026-08-06，第二轮审查后）

### 16.1 P0-1：接口编译遗漏与“假绿”修正

- `MmdRuntimeModel::EvaluateTick` 从纯虚改为默认返回
  `UnsupportedReplayProfile`，测试替身与未来后端不再被强迫实现；
- 上一轮 CTest 5/5 曾建立在**未重建的旧 unit/runtime 二进制**上；本轮
  对四个构建目录全部从零重建，`wisteria.unit` 真实验证通过。

### 16.2 P0-2：顶点未定义行为的真实根因 = 双 GLM ODR 冲突

ASan+UBSan（`-O1`）复现了审查者看到的顶点垃圾：

```text
bind=(-1,0,0)  morph=(0,0,0)  transforms0=identity
glm::mat4(1) * vec4(-1,0,0,1) → (0,0,garbage,0)   // 同一 TU 内手动分量乘正确
```

根因不是 Saba 并行更新，而是工程同时可见两套 GLM：

- 引擎 TUs 经 `glm_headers` 使用 `third-party/glm`（GLM 1.0.3）；
- Saba 库使用自带 `third-party/saba/external/glm`（GLM 0.9.x）；
- 两套 GLM 生成同名内联符号但浮点向量 `operator==` 等语义不同（0.9 用
  `memcmp` 位比较、1.0 用值比较），链接器按 TU/优化随机选择实例化，
  矩阵乘法读到错误布局 → 顶点分叉。

第一版 Fixup 曾把 `glm_headers` 指向 Saba 自带 GLM 0.9 以快速消除冲突；
第三轮复审后改为**正确的所有权方向**（见 §17）：WISTERIA 持有 GLM
1.0.3，Saba 链接 `glm_headers`。同时加固 Saba 顶点路径（`VertexBoneInfo{}`
零初始化、更新缓冲以 bind 姿态播种、`glm::mat4 m(1)`、非法骨骼索引
identity 回退），并把 GLM 0.9 位比较语义差异（`-0.0 != 0.0`）在
Impulse stop-control 检测中改为显式分量判零（该防护在 GLM 1.0 下依然
成立，属于跨版本稳健性）。

验证：

```text
ASan+UBSan（-O1）集成测试 42 PASS / 0 FAIL / 0 sanitizer 报告
R1.2A identity pose matches bind           PASS
R1.2A out-of-range pose/physics            PASS
```

### 16.3 P0-3：步进状态机

`TimelineStatus` 新增 `InvalidState` / `NonSequentialFrame` /
`DeterminismViolation`。Runtime 维护 `deterministicPrepared` 与
`expectedNextFrame`：

```text
PrepareFrameZero 成功 → prepared=true, expectedNextFrame=1
StepMotionFrameExact：
  未 prepared         → InvalidState
  frame != 期望帧      → NonSequentialFrame
  成功后 expectedNextFrame++
EvaluateTick(ResetAtTarget / PreserveState) 清除 prepared
```

任意目标帧必须走 `EvaluateTick(target, ReplayFromStart)`。

### 16.4 P0-4：ReplayConfig 与真实 Bullet 配置绑定

`ValidateReplayConfig` 现在同时检查：

```text
ReplayConfig：30/120、warmup==0、loopMotion==false、physics enabled
live：physicsSettings.fixedTimeStep==1/120、maxSubSteps>=4、
      MMDPhysics::GetFPS()==120、GetMaxSubStepCount()>=4
```

每帧 `StepFrameExact` 之后必须
`executedSubsteps == 4 && remainingAccumulator == 0`，否则
`DeterminismViolation` 且不标记 canonical。物理禁用时重放返回
`UnsupportedReplayProfile`（R1.2A 只支持启用物理的确定性重放）。

### 16.5 P1：Canonical Reset 补齐 Broadphase

刚体瞬移后显式调用 `world->updateSingleAabb(body)`，避免 reset 后第一次
碰撞仍用旧 AABB；`UpdateRange` 清 pair cache 之外现在覆盖代理体积。

### 16.6 P1：Morph override API 拆分（行为兼容恢复）

```cpp
SetMorphWeight(name, w)      // 恢复旧语义：瞬时，VMD 下一帧可覆盖
SetMorphOverride(name, w)    // 持久覆盖，每帧 VMD 求值后重放
ClearMorphOverride(name)     // 恢复 VMD 驱动
ClearAllMorphOverrides()     // 清空全部覆盖
```

### 16.7 P1：Snapshot 能力位拆分

```cpp
supportsSnapshotCapture   = true   // R1.2A
supportsSnapshotRestore   = false  // R1.2B
supportsCanonicalRestore  = false  // R1.2B
```

### 16.8 Hash 加固

- `DeterminismHashes` 增加 `valid` 标记；
- `HashVertices`：位置/法线数量分别写入 hash，数量不一致或非 finite
  直接返回 invalid，不再静默截断；
- `HashPose`：写入 local/global/skinning 数量与通道标记；
- `HashPhysics`：校验 body index 严格 0..N-1，非 finite 标记 invalid；
- 删除名不副实的 `HashBytesLittleEndian`（typed hasher 已显式小端拆字节）。

### 16.9 新增回归测试

```text
GLM multiply sanity（防 ODR 回归）
R1.2A identity pose matches bind
R1.2A 1000-frame substep probe
R1.2A step state machine
R1.2A live physics config binding
R1.2A disabled physics rejected
R1.2A divergent history reset convergence
R1.2A morph override lifecycle
```

### 16.10 Fixup 后验证矩阵（全量重建）

| 平台/档位 | CTest | 说明 |
| ---- | ---- | ---- |
| Windows MSVC CORE | 5/5 | unit/runtime/integration/abi/render |
| Windows MSVC FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux g++ CORE | 5/5 | llvmpipe |
| Linux g++ FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux ASan+UBSan（-O1） | integration 42 PASS | 0 sanitizer 报告 |

> R1.2A 至此可冻结；R1.2B 仍按 §13 边界推进。

## 17. R1.2A Final Fix（2026-08-06，第三轮复审后）

### 17.1 GLM 所有权方向修正

最终配置（与引擎所有权目标一致）：

```cmake
add_library(glm_headers INTERFACE)
target_include_directories(glm_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glm)   # GLM 1.0.3

# third-party/saba/WISTERIA.cmake
target_link_libraries(saba PUBLIC glm_headers)     # Saba 服从引擎数学 ABI
```

即：**WISTERIA 决定 GLM 版本，Saba Adapter 服从 WISTERIA 数学 ABI**，
不再由第三方后端反向决定通用引擎的数学库。Saba 的
`external/glm`（0.9.x）保留在仓库中但不参与构建。

### 17.2 Canonical Reset 补齐隐藏 Bullet 状态

`ResetCanonicalNoStep()` 在写回 transform / AABB / pairs / 速度力之后新增：

```text
SetActivation(true)                       // 动态刚体恢复真实 Dynamic 模式
setActivationState(ACTIVE_TAG)            // 不继承历史 sleeping 状态
setDeactivationTime(0)
activate(true)
```

因此 Canonical Frame Boundary 的 Snapshot（`kinematic=false`）与 Bullet
内部 collision flags / motion state 一致；长时间运行后的 sleep 历史不会
泄漏进 reset 边界。

### 17.3 非法骨骼索引：identity 回退，不再钳制到 bone 0

`ClampBoneIndex → 0` 改为按贡献回退：

```text
Weight1/2/4：非法索引的贡献用 identity 矩阵（权重大于 0 时仍确定）
SDEF：非法索引回退到 identity 加权混合
QDEF：非法且非 -1 的索引按权重 0 处理
每次模型仅告警一次
```

不再把损坏模型静默绑定到第 0 根骨骼；当前合法 fixture 不受影响。

### 17.4 Pose Hash 结构校验

`HashPose` 现在要求：

```text
local count == global count == skinning count
每个矩阵 16 个分量全部 finite
```

不满足时返回 `valid=false`，不再把 NaN Pose 包装成“有效确定性 hash”。
新增 unit 测试覆盖数量不一致、NaN、重复 body index、顶点数组不一致。

### 17.5 Final Fix 验证矩阵（全量重建，GLM 1.0.3）

| 平台/档位 | CTest | 说明 |
| ---- | ---- | ---- |
| Windows MSVC CORE | 5/5 | unit/runtime/integration/abi/render |
| Windows MSVC FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux g++ CORE | 5/5 | llvmpipe |
| Linux g++ FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux ASan+UBSan（-O1） | integration 42 PASS | 0 sanitizer 报告 |

> R1.2A 可以正式冻结；下一阶段 R1.2B RestoreState 契约。

## 18. R1.2A Final Fix 2（2026-08-06，第四轮复查后）

### 18.1 QDEF 非法骨骼安全回退（P0）

原 QDEF 分支仍有两个隐藏问题：

- `glm::dualquat dq[4];` 默认构造不初始化（GLM 1.0.3），而 hemisphere
  符号判断固定读取 `dq[0..3].real`，非法槽位即使权重为 0 也会读到未初始化
  值；
- 全部槽位非法或权重归零时 `glm::normalize(0)` 产生 NaN。

修复：

```text
dq[4] 显式初始化为 dual quaternion identity
只构造/读取有效且权重非零的槽位
hemisphere 参考 = 第一个有效槽位（不再固定 dq[0]）
totalWeight <= epsilon 或 blend 实部长度 <= epsilon → m = identity
```

同时修正 vendored Saba `PMXFile.cpp` 的 QDEF 读取越界
（`m_boneWeights[3]` / `[4]` → `[2]` / `[3]`，原代码写越界 1 个 float）。

新增回归测试：以 `pmx_physics.pmx` 为模板，测试内拼接生成三种 QDEF 坏骨骼
变体（槽 0 非法 + 槽 1 有效、带非零权重的非法槽、四槽全非法），断言所有
顶点 finite，全非法时回退到 bind 姿态。

### 18.2 非法骨骼告警标志线程安全（P1）

`m_invalidBoneWarningEmitted` 从普通 `bool` 改为 `std::atomic_bool`，
`WarnInvalidBoneOnce()` 使用 `exchange(true, relaxed)` 保证并行蒙皮下只
有一个线程告警；`Destroy()`（重载模型路径）会复位该标志。

### 18.3 Final Fix 2 验证矩阵（全量重建）

| 平台/档位 | CTest | 说明 |
| ---- | ---- | ---- |
| Windows MSVC CORE | 5/5 | 含 QDEF 回归 |
| Windows MSVC FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux g++ CORE | 5/5 | 含 QDEF 回归 |
| Linux g++ FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux ASan+UBSan（-O1） | integration 43 PASS | 0 sanitizer 报告 |

> R1.2A 至此可正式冻结；进入 R1.2B RestoreState 契约。

## 19. R1.2A Final Fix 3（2026-08-06，第五轮复查后，测试加固）

代码安全修复已验收；本轮只补测试/契约层：

### 19.1 QDEF 权重解析的精确回归测试

新增 `TestPmxQdefParserWeights`：直接调用 `saba::ReadPMXFile` 读取测试内
拼接的 QDEF 变体，逐槽断言四个骨骼索引与四个权重
（`0.1/0.2/0.3/0.4` 等互异值）。旧解析器
（`m_boneWeights[3]` / `[4]`）会把权重错位并越界写，该测试必然失败——
不再只依赖最终 skinning 输出“碰巧 finite”。

### 19.2 零权重槽位在矩阵读取前跳过

QDEF 循环改为：

```text
rawWeight == 0          → 跳过（不读取骨骼矩阵）
boneID == -1            → 跳过
boneID 越界             → 告警一次并跳过
候选 dualquat 实部非有限或零长 → 跳过该贡献
只对真正参与的槽位 normalize 并累加
```

文档声称的“只构造/读取有效且权重非零的槽位”现在与代码一致；零权重槽位
的异常骨骼矩阵不再可能污染混合。

### 19.3 normals finite 断言

QDEF 坏骨骼用例补上 `frame.normals.size() == positions.size()` 与逐法线
finite 检查，位置回退正常但法线 NaN 的场景也会被测试抓住。

### 19.4 告警文案中立化

`WarnInvalidBoneOnce` 文案改为
`"applying a skinning-specific safe fallback"`，不再对 QDEF（丢弃贡献）
与 BDEF/SDEF（identity 贡献）使用同一句不准确的描述。

### 19.5 Final Fix 3 验证矩阵（全量重建）

| 平台/档位 | CTest | 说明 |
| ---- | ---- | ---- |
| Windows MSVC CORE | 5/5 | 含 QDEF 两项回归 |
| Windows MSVC FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux g++ CORE | 5/5 | 含 QDEF 两项回归 |
| Linux g++ FULL_ASSETS | 5/5 | 真实 PMX/VMD 全跑 |
| Linux ASan+UBSan（-O1） | integration 44 PASS | 0 sanitizer 报告 |

> R1.2A 可以正式冻结；进入 R1.2B RestoreState 契约。

## 20. R1.2B 实现与验收（2026-08-06）

- 契约 v4.1.1 冻结后完成实现（实现细节与测试见
  [R1_2B_BASELINE_20260806.md](../validation/R1_2B_BASELINE_20260806.md)）；
- 新增 `RigidTransformSnapshot`（3×3 basis）、`PmxRigidBodyMode`、
  `layoutFingerprint` / `physicsConfigurationFingerprint`、
  `TimelineStatus::SnapshotMismatch / InvalidSnapshot / Poisoned`、
  `IPhysicsStateAccess::RestoreState`；
- Saba 窄接口：刚体/关节定义访问器、`SelectMotionStateForMode` /
  `NormalizeCanonicalActivation`、`RebuildCollisionWorldDeterministic` /
  `ClearSolverHistoryDeterministic`；
- Canonical Reset 改用 `forceActivationState`（Bullet 3.25 的
  `activate(true)` 仍被 DISABLE_DEACTIVATION 拦截）；
- 测试钩子 `StepRestoredPhysicsForProbe` 与故障注入
  （`WISTERIA_DETERMINISM_TEST_HOOKS`，仅测试构建）；
- Final Test Fix：配置指纹补齐全部有效 solver 静态字段；补反向测试
  （非法 position/basis、-0.0f、DISABLE_DEACTIVATION 历史、非零
  definitionMass、FollowBone transform 不匹配、Mode 2 骨骼回写）；
- 四套矩阵 CTest 5/5，ASan+UBSan integration 59 PASS / 0 报告。
