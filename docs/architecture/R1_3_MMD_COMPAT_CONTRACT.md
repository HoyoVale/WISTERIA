# R1.3 — MMD Physics Compatibility & Backend Governance — Phase 0A Contract Frozen

> 状态：**Phase 0A Contract Frozen（2026-08-07）**。R1.2 Complete
> （2026-08-07）之后的主线。本契约冻结 **Phase 0A**（兼容基线、
> 运行 Profile、确定性轨迹系统）；Phase 0B（社区实现对照）只定义
> 进入门槛，不冻结规则内容。
>
> R1.2 规范源：
> [R1_2_DETERMINISTIC_TIMELINE_CONTRACT.md](R1_2_DETERMINISTIC_TIMELINE_CONTRACT.md)
> （R1.2 Complete）、[R1_2B_RESTORE_STATE_CONTRACT.md](R1_2B_RESTORE_STATE_CONTRACT.md)、
> [R1_2C_FRAME_CHECKPOINT_CONTRACT.md](R1_2C_FRAME_CHECKPOINT_CONTRACT.md)。
>
> v1 评审结论：阶段定位与 Phase 0A 范围正确，但 v1 仍以旧
> `MmdPhysicsInstance + PhysicsWorld + RuntimePolicy` 架构为参照，
> 该路线已不在活动构建。**v2 重新锚定到当前 Saba Runtime 主链**，
> 并修正 Cold-Step 分层、配置指纹、Linked-body / Mode 2 语义、
> Trace schema 与单位审计验收条件。
>
> v2 冻结前闭合（评审轮 2，2026-08-07）：
> 默认档改为 `MMD_RAW`；`MmdPhysicsConfiguration` 纳入
> `MmdPhysicsRuntimeSettings` 成为唯一权威配置；fingerprint v2 只
> Hash 有效行为、不 Hash Preset 标签；`DisableConstraintLinkedPairs`
> 公式冻结；Trace 增加 interpolation/motion-state 变换与 `hasMotion`；
> §11 三个 legacy 文档标记已完成。

## 0. 阶段定位

```text
R1.2：时间线与状态所有权（完成）
R1.3：MMD 物理兼容与后端治理（本契约）
R1.4：稳定 Runtime 接口、序列化与外部集成边界
      （Checkpoint 序列化、C ABI、帧转换边界）
```

R1.3 的一句话定义：

> 把 Saba 从一个第三方播放器实现，变成 WISTERIA 中一个受控、可替换、
> 可验证的 MMD 后端；不重写 Bullet/Saba，不发明 MMD 物理，而是把
> “标准兼容行为”“社区兼容行为”“WISTERIA 增强行为”分层管理，并用
> 确定性轨迹证明每一项变化。

## 1. v1 → v2 变更记录

```text
1. 明确当前活动后端是 SabaMmdRuntimeModel，不再以旧物理链为参照
2. 引入 SabaBaseline 内部基底身份（backend + baseline）
3. 删除 CompatibilityProfile 中的 Cold-Step 开关（归 R1.2 执行层）
4. 配置指纹立即升级 v2，与 R1.2C 校验联动，不再“延后”
5. 允许“Preset 派生诊断配置”，禁止匿名配置（ValidateConfiguration）
6. 修正 Linked-body collision 精确语义（PmxMaskOnly 基线 + 诊断档）
7. 拆开 Mode 2 回写模式与 Trace 诊断选项
8. 固定 Trace schema v1（必填字段、稳定排序、接触对、误差公式）
9. 修正 Unit Audit 合法性规则（finite + 空集合合法，不做“全为正”）
10. 标记旧 MmdPhysicsInstance 文档为 legacy/inactive
```

### v2 → Phase 0A Frozen 闭合记录（2026-08-07）

