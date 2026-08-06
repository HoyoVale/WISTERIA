# R1.2C — FrameCheckpoint 编排与等价性契约（v2，已冻结）

> 状态：**Contract Frozen（2026-08-06）**。R1.2A / R1.2B
> 已冻结并实现。本文档只定义 Checkpoint 数据、公开接口、恢复顺序、
> 状态机、兼容校验与等价性验收；Bullet/Saba 隐藏状态一律引用
> [R1_2B_RESTORE_STATE_CONTRACT.md](R1_2B_RESTORE_STATE_CONTRACT.md)，
> 不重新定义。实现与基线见
> [R1_2C_BASELINE_20260806.md](../validation/R1_2C_BASELINE_20260806.md)。

## 1. 阶段目标与非目标

### 目标

```text
CreateCheckpoint(output)               // 捕获“当前 canonical 帧”
RestoreCheckpoint(checkpoint)          // 替换 Runtime 配置/覆盖并恢复
ReplayFromCheckpoint(checkpoint, M)    // Restore 后从 N+1 推进到 M
证明 restore → replay == from-start    // 等价性矩阵（防假绿）
```

### 明确不做

```text
- 跨资产/跨模型迁移（布局/配置不匹配即拒绝）
- 任意 ReplayConfig（仍冻结 30Hz/120Hz、无 warmup、无 loop）
- checkpoint 文件序列化格式（先做内存值对象）
- C ABI 导出（R1.4）
- 跨机器/跨构建 exact replay（仍限同构建同平台）
- 重新讨论 R1.2B 已冻结的物理语义
```

## 2. 公开接口（唯一入口）

```cpp
// R1.2C（MmdRuntimeModel 落点）
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

规则：

```text
CreateCheckpoint：
- 捕获“当前 canonical frame”（即当前求值帧），不接收 frame 参数；
- 失败不修改 output（先写本地、成功后再移动）；
- 除 canonical boundary 外，还要求：
    CapturePhysicsSnapshot == Ok
    PoseHash.valid / PhysicsHash.valid / VertexHash.valid 全部为 true
  任一不满足 → DeterminismViolation，output 保持原值；
- 调用方需要第 N 帧 checkpoint 时，先 ReplayFromStart(N)，再
  CreateCheckpoint。

ReplayFromCheckpoint：
- 严格顺序（所有能在修改世界前判断的失败都必须在 Restore 前拒绝）：
    ValidateCheckpointStatic(checkpoint)     // 只读，§3/§4 Phase 0
    target 范围与溢出校验                     // target < N 或 == UINT64_MAX → InvalidState
    RestoreCheckpointValidated(checkpoint)   // 开始修改世界
    for frame = N+1; frame <= target; ++frame
        StepMotionFrameExact(frame, config)
- 重放循环中任一 Step 非 Ok：
    立即停止；deterministicPrepared=false；runtimeCanonicalBoundary=false；
    实例进入 Poisoned；返回原始失败状态码；
    后续确定性入口返回 Poisoned，直到既有恢复入口重建。
  异常 → 直接返回 Poisoned。

EvaluateTick(target, SeekPolicy::ReplayFromCheckpoint) 无法携带
checkpoint，保留为预留枚举，不是 R1.2C 的真实入口。
```

```cpp
TimelineStatus ReplayFromCheckpoint(
    const FrameCheckpoint& checkpoint,
    MotionFrameIndex target
)
{
    // 全部只读，不修改 Runtime。
    const TimelineStatus validation =
        ValidateCheckpointStatic(checkpoint);
    if (validation != TimelineStatus::Ok)
        return validation;

    if (target < checkpoint.frame ||
        target == std::numeric_limits<MotionFrameIndex>::max())
    {
        return TimelineStatus::InvalidState;
    }

    const TimelineStatus restore =
        RestoreCheckpointValidated(checkpoint);
    if (restore != TimelineStatus::Ok)
        return restore;

    for (MotionFrameIndex frame = checkpoint.frame + 1;
         frame <= target;
         ++frame)
    {
        // 固定步推进；任一失败按 §2 规则 Poisoned。
    }
    return TimelineStatus::Ok;
}
```

## 3. FrameCheckpoint 与 DeterminismFingerprint（唯一结构）

```cpp
struct UserOverrideState
{
    // 稳定排序：Morph 按 UTF-8 名称字典序；IK 按骨骼名称字典序。
    std::vector<std::pair<std::string, float>> morphOverrides;
    std::vector<std::pair<std::string, bool>> ikOverrides;
    bool physicsEnabled = true;
    bool loopMotion = false;
};