```text
A. MmdPhysicsConfiguration 纳入 MmdPhysicsRuntimeSettings，
   成为唯一权威配置（§4）
B. fingerprint v2 区分“Profile 身份”与“有效物理行为 Hash”，
   只 Hash effective behaviour（§5）
C. DisableConstraintLinkedPairs 固定为：
   PMX mask AND not constraint-linked（§8.1）
D. Trace schema 增加 interpolationWorldTransform、
   motionStateTransform 与 hasMotion（§6）
```

## 2. 活动后端基线（v2 锚定）

当前唯一注册的 MMD 后端链：

```text
ModelBackendRegistry
→ SabaMmdBackend（src/runtime/model_backend.cpp 唯一注册）
→ SabaMmdRuntimeModel（include/wisteria/runtime/saba_mmd_runtime_model.hpp）
→ Saba 自有每模型 Bullet world（OwnsSimulationStep）
```

`SabaMmdRuntimeModel` 声明由 Saba 持有每模型 Bullet 世界，Scene 跳过
共享 `PhysicsWorld` 的固定步生命周期。当前活动公开配置：

```cpp
struct MmdPhysicsRuntimeSettings
{
    float fixedTimeStep = 1.0f / 120.0f;
    int maxSubSteps = 10;
    glm::vec3 gravity{0.0f, -98.0f, 0.0f};
    bool enabled = true;
};
```

### 2.1 SabaBaseline

**SabaBaseline v1 = 当前提交 `2808dab`（R1.2C）的真实物理行为，零行为
变化。** 它包含：

```text
- Saba 的 shape 构造、坐标转换、ground、激活策略、joint 构造；
- Saba Mode 2 实现（物理旋转 + 动画平移回写）；
- SabaBaseline 支持 WISTERIA R1.2 所需的确定性窄接口
  （motion-state 边界同步、ground proxy 重建挂载、FollowBone 边界读取）；
  Cold-Step 的“何时调用这些接口”属于 R1.2 Deterministic Execution
  Profile，不属于 SabaBaseline / MMD Compatibility 行为；
- 默认重力 -98（Bullet 单位），不是 -9.8；
- 默认 linked-body 碰撞：仅 PMX group/mask
  （Saba 以 addConstraint(constraint) 添加关节，不启用
  disableCollisionsBetweenLinkedBodies）。
```

### 2.2 旧路线状态

旧 WISTERIA 自有物理链（`MmdPhysicsInstance` + 共享 `PhysicsWorld` +
`MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults()`）**不在活动构建**，
相关源码文件已移除。README、`PHYSICS_LAYER_AUDIT.md`、
`PROJECT_LAYOUT.md` 中的描述将在本契约冻结前标记为 legacy（见 §11）。

## 3. 目标与非目标

### Phase 0A 目标（本契约的第一实现批次）

```text
1. 兼容规则与自适应增强分层
   MmdPhysicsCompatibilityProfile / MmdPhysicsAdaptivePolicy /
   MmdPhysicsConfiguration，锚定 SabaMmdRuntimeModel
2. SabaBaseline 身份 + 三个 Preset：MMD_RAW / MMD_COMMUNITY /
   WISTERIA_ADAPTIVE
3. Deterministic Physics Trace（JSONL schema v1）+ 最小差分工具
   （第一分叉 / 最大分叉 / 关节误差）
4. 单位与重力审计（先测量，不改默认值）
5. Linked-body collision 与 Mode 2 的 A/B 开关（先可测，不裁决答案）
6. Profile 身份进入日志与 effective configuration fingerprint v2
   （每条轨迹可复现、可引用）
```

### 明确不做（Phase 0A）

```text
- 修改默认重力（-98 / -9.8 之争）或全局单位换算
- 调某个角色的裙摆/头发参数
- 引入未经验证的 solver / CCD / margin 参数
- Checkpoint 文件序列化、C ABI、帧转换边界（R1.4）
- 大规模物理文件重构或完整的 IMmdPhysicsBackend 抽象
- 把旧 MmdPhysicsInstance 的增强策略移植到 Saba Runtime
  （Phase 0A 只建立槽位和身份，不移植旧策略）
- 新增 R1.2D；继续扩写 R1.2 文档
- 跨机器/跨构建 exact replay 承诺
- 声称“哪个 Profile 是官方正确答案”
- 改变 R1.2 Cold-Step，或提供 solver-history 切换开关
  （R1.3 Profile 不控制确定性执行层）
```

## 4. 分层接口（Phase 0A 落点）

```cpp
enum class MmdPhysicsPreset
{
    MmdRaw,           // SabaBaseline + 无社区/Adaptive 覆盖
    MmdCommunity,     // Phase 0A 与 MMD_RAW 相同；以后逐项吸纳有依据规则
    WisteriaAdaptive  // Phase 0A 同样基于 SabaBaseline；仅预留增强槽位
};

struct MmdPhysicsProfileIdentity
{
    std::string backend = "saba-mmd";
    std::string baseline = "saba-baseline-v1";
    MmdPhysicsPreset preset;
    std::uint32_t profileRevision = 1;
};

enum class MmdLinkedBodyCollisionMode
{
    PmxMaskOnly,                       // 当前 Saba 基线（仅 PMX mask）
    DisableConstraintLinkedPairs,      // 相连关节刚体对禁用碰撞
    ForceEnableLinkedPairsDiagnostic   // 诊断：覆盖 PMX mask（Phase 0A Reserved）
};

enum class MmdMode2WritebackMode
{
    PreserveAnimatedTranslation,  // 当前 Saba：物理旋转 + 动画平移
    StrictBoneLength,             // Reserved（算法未定义，见 §8.2）
    FullTransformDiagnostic       // 诊断：完整平移 + 旋转回写
};

struct MmdPhysicsCompatibilityProfile
{
    // 只放“PMX/MMD/社区标准解释”类字段。
    float gravityScale = 1.0f;   // 审计后定值；Phase 0A 不改行为
                                 // （当前实际重力 -98，身份按实际记录）
    MmdLinkedBodyCollisionMode linkedBodyCollision =
        MmdLinkedBodyCollisionMode::PmxMaskOnly;
    MmdMode2WritebackMode mode2 =
        MmdMode2WritebackMode::PreserveAnimatedTranslation;
    // 注意：没有 clearSolverHistoryAtFrameBoundary。
    // Cold-Step 属于 R1.2 确定性执行层，不由 MMD Profile 控制（v2 删除）。
};

struct MmdPhysicsAdaptivePolicy
{
    // 只放“WISTERIA 自己的增强”类字段。
    // Phase 0A 全部为未支持/关闭；不虚构旧增强已移植到 Saba Runtime。
    bool recoveryEnabled = false;
    bool adaptiveCcdEnabled = false;
    bool adaptiveMarginEnabled = false;
    bool localChainEnhancementsEnabled = false;
};

struct MmdPhysicsConfiguration
{
    MmdPhysicsProfileIdentity identity;

    // Runtime 的基础物理数值也是完整配置的一部分。
    // 唯一权威配置：不允许存在独立于 Configuration 的第二份状态。
    MmdPhysicsRuntimeSettings runtime;

    MmdPhysicsCompatibilityProfile compatibility;
    MmdPhysicsAdaptivePolicy adaptive;
};

struct MmdPhysicsDiagnosticOverrides
{
    std::optional<MmdLinkedBodyCollisionMode> linkedBodyCollision;
    std::optional<MmdMode2WritebackMode> mode2;
    MmdPhysicsTraceOptions trace;   // 只记录，不改变行为
};

MmdPhysicsConfiguration BuildPresetConfiguration(MmdPhysicsPreset preset);

TimelineStatus DeriveDiagnosticConfiguration(
    const MmdPhysicsConfiguration& base,
    const MmdPhysicsDiagnosticOverrides& overrides,
    MmdPhysicsConfiguration& output);

bool ValidateConfiguration(const MmdPhysicsConfiguration& config);
```

`BuildPresetConfiguration(MmdRaw)` 直接展开为：