struct AssetIdentity
{
    std::uint64_t pmxFileHash = 0;      // Initialize 时 PMX 文件字节 FNV-1a64
    std::uint64_t vmdFileHash = 0;      // LoadMotion 时 VMD 文件字节 FNV-1a64
    bool hasMotion = false;             // 区分“无 VMD”与 vmdFileHash==0
    std::uint64_t layoutFingerprint = 0;            // R1.2B 直接复用
    std::uint64_t physicsConfigurationFingerprint = 0;  // R1.2B 直接复用
};

struct DeterminismFingerprint
{
    std::uint32_t schemaVersion = 1;
    MotionFrameIndex frame = 0;
    AssetIdentity asset;
    ReplayConfig config;
    UserOverrideState overrides;
    FrameStateHashes state;   // R1.2A 双 hash（Pose/Physics/Vertex）
};

struct FrameCheckpoint
{
    MotionFrameIndex frame;
    PhysicsSnapshot physics;           // R1.2B 快照（schema v2）
    UserOverrideState overrides;
    ReplayConfig config;
    DeterminismFingerprint fingerprint;
};
```

内部一致性（Phase 0 必须全部成立，否则 `InvalidCheckpoint`）：

```text
- checkpoint.frame == checkpoint.physics.motionFrame
- checkpoint.frame == checkpoint.fingerprint.frame
- checkpoint.physics.physicsTick == frame × 4
- checkpoint.physics.canonical == true
- checkpoint.config == checkpoint.fingerprint.config（逐字段）
- checkpoint.overrides == checkpoint.fingerprint.overrides（逐字段）
- checkpoint.physics.layoutFingerprint
    == checkpoint.fingerprint.asset.layoutFingerprint
- checkpoint.physics.physicsConfigurationFingerprint
    == checkpoint.fingerprint.asset.physicsConfigurationFingerprint
- loopMotion 双处一致且均为 false
- overrides.physicsEnabled == true
- morphOverrides 已排序、无重复名称、权重 finite、名称存在于当前模型
- ikOverrides 已排序、无重复名称、名称存在于当前模型
```

`overrides` 逐字段相等中，Morph 权重使用**原始位模式**比较（`+0.0f` 与
`-0.0f` 视为不同），避免把内容不同的 Checkpoint 判为相同。

## 4. 恢复顺序（严格相位）

```text
Phase 0  全部只读验证：
         - §3 内部一致性
         - R1.2B 物理快照静态校验（复用内部辅助函数
           ValidatePhysicsSnapshotStatic：值级/布局/配置，只读）
         - asset identity：pmxFileHash / vmdFileHash / hasMotion /
           layoutFingerprint / physicsConfigurationFingerprint 与当前一致
         - ReplayConfig 冻结档位（30/120、无 warmup、无 loop、enabled）
         - checkpoint.frame < UINT64_MAX（防 frame+1 溢出）
         任一失败 → 对应状态码，Runtime/overrides/时间标签/output 不变

Phase 1  用 checkpoint.config / checkpoint.overrides 替换当前 Runtime 值
         （替换后不再回滚）

Phase 2  只求值 checkpoint.frame 的动画/Morph/IK，不重置、不推进物理。
         （只能复用 ResetAtTarget 的“动画求值子相位”；调用完整
         ResetAtTarget 会在 Restore 前重置物理世界，禁止）

Phase 3  同步 Kinematic target（R1.2A Canonical Reset 的同步语义）

Phase 4  RestorePhysicsSnapshot(checkpoint.physics)
         —— R1.2B 内部已包含 after-physics 求值（UpdateNodeAnimation(true)）
           → model->Update() → SyncPoseFromSaba()，R1.2C 不再重复

Phase 5  重算并校验三个状态 Hash：
         - PhysicsHash 可在 Phase 0 预先 Hash checkpoint.physics 验证
         - Pose/Vertex 只能在恢复后验证
         任一不一致 → DeterminismViolation → Poisoned