```text
runtime.fixedTimeStep = 1/120
runtime.maxSubSteps   = 10
runtime.gravity       = (0, -98, 0)
runtime.enabled       = true
linkedCollision       = PmxMaskOnly
mode2                 = PreserveAnimatedTranslation
adaptive              = 全部 false
```

原则：

```text
- 每个字段必须能回答“这是 MMD 兼容规则，还是 WISTERIA 增强”；
  字段归属一经确定不得混放（评审时逐项核对）；
- Phase 0A 默认 = MMD_RAW；WISTERIA_ADAPTIVE 保留名称但暂不作为
  默认，Phase 0A 行为与 MMD_RAW 相同，只预留增强槽位；
- BuildPresetConfiguration 是普通运行的唯一构造入口；
  诊断实验必须从 Preset 派生并携带 originPreset 身份；
- 公开 struct 无法真正禁止手工构造，因此 ValidateConfiguration 是
  强制校验入口，匿名/无身份配置必须被拒绝；
- 派生配置身份：originPreset=MmdRaw → identity=custom-from-mmd-raw，
  同时计算 effectiveConfigurationHash。
- MmdPhysicsConfiguration 是 R1.3 唯一权威配置对象；现有
  SetMmdPhysicsSettings 只作为兼容性低层入口，其修改必须同步进入
  当前 Configuration、重新计算 effective fingerprint，并使已有
  deterministic canonical/prepared 状态失效；
- Phase 0A 只在 Initialize 前应用 Profile；运行后切换 Profile
  暂不支持（三个 Preset 分别创建 Runtime 做 from-start 实验）。
```

### Preset 语义

| Preset | 兼容层 | 增强层 | 目的 |
| ------ | ------ | ------ | ---- |
| `MMD_RAW` | SabaBaseline，无社区/Adaptive 覆盖 | 全关 | 最干净对照组 |
| `MMD_COMMUNITY` | v1 与 RAW 相同；以后逐项吸纳有依据规则 | 全关 | 主流播放器近似 |
| `WISTERIA_ADAPTIVE` | Phase 0A 与 RAW 相同 | 全关（仅预留） | 非默认档；名称保留，未来增强落地后再体现差异 |

### Profile 身份

每个 Preset 展开后的完整配置必须生成稳定身份：

```text
preset=MMD_RAW
backend=saba-mmd
baseline=saba-baseline-v1
compatibility=mmd-raw-v1
adaptive=disabled
linkedCollision=PmxMaskOnly
mode2=PreserveAnimatedTranslation
gravity=-98
fixedStep=1/120
executionProfile=deterministic-cold-step-v1
```

身份进入：日志头、effective configuration fingerprint v2（§5）与
Trace 头（§6）。Preset 名称用于人读，effective hash 用于证明行为一致。

## 5. 配置指纹 v2（Phase 0A 第一步）

### 原则

```text
凡能改变刚体、骨骼回写、接触、约束、Pose/Physics/Vertex 的字段，
必须立即进入 physicsConfigurationFingerprint。
```

draft v1 中“linked collision 与 Mode 2 进入配置指纹”与“Phase 0A 不
扩展 fingerprint schema”互相矛盾。**v2 裁决：指纹立即升级，不延后。**

### 两类身份（冻结裁决）

```text
人类/Trace 身份（描述“这项实验叫什么”，不进 fingerprint）：
  preset=MMD_COMMUNITY
  originPreset=MMD_RAW
  profileRevision=1
  baseline=saba-baseline-v1

Physics configuration fingerprint（只 Hash 能真正改变计算结果
的有效行为）：
  fingerprint schema version
  backend behaviour ABI/version
  baseline behaviour revision
  runtime.enabled
  effective gravity
  fixed timestep
  max substeps
  linked collision effective behaviour
  Mode 2 effective writeback behaviour
  solver settings
  shape margin / dimensions
  model scale
  所有实际启用的 adaptive 行为
```

**不要 Hash：** MMD_RAW / MMD_COMMUNITY 等显示名称、originPreset、
trace 文件名、人类可读 profile 字符串。

理由：Phase 0A 三个 Preset 有效物理行为完全相同；仅切换 Preset
标签不应导致 fingerprint 变化，否则 RAW checkpoint → COMMUNITY
（行为相同）会被误拒绝。将来 COMMUNITY 真正加入一条社区规则时，
effective behaviour 变化，fingerprint 自然变化。这与 R1.2C 的原始
原则一致：**配置不兼容是因为物理行为不同，而不是因为标签不同。**

### Hash 内容

```text
physics configuration fingerprint version：1 → 2

Hash：
- backend behaviour ABI/version（saba-mmd）
- baseline behaviour revision（saba-baseline-v1）
- effective gravity
- fixed step / max substeps
- linked collision effective behaviour
- Mode 2 effective writeback behaviour
- effective solver settings（现有全部 solver 字段）
- effective margins / shape dimensions
- model scale
- 实际启用的 adaptive 行为（Phase 0A 恒 false，仍记录）
```

不升级 `FrameCheckpoint` schema（其中已持有
`physicsConfigurationFingerprint` 字段），但指纹内部必须有版本域。

### 与 R1.2C 的关系

R1.2C 的 Restore 已在校验阶段比对
`physicsConfigurationFingerprint`（`RestoreCheckpointValidated`）。
指纹 v2 接入后，Profile 字段一旦变化，跨配置 Restore 继续被现有校验
拒绝；Phase 0A 不绕过该校验。

```text
场景：Checkpoint 在 PreserveAnimatedTranslation 下创建，
Runtime 切换为 FullTransformDiagnostic，RestoreCheckpoint
→ fingerprint v2 不同 → SnapshotMismatch / ConfigurationMismatch
（沿用 R1.2C 既有状态码），禁止恢复。
```

## 6. Deterministic Physics Trace（schema v1）

### 6.1 输出形态

```text
traces/
  <model-hash>/
    mmd-raw.jsonl
    mmd-community.jsonl
    wisteria-adaptive.jsonl
```

JSONL：每个 canonical frame 一行，必填字段：

```json
{
  "traceSchemaVersion": 1,
  "backendIdentity": "saba-mmd",
  "presetIdentity": "mmd-raw-v1",
  "effectiveConfigurationHash": "0123456789abcdef",
  "executionProfile": "deterministic-cold-step-v1",
  "modelHash": "abcdef0123456789",
  "hasMotion": true,
  "motionHash": "abcdef0123456789",
  "frame": 150,
  "physicsTick": 600,
  "canonical": true,
  "stateHashes": {
    "pose":    { "hash": "0123456789abcdef", "valid": true },
    "physics": { "hash": "0123456789abcdef", "valid": true },
    "vertex":  { "hash": "0123456789abcdef", "valid": true }
  },
  "bodies": [
    {
      "index": 309,
      "mode": "PhysicsWithBone",
      "worldTransform": {
        "position": [1.2, 8.4, -0.7],
        "rotationBasis": [1,0,0,0,1,0,0,0,1]
      },
      "interpolationWorldTransform": {
        "position": [1.2, 8.4, -0.7],
        "rotationBasis": [1,0,0,0,1,0,0,0,1]
      },
      "motionStateTransform": {
        "position": [1.2, 8.4, -0.7],
        "rotationBasis": [1,0,0,0,1,0,0,0,1]
      },
      "linearVelocity": [0.02, -0.1, 0.0],
      "angularVelocity": [0.0, 0.2, -0.1]
    }
  ],
  "bones": [ { "index": 12, "local": [...], "global": [...] } ],
  "joints": [
    {
      "index": 274,
      "rawLinearError": 0.02,
      "linearViolation": 0.0,
      "rawAngularErrorDeg": 1.3,
      "angularViolationDeg": 0.0
    }
  ],
  "contactPairs": [
    {
      "bodyA": 12,
      "bodyB": 13,
      "pointCount": 2,
      "maxPenetration": 0.015,
      "normalImpulse": 1.72
    }
  ],
  "events": ["reset", "checkpoint", "restore", "profileSwitch"]
}
```