Phase 6  deterministicPrepared = true
         expectedNextFrame = checkpoint.frame + 1
```

失败事务边界（与 R1.2B 对齐）：

```text
Phase 0：全部只读，失败不修改任何状态。
Phase 1 开始后：不再承诺回滚；任意失败进入 Poisoned；
             不恢复调用前 overrides。
```

### 状态码边界（E7 实现时禁止随意选择）

```text
Checkpoint 自身重复字段不一致
  （frame / config / overrides / fingerprint / hash 自相矛盾）
  → InvalidCheckpoint

Checkpoint 合法，但与当前 PMX / VMD / 物理布局或配置不兼容
  → SnapshotMismatch

当前 Runtime 状态不允许创建 Checkpoint（非 canonical / hash 无效）
  → InvalidState（创建）/ DeterminismViolation（hash 无效）

恢复后状态 Hash 不一致
  → DeterminismViolation + Poisoned
```

## 5. ReplayFromCheckpoint 的 off-by-one

```text
checkpoint frame = N：其物理状态已经完成 N 帧结果。

RestoreCheckpoint 成功后：
    expectedNextFrame = N + 1

ReplayFromCheckpoint(checkpoint, target)：
    if target < N          → InvalidState（明确拒绝；如需更早帧，
                             走 ReplayFromStart）
    if target == N         → 只恢复，不执行任何子步，返回 Ok
    if target >= UINT64_MAX → InvalidState（防 ++frame 溢出死循环）
    for frame = N+1; frame <= target; ++frame
        StepMotionFrameExact(frame, config)
```

## 6. 等价性承诺（防假绿）

验收核心（同构建同平台）：

```text
实例 A：ReplayFromStart(M)               → baseline hashes
实例 B：ReplayFromStart(N) → CreateCheckpoint(N)
实例 C：先推进到不同历史帧/应用不同 overrides（显著分叉）
        → ReplayFromCheckpoint(checkpoint, M)

比较实例 C 与实例 A：
    Pose exactHash
    Physics exactHash
    Vertex exactHash
```

`target == N` 的零步恢复也必须让实例 C 在恢复前处于不同历史，证明
Restore 确实生效，而不是“本来就停在 N”。禁止同一实例“停在 N 再继续”
的弱测试。

覆盖 checkpoint 帧：

```text
0、1、150、动作末帧、超出动作末帧
```

每个目标帧 M 至少覆盖 `M == N` 与 `M > N` 两条路径。

## 7. 测试与能力开放

只有等价性矩阵全部通过后，才把能力位翻 true：

```cpp
supportsCheckpointCapture = true;
supportsCheckpointRestore = true;
supportsReplayFromCheckpoint = true;
```

测试矩阵：

```text
E1  checkpoint(0)  → replay(1/150/末帧/超末帧) == from-start
E2  checkpoint(1)  → replay(150) == from-start(150)
E3  checkpoint(150) → replay(300) == from-start(300)
E4  checkpoint(末帧) → target==N（零步，实例 C 分叉后恢复）与 target>N
E5  超出动作末帧的 checkpoint → replay 继续物理、动画保持末帧
E6  checkpoint 创建在非 canonical 边界 → InvalidState，output 不变
E7  结构/内部不一致（重复字段、frame、physics、config、overrides、
    fingerprint 不一致）→ InvalidCheckpoint / SnapshotMismatch，
    Phase 0 拒绝，世界不变
E8  跨模型/VMD/配置 checkpoint → 拒绝
E9  结构完全合法但恢复后 Pose/Physics/Vertex exactHash 不一致
    → DeterminismViolation → Poisoned
E10 1000 次 create/restore 循环无漂移
E11 ASan+UBSan 下 E1-E10
```

冻结标准沿用既有矩阵：Windows/Linux CORE + FULL_ASSETS CTest 5/5、
ASan+UBSan 0 sanitizer 报告。

## 8. 明确不实现（边界重申）

```text
- checkpoint 文件格式 / 序列化
- 跨资产迁移与布局热迁移
- 任意频率 ReplayConfig
- C ABI（R1.4）
- 跨机器/跨构建 exact replay
- Bullet 高级参数控制面
```

> 实现过程中发现的 Saba/Bullet 真实行为只回填本文档与 R1.2B Phase 细节，
> 不重新打开已冻结的契约。