### 6.2 固定规则

```text
- 必填：traceSchemaVersion / backendIdentity / presetIdentity /
  effectiveConfigurationHash / executionProfile / modelHash /
  hasMotion / motionHash / frame / physicsTick / canonical；
- hasMotion 与 R1.2 AssetIdentity 同语义：无 VMD 时
  hasMotion=false、motionHash="0000000000000000"；
  “无 VMD”与“VMD hash == 0”必须可区分；
- 数组稳定排序：bodies 按 rigid body index；bones 按 bone index；
  joints 按 joint index；contactPairs 按 (min(bodyA,bodyB),
  max(bodyA,bodyB))；
- rotationBasis：column-major，9 个 float（必须显式声明，不留歧义）；
- body 记录 worldTransform（Bullet body COM）与
  interpolationWorldTransform（latency interpolation 状态）两个独立
  变换；motionStateTransform 在 body 存在独立可读状态时记录，
  否则 motionStateAvailable=false，不伪造数值；
- Hash：16 位小写十六进制字符串，valid 单独记录；
- 最少记录集（验收必须全部存在）：frame、physicsTick、profile、
  body transform/interpolation/velocity、bone transform、rigid body
  mode、joint violation、contact pair、三个 state hash、事件标记。
```

R1.2C 最难找的主分叉之一是 body COM ≠ motion state transform；
只记录 COM 会在“两个世界看起来 body 完全一样，但下一步突然分叉”
时无法定位，因此上述三个变换都必须进入 schema。

### 6.3 Joint error 公式

```text
rawLinearError      关节两端局部 Frame 在 constraint frame 的平移差
                    （未扣除允许范围）
linearViolation     扣除允许 linear limits 后的 violation norm
rawAngularErrorDeg  角度差（未扣除允许范围）
angularViolationDeg 扣除允许 angular limits 后的违规角度
```

避免再次把宽行程关节的合法差异误判为故障。

### 6.4 Trace 是只读观察层

```cpp
PhysicsTraceFrame CapturePhysicsTraceFrame() const;
```

JSONL Writer 和 diff 工具放在 tooling/test 层。Runtime 不负责：

```text
- 创建目录；
- 打开文件；
- JSON 转义；
- flush；
- 文件命名。
```

否则 Trace 会把 I/O 和序列化逻辑塞进 Runtime，并可能改变帧耗时。

### 6.5 最小差分工具

输入两条 JSONL，输出：

```text
Profile A / Profile B
First divergence: frame / body / positionError / rotationErrorDeg
Maximum divergence: frame / body / positionError / rotationErrorDeg
Joint error delta: joint / linear / angular
```

验收：人为注入一个可重复的第一分叉（如某一帧把某刚体位置 +0.001），
差分工具必须定位到该帧与刚体。

### 6.6 与 Checkpoint 的关系（裁决）

```text
- 第一版跨 Profile 比较：各自从 frame 0 确定性重放（from-start），
  不依赖 Checkpoint；
- 诊断分叉实验（Checkpoint(N) + Profile A 到 M，恢复后换 Profile B）
  只定义语义，**Phase 0A 不实现**；
- 正式 RestoreCheckpoint 的兼容校验（layout/config/资产指纹）不能被
  绕过；若两 Profile 的配置指纹不同，跨 Profile 只能走专用实验入口
  （预留，不在 Phase 0A 实现）；
- R1.2 的等价性承诺不受 Trace 影响（Trace 是只读导出）。
```

## 7. 单位与重力审计（先测量）

### 验收规则（v2 修正）

```text
- 所有实际数值必须 finite；
- 空集合（无 VMD、无关节的 CORE fixture、0 质量 FollowBone、
  零长度辅助骨骼）报告 available=false / count=0；
- 不同指标使用各自合法范围：负的关节下限、零角度限制、零长度
  辅助骨都是合法数据，不做“每项为正”断言；
- “合理量级”只能作为诊断 warning，不作为断言，
  否则小型测试模型会被误判。
- 除零边界：modelHeight <= epsilon →
  gravityPerModelHeight.available=false；
  medianBodySize <= epsilon →
  shapeMarginPerMedianBodySize.available=false。
```

### 审计输出

```text
modelBounds
modelHeight

boneLength:
  count / zeroCount / minPositive / median / p95 / max

rigidBodySize:
  count / min / median / p95 / max

jointLinearRange:
  count / zeroRangeCount / medianExtent / maxExtent

jointAngularRangeDeg:
  count / zeroRangeCount / medianExtent / maxExtent

gravityMagnitude
gravityPerModelHeight
fixedTimeStep
shapeMarginPerMedianBodySize
```

每条审计指标 finite 且在其合法范围内；审计不修改任何默认值。
最终目标是回答：**PMX 1 单位在 WISTERIA 中具体代表什么？**
裁决发生在 Phase 0B（有社区实现对照后），不在 Phase 0A。

## 8. 两个首要 A/B 开关

### 8.1 Linked-body collision

```cpp
enum class MmdLinkedBodyCollisionMode
{
    PmxMaskOnly,                       // 当前 Saba 基线
    DisableConstraintLinkedPairs,      // 相连关节刚体对禁用碰撞
    ForceEnableLinkedPairsDiagnostic   // 诊断：覆盖 PMX mask（Reserved）
};
```

语义（v2 修正，消除 v1 歧义）：

```text
PmxMaskOnly
  当前 Saba 基线：addConstraint(constraint) 不传
  disableCollisionsBetweenLinkedBodies，碰撞只由 PMX group/mask 决定；
  不额外因为“两个刚体有关节连接”而禁碰撞。

DisableConstraintLinkedPairs
  冻结公式：
  collisionAllowed = pmxMaskAllows(A, B) AND !isConstraintLinked(A, B)

  PMX mask 永远是基础过滤；该模式只增加“相连关节刚体对禁止碰撞”
  这一额外条件，不会把 PMX 原本禁止的 pair 重新打开。
  Ground 不属于 PMX constraint-linked pair，不受本策略影响。
  通过 A/B 测量验证。

ForceEnableLinkedPairsDiagnostic
  诊断模式：覆盖 PMX mask 强制允许碰撞；
  Phase 0A 若不实现 mask override，则保留为 Reserved，
  不进入正式 Profile。
```

```text
- 每个已实现枚举有独立 smoke 测试；
- ground 特殊 filter 不受 DisableConstraintLinkedPairs 影响，
  单独测试确认；
- 不裁决“官方答案”；由轨迹证据决定归属
  （COMMUNITY 或 ADAPTIVE）。
```

### 8.2 Mode 2（行为与诊断拆开）

```cpp
enum class MmdMode2WritebackMode
{
    PreserveAnimatedTranslation,  // 当前 Saba：物理旋转 + 动画平移
    StrictBoneLength,             // Reserved（算法未定义）
    FullTransformDiagnostic       // 诊断：完整平移 + 旋转回写
};

struct MmdPhysicsTraceOptions
{
    // 只记录平移增量，不改变物理行为。
    bool recordMode2TranslationDelta = false;
};
```

```text
PreserveAnimatedTranslation
  当前 Saba 基线（DynamicAndBoneMergeMotionState）：
  物理刚体提供旋转，动画骨骼保留平移。

FullTransformDiagnostic
  只用于导出/对比，不进入默认 Preset。

StrictBoneLength
  冻结前必须定义：
  - 使用哪两点计算骨骼长度；
  - 保持 bind length 还是动画帧长度；
  - 在父空间还是世界空间修正；
  - 修改骨骼平移还是刚体位置；
  - before/after physics 的执行顺序；
  - 根骨骼和无父骨骼如何处理；
  - 是否影响后代；
  - 是否进入 Checkpoint Fingerprint。
  未定义前为 Reserved，不要求 smoke test。
```

```text
- 记录数据（recordMode2TranslationDelta）不是物理行为模式；
- 候选默认 PreserveAnimatedTranslation，Phase 0A 不启用新默认；
- 每个已实现枚举有独立 smoke 测试。
```

## 9. 后端边界（Phase 0A 收尾）

- 现状已满足：上层 Runtime 不暴露 `bt*`，Saba 窄接口已存在；
- Phase 0A 只要求配置以中立类型（`MmdPhysicsConfiguration`）进入
  Runtime，Saba Adapter 负责翻译，不引入完整 `IMmdPhysicsBackend`；
- 完整后端抽象（`Capabilities / Configure / Step / Capture / Restore`）
  在 Phase 0A 之后单独评估，不塞进本批次。

## 10. 测试与验收（Phase 0A）

```text
单元：BuildPresetConfiguration 每个 Preset 完整且身份稳定；
      ValidateConfiguration 拒绝匿名/无身份配置
集成：三个 Preset 在 pmx-physics 上各跑 300 帧
      → 轨迹可生成、逐帧 hash 可复现；
      Phase 0A 轨迹与 SabaBaseline 一致（零行为变化）
Schema：JSONL 可解析、必填字段齐全、排序稳定
A/B：linked-body 与 Mode 2 已实现枚举各 smoke；
      ground filter 单独覆盖
审计：输出非空、数值 finite、空集合合法
差分：能定位人为注入的第一分叉
指纹：Profile 字段变化 → fingerprint v2 变化
      → 跨配置 Restore 拒绝（回归 R1.2C 校验）
门禁：Linux/Windows CORE 与 FULL_ASSETS CTest 全绿
```

## 11. 文档状态标记（已完成，2026-08-07）

README、`PHYSICS_LAYER_AUDIT.md`、`PROJECT_LAYOUT.md` 中旧物理链
描述已加状态声明：

```text
Legacy WISTERIA-owned physics path
目前不参与 SabaMmdRuntimeModel 主运行链；
保留为历史架构与未来通用 PhysicsInstance 参考。
```

目的：避免实现者在旧 `mmd_physics_policy.hpp`（已移除）上增加新
Profile，导致测试编译通过但运行行为完全不变——形成隐蔽的假实现。

## 12. 推荐执行顺序

```text
1. R1.2 文档收尾（已完成：R1.2 Complete）
2. 本契约 v2 评审（已完成：Phase 0A Frozen，2026-08-07）
3. 旧文档 legacy 标记（已完成，§11）
4. 分层类型 + SabaBaseline + BuildPresetConfiguration +
   ValidateConfiguration
5. Profile 身份 + 配置指纹 v2 接入（先于 Trace）
6. Physics Trace（JSONL schema v1）+ 最小差分
7. Unit Audit（修正后的合法性规则）
8. Linked-body / Mode 2 A/B + 测试
9. 轨迹回归用例与四套矩阵
10. Phase 0B 社区实现对照（单独契约）
```

## 13. 拍板裁决（2026-08-07 冻结）

```text
1. 默认档：Phase 0A 默认 = MMD_RAW；
   WISTERIA_ADAPTIVE 保留名称，暂不作为默认
   （行为保持一致不要求名称沿用历史含义）
2. Cold-Step：批准归 R1.2 deterministic execution profile；
   R1.3 只在 Trace 记录 executionProfile，不提供开关
3. 跨 Profile Checkpoint：批准 Phase 0A 不实现，
   全部 from-start 重放
4. Mode 2 默认：批准 PreserveAnimatedTranslation
5. ForceEnableLinkedPairsDiagnostic：批准 Reserved
6. StrictBoneLength：批准 Reserved
7. 指纹 v2：批准立即实施；只 Hash effective behaviour，
   不 Hash Preset 显示标签（§5）
```
