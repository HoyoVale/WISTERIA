# R1.2B — PhysicsSnapshot RestoreState 契约（v4.1.1，已冻结并实现）

> 状态：**已冻结并实现（2026-08-06）**。R1.2A 已冻结；本文档是 R1.2B
> 类型、顺序、状态码与测试的**唯一规范源**。
> 本文档是 R1.2B 类型、顺序、状态码与测试的**唯一规范源**；总契约
> `R1_2_DETERMINISTIC_TIMELINE_CONTRACT.md` 只保留阶段说明与链接，不再
> 维护 R1.2B 的结构定义。
>
> v4.1.1 在 v4.1 基础上修正：FollowBone 改为从快照精确写回（不再清零）、
> basis 校验增加右手行列式条件与显式分量顺序、配置指纹定义为“全部有效
> 静态配置”并补 solver 细节与 T9b、冻结 `sizeof(btScalar)==4` 边界。
> 实现状态与验收见
> [R1_2B_BASELINE_20260806.md](../validation/R1_2B_BASELINE_20260806.md)。

## 1. 目标、非目标与阶段边界

### 1.1 目标（收窄后的 R1.2B）

把**一个 canonical 物理快照**安全写回 Saba/Bullet，只承诺：

```text
1. 校验拒绝路径不修改世界（validate-then-mutate）；
2. dynamic 刚体的可观察物理字段（transform/interpolation/velocity/
   activation/deactivation）写回与快照一致；
3. 隐藏缓存（collision world / solver warm-start / accumulator）按本契约
   的确定性清理序列规范化；
4. 骨骼/蒙皮从恢复后的物理状态回写；
5. canonical 边界与状态机标志有分离、可验证的定义；
6. 写入期灾难性失败会把实例置为 Poisoned，调用方可明确重建。
```

### 1.2 明确不承诺（v1 矛盾项的裁决）

```text
- 不承诺“写回阶段原子性/可回滚”（只承诺校验拒绝不修改世界）；
- 不恢复任意 force/torque（canonical 快照 force/torque 必须为零，恢复只清零）；
- 不重设质量/形状/关节等静态布局（只校验，不写回）；
- **动画前置是硬性条件**：当前动画求值帧与 FollowBone transforms 必须与
  快照一致，否则 Restore 返回 `InvalidState`；R1.2B 不负责恢复动画；
- 不承诺“Restore 后可直接继续确定性步进”（R1.2B 完成后
  deterministicPrepared=false，继续步进属于 R1.2C Checkpoint 语义）；
- 不承诺 restore → replay == from-start（R1.2C 证明）。
```

### 1.3 阶段边界（取代旧文档的模糊表述）

| 阶段 | 负责内容 | 明确不负责 |
| ---- | -------- | ---------- |
| R1.2A（已冻结） | Capture（只读）、Canonical No-Step Reset、确定性步进、双 hash、状态机 | Restore、Checkpoint |
| **R1.2B（本文档）** | **只接受 canonical 快照（且动画前置满足）的 RestoreState + 确定性碰撞世界重建/求解历史清理 + 布局校验 + Poisoned 失败语义 + canonical 可验证定义** | 动画/覆盖/fingerprint 恢复、等价性证明、Checkpoint |
| R1.2C | `FrameCheckpoint`（frame、physics、overrides、config、fingerprint）、`ReplayFromCheckpoint`、restore→replay == from-start 的证明；恢复动画/覆盖并**重建** Kinematic target；`motionFrame + 1` 溢出校验 | 不再重新定义 canonicalization |

关键修正：

- **“完整 canonicalization”整体移入 R1.2B 的 Canonical Restore Sequence**；
- R1.2C 不再实现 canonicalization，只做 Checkpoint 编排与等价性证明；
- **继续确定性步进的能力从 R1.2B 移出**：R1.2B 恢复后
  `deterministicPrepared=false`；R1.2C 恢复完整 Checkpoint 后才允许
  `prepared=true` 并继续。

## 2. 类型与接口（唯一规范源）

### 2.1 TimelineStatus 扩展

R1.2B 实现阶段在 `wisteria/runtime/determinism.hpp` 加入（以本文档为准）：

```cpp
enum class TimelineStatus
{
    // R1.2A 已有：
    Ok, NoPhysics, InvalidCheckpoint, UnsupportedReplayProfile,
    InvalidState, NonSequentialFrame, DeterminismViolation,

    // R1.2B 新增：
    SnapshotMismatch,   // schema/布局/配置不匹配，或快照字段被篡改
    InvalidSnapshot,    // 值级非法：非 finite、非法旋转 basis、非 canonical 声明
    Poisoned,           // 写入期灾难性失败后，实例需重建才能继续
};
```

### 2.2 IPhysicsStateAccess

```cpp
class IPhysicsStateAccess
{
public:
    virtual ~IPhysicsStateAccess() = default;

    // 只接受 canonical=true 的快照；校验见 §3，写回顺序见 §5。
    // 成功后 deterministicPrepared=false（§9.3）。
    virtual TimelineStatus RestoreState(
        const PhysicsSnapshot& snapshot
    ) = 0;
};
```

### 2.3 MmdRuntimeModel 落点

```cpp
// R1.2B（语义见本文档）
virtual TimelineStatus CapturePhysicsSnapshot(
    PhysicsSnapshot& output
) const = 0;
virtual TimelineStatus RestorePhysicsSnapshot(
    const PhysicsSnapshot& snapshot
) = 0;
```

### 2.4 刚体语义模式（不可变，来自 PMX 定义）

```cpp
enum class PmxRigidBodyMode : std::uint8_t
{
    FollowBone = 0,        // PMX Mode 0（Static / 运动学）
    Physics = 1,           // PMX Mode 1（Dynamic）
    PhysicsWithBone = 2,   // PMX Mode 2（DynamicAndBoneMerge）
};
```

该枚举**必须来自模型定义**（vendored Saba 窄访问器，§11），禁止从 Bullet
运行时 `invMass` 反推。

### 2.5 PhysicsSnapshot 最终字段（R1.2B 冻结）

```cpp
// Bullet btTransform 内部持有 basis(3×3) + origin，不是四元数。
// 用四元数做 Capture→Restore→Capture 不保证位级可逆（两次方向转换都含
// 乘法/开方/分支），因此快照必须保存 3×3 basis 的 9 个 float 分量。
struct RigidTransformSnapshot
{
    glm::vec3 position{0.0f};
    // 列主序 3×3 基矩阵，显式 9 个 float 序列化/Hash，
    // 不依赖 glm::mat3 内存布局或 padding。
    std::array<float, 9> rotationBasis{};
};

struct RigidBodySnapshot
{
    std::uint32_t index = 0;             // 稳定刚体索引，必须 == vector 位置
    PmxRigidBodyMode mode = PmxRigidBodyMode::FollowBone;  // 不可变定义
    float definitionMass = 0.0f;         // PMX 原始质量字段（位模式），
                                         // 只用于指纹/逐体校验，
                                         // 不决定运行时静动态模式
    RigidTransformSnapshot worldTransform;
    RigidTransformSnapshot interpolationTransform;
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 interpolationLinearVelocity{0.0f};
    glm::vec3 interpolationAngularVelocity{0.0f};
    glm::vec3 totalForce{0.0f};          // canonical 快照必须为零
    glm::vec3 totalTorque{0.0f};         // canonical 快照必须为零
    std::int32_t activationState = 0;    // Physics：canonical 必须 == ACTIVE_TAG
                                         // FollowBone：仅信息性（不校验不恢复）
    float deactivationTime = 0.0f;       // Physics：canonical 必须 == 0
                                         // FollowBone：仅信息性（不校验不恢复）
};

struct PhysicsSnapshot
{
    std::uint32_t schemaVersion = 2;     // 本文档冻结的 schema
    std::uint64_t layoutFingerprint = 0; // 完整物理布局指纹（§3.2）
    std::uint64_t physicsConfigurationFingerprint = 0; // 物理配置指纹（§3.2）
    std::vector<RigidBodySnapshot> rigidBodies;
    MotionFrameIndex motionFrame = 0;    // VMD 帧（30Hz 动作边界）
    TimelineTick physicsTick = 0;        // 必须 == motionFrame × 4
    std::uint32_t jointCount = 0;
    // 捕获路径声明的边界标记（claim）。Restore 校验它，不修改它。
    bool canonical = false;
};
```

三个概念分离（§7）：

```text
snapshot.canonical          输入快照的边界声明（capture 写入）
runtimeCanonicalBoundary    Restore 完成后当前实例是否位于 canonical 边界
deterministicPrepared       状态机是否允许继续确定性步进
```

PhysicsHash 的旋转输入相应改为 `rotationBasis` 的 9 个 float 分量
（显式序列化，不依赖 glm::mat3 布局/padding）；四元数不再作为
exact restore/hash 的唯一旋转状态。

**PhysicsHash 的 FollowBone 例外**：

```text
PhysicsHash 不包含 FollowBone 刚体的 activationState / deactivationTime；
Physics / PhysicsWithBone 的对应字段正常参与 Hash。
```

## 3. 快照兼容性校验

### 3.1 校验项（Phase 0，全部通过才进入写回）

```text
1. 目标模型存在 MMDPhysics 世界                 → 否则 NoPhysics
2. 物理启用                                    → 否则 UnsupportedReplayProfile
3. snapshot.schemaVersion == 当前 schema       → 否则 SnapshotMismatch
4. snapshot.canonical == true                  → 否则 InvalidSnapshot
5. rigidBodies.size() == 世界刚体数量           → 否则 SnapshotMismatch
6. jointCount == 世界关节数量                   → 否则 SnapshotMismatch
7. 每刚体 index == vector 位置，严格 0..N-1     → 否则 SnapshotMismatch
8. 当前世界不可变布局重算 fingerprint
   == snapshot.layoutFingerprint               → 否则 SnapshotMismatch（跨布局）
9. 当前物理配置重算 fingerprint
   == snapshot.physicsConfigurationFingerprint → 否则 SnapshotMismatch（配置不兼容）
10. 逐体直接比较（必须在数量校验之后，避免越界）：
    snapshot.body[i].mode == 当前布局 body[i].mode
    snapshot.body[i].definitionMass
        == 当前布局 body[i].definitionMass（位模式）
    snapshot.body[i].index == i
                                               → 否则 SnapshotMismatch（篡改/跨布局）
11. 以下字段逐项 finite（不含集合式省略）：
    definitionMass
    worldTransform.position
    worldTransform.rotationBasis（9 分量）
    interpolationTransform.position
    interpolationTransform.rotationBasis（9 分量）
    linearVelocity / angularVelocity
    interpolationLinearVelocity / interpolationAngularVelocity
    totalForce / totalTorque
    deactivationTime                           → 否则 InvalidSnapshot
12. rotationBasis（world 与 interpolation）列向量近似单位正交
    （列内积 |dot-0| <= 1e-3、列长 |len-1| <= 1e-3；
    不做正交化，只拒绝），且
    abs(determinant(basis) - 1.0f) <= 1e-3
    （拒绝反射矩阵）                          → 否则 InvalidSnapshot
13. canonical 声明前置条件（零值统一使用 `+0.0f` 位模式比较，
    不是数值 `== 0`，`-0.0f` 直接拒绝）：
    Physics / PhysicsWithBone：
      activationState == ACTIVE_TAG
      deactivationTime == +0.0f（位模式）
    FollowBone：
      activationState / deactivationTime 不校验（仅信息性，不恢复；
      模式正确性由 SelectMotionStateForMode / NormalizeCanonicalActivation
      保证）
    totalForce / totalTorque 每分量 == +0.0f（位模式）
    physicsTick == motionFrame × 4（乘法无溢出）  → 否则 InvalidSnapshot
14. 动画前置：当前动画求值帧 == snapshot.motionFrame，
    且每个 FollowBone 刚体的当前 canonical kinematic transform
    （position + rotationBasis）与快照位级一致     → 否则 InvalidState
```

任意一项失败：**不触碰世界**，返回对应状态码。

校验顺序硬性规则：**所有 `body[i]` 访问都必须在第 5 项数量校验之后**。
`motionFrame + 1` 溢出校验不属于 R1.2B（Restore 后不允许直接步进），
移到 R1.2C 的 `ReplayFromCheckpoint` 前置条件。

`rotationBasis` 的 9 个分量固定顺序（列主序显式写出）：

```text
[c0.x, c0.y, c0.z,
 c1.x, c1.y, c1.z,
 c2.x, c2.y, c2.z]
```

恢复到 `btMatrix3x3` 时必须显式完成列/行映射，禁止依赖构造函数参数顺序
或结构内存布局。

### 3.1b 精度边界（冻结）

R1.2B 第一版**只支持 `sizeof(btScalar) == 4`**：

```text
BT_USE_DOUBLE_PRECISION 构建不提供 snapshot restore 能力；
由编译期或 capability 明确拒绝。
```

原因：快照 scalar schema 为 float，double→float 捕获已丢失位模式，即使
源/目标实例都是 double 构建也无法 exact restore。双精度 schema 留待未来
单独设计，不在本阶段扩大范围。

### 3.2 layoutFingerprint（完整物理布局）

FNV-1a64（复用 R1.2A hash 基建），输入：

```text
schemaVersion
layoutVersion（模型布局版本/资产布局代，R1.2B 固定为 1）
jointCount
每刚体（按稳定索引排序）：
    index
    PmxRigidBodyMode
    definitionMass（PMX 原始质量位模式）
    shape 类型与尺寸（sphere 半径 / box 半尺寸 / capsule 半径+半高）
    bone index（绑定骨骼）
    body local transform（offset/bone-frame）
    collision group / mask
    linear damping
    angular damping
    restitution
    friction
每关节（按稳定索引排序）：
    rigidBodyA / rigidBodyB
    joint type（含类型相关全部静态参数）
    frame transform
    线性/角度限制
    spring 参数
全局：
    physics-layout conversion version（PMX→Bullet 转换版本）
    model scale / unit scale（若参与创建）
```

### 3.2b physicsConfigurationFingerprint（物理配置指纹）

同一布局指纹不足以判定兼容：两个实例可以从创建期就具有不同的
collision margin、gravity、solver 配置等。R1.2B **不提供这些参数的控制面**，
但兼容性校验必须覆盖它们。FNV-1a64 输入：

```text
Bullet scalar precision / physics ABI version
effective collision margin
effective Bullet shape dimensions（创建后实际值）
world gravity
fixed timestep
solver implementation + solver mode
solver iterations
solver damping
max error reduction
split impulse 开关及阈值
ERP / ERP2
global CFM
warm-start factor
restitution threshold
linear slop
randomization / ordering 相关选项
CCD 与 dispatch 相关有效配置
broadphase implementation
model/unit scale
compatibility profile
linked-body collision policy
deactivation policy
physics-layout conversion version
```

语义：

- `physicsConfigurationFingerprint` 哈希当前后端中**所有会影响一次物理步
  结果的有效静态配置**；上面列出的字段是**最低集合，不是封闭的完整
  集合**——实现阶段必须逐项枚举后端实际暴露的静态配置并全部纳入；
- per-body effective dimensions / margins 按稳定刚体索引写入指纹；
- 建议实现阶段定义版本化的 `EffectivePhysicsConfigurationV1` 逐字段
  序列化，而不是由实现者自由解释“solver mode”等字段。

Restore Phase 0 同时要求：

```text
snapshot.layoutFingerprint               == currentLayoutFingerprint
snapshot.physicsConfigurationFingerprint == currentConfigurationFingerprint
```

语义：

- **指纹只在“当前世界”一侧重算**：快照本身不携带 shape/bone/offset/
  group-mask/joint/damping 等布局字段，无法从快照重算完整指纹；
- 篡改防护由 **逐体直接比较**（§3.1 第 9 项）承担：mode/definitionMass/
  index 与当前不可变布局逐体比较，保留旧指纹也无法通过；
- 指纹所需数据来自不可变 PMX 定义（§11 窄访问器），不来自帧状态；
- definitionMass 使用 **PMX 文件记录的原始质量位模式**（窄访问器直接
  读取），不使用 `1.0f / invMass` 的倒数往返值；它**不决定**运行时
  静动态模式（模式只由 `PmxRigidBodyMode` 与 Saba 接口决定，见 §4）；
- collision margin、gravity、solver/broadphase、compat profile 等属于
  **physicsConfigurationFingerprint**（§3.2b）：不可热修改不代表不同实例
  必然相同，兼容性校验必须显式比较；
- R1.2C 才把指纹扩展为完整 `DeterminismFingerprint`（资产+动作+配置+
  目标帧+状态 hash）。

### 3.3 刚体匹配键

只使用稳定索引；名称匹配留给未来能力层。R1.2B 的“跨模型拒绝”准确含义是
**跨物理布局拒绝**：两个 PMX 文件只要物理布局指纹相同（含 damping/
friction/restitution/关节类型等），R1.2B 允许恢复；R1.2C 再用
`modelAssetFingerprint` 收紧到具体资产。

## 4. 刚体分类与恢复语义

| 模式 | 恢复行为 |
| ---- | -------- |
| FollowBone（Mode 0） | 动画 target 必须与 snapshot.worldTransform 位级一致（§3.1 第 14 项前置）→ worldTransform 从 snapshot 写回 → interpolationTransform 从 snapshot 写回 → regular/interpolation velocity 从 snapshot 写回 → activation/deactivation 由 Saba 规范化（信息性，不恢复）→ force/torque 清零 |
| Physics（Mode 1） | world/interpolation transform、四类 velocity、activation、deactivation 从 snapshot 写回；force/torque 清零 |
| PhysicsWithBone（Mode 2） | 按 Physics 写回；合并回写语义由 Saba `DynamicAndBoneMergeMotionState` 保持；**必须用独立 Mode 2 fixture 验证（T22）** |

Kinematic 刚体的快照 position/rotation 是**前置校验输入**（§3.1 第 14 项）：
动画状态必须已经与 `snapshot.motionFrame` 一致，否则 Restore 拒绝。
canonical 快照的 FollowBone interpolation/velocity 本就与动画目标一致
（R1.2A Canonical Reset 规范化），Restore 从快照精确写回即可同时满足
位级一致与跨实例一致。

## 5. Canonical Restore Sequence（严格顺序）

```text
Phase 0  校验（§3，只读）。失败即返回，世界不变。

Phase 1  模式与 transform
         对每个刚体（按 snapshot 顺序）：
           SelectMotionStateForMode(mode)  // 语义接口，见 §11；
                                           // 只选 motion state 与
                                           // collision mode，不设激活
           全部模式（含 FollowBone）：
             写 centerOfMassTransform(position, rotationBasis)
             写 interpolationWorldTransform(
                 snapshot.interpolationTransform.position,
                 snapshot.interpolationTransform.rotationBasis
             )
             （interpolation velocities 由 Phase 2 统一写入，Phase 1 不重复）
           非 FollowBone：写 active motion state transform
             - motionFrame == 0：直接写 centerOfMassTransform
               （与 PrepareFrameZero 的
               SyncActiveMotionStateToBodyTransform 一致）
             - motionFrame > 0：写
               integrate(interpolationWorldTransform,
                         interpolationLinearVelocity,
                         interpolationAngularVelocity,
                         -fixedTimeStep)
               —— 即 Bullet synchronizeSingleMotionState 在
               m_localTime == 0（latency interpolation 开启）时写入的
               canonical 边界插值变换
             Saba 每帧 SetActivation(true) 会经
             btRigidBody::setMotionState 把该变换读回刚体，
             因此恢复 motion state 必须复刻 from-start 路径的精确值，
             不能写 centerOfMassTransform 或省略该步。

Phase 2  速度
         全部模式：写 linear/angular velocity（含 interpolation 速度）

Phase 3  力
         全部刚体：clearForces()
         （canonical 前置校验已保证快照 force/torque 为零；
          本阶段只负责把目标实例也清零）

Phase 4  碰撞世界与求解历史（语义接口，§11）
         RebuildCollisionWorldDeterministic()
           - 更新全部刚体 AABB
           - 释放旧 collision algorithms
           - 清理 contact manifolds（accumulated impulses 随 manifolds 丢弃）
           - 移除旧 overlapping pairs
           - 从恢复后的 AABB 确定性重建 overlapping pairs
           - manifolds 保持为空，等待下一次 collision dispatch
         ClearSolverHistoryDeterministic()
           - 清非接触约束（关节）applied impulses
           - 清 6DOF/spring 内部累计状态
           - 重置 solver 自身内部状态（seed / 临时缓存）

Phase 5  激活与时间边界
         Physics / PhysicsWithBone：
           NormalizeCanonicalActivation(Physics)
             // ACTIVE_TAG + deactivationTime 0
         FollowBone：
           NormalizeCanonicalActivation(FollowBone)
             // Saba canonical kinematic 规则
         physics->ResetSimulationTime()
         记录 lastExecutedSubsteps = 0

Phase 6  惯性张量与骨骼蒙皮
         实现期探针确认当前 vendored Bullet 的
         setCenterOfMassTransform 是否已内部调用 updateInertiaTensor：
           已更新 → 不重复调用
           未更新 → 由后端 RestoreTransform 接口补齐
         （上层恢复顺序不依赖该条件分支的“当前事实”）
         dynamic 刚体：ReflectGlobalTransform()
         全部刚体：CalcLocalTransform()
         根节点 UpdateGlobalTransform()
         model->UpdateNodeAnimation(true)
         （实现发现：正常 StepFrameExact 在物理反射后也会对
          after-physics 节点做动画求值；Restore 缺少这一步会留下
          物理反射的 local 矩阵，不是正常帧边界）
         model->Update()
         SyncPoseFromSaba()
```

顺序不可交换（全契约只有这一套顺序）：

```text
transform/速度/力
→ RebuildCollisionWorldDeterministic（含 AABB 更新与 pairs 重建）
→ ClearSolverHistoryDeterministic
→ NormalizeCanonicalActivation + 时间边界
→ 惯性张量/骨骼蒙皮回写
```

## 6. 隐藏状态处理清单

| 状态 | 来源 | 写回方式 | 保证 |
| ---- | ---- | -------- | ---- |
| world transform | 全部模式：snapshot | `setCenterOfMassTransform` | 全部模式位级一致 |
| interpolation transform | 全部模式：snapshot | `setInterpolationWorldTransform` | 全部模式位级一致 |
| motion state 模式 | snapshot.mode（不可变定义） | `SelectMotionStateForMode(mode)` | 与 PMX 模式一致 |
| velocity | 全部模式：snapshot | `setLinearVelocity` 等 | 全部模式位级一致 |
| force/torque | 不恢复；canonical 必须为零 | `clearForces()` | 目标实例清零 |
| activation state | Physics：snapshot（==ACTIVE_TAG）；FollowBone：不恢复 | `NormalizeCanonicalActivation(mode)` | Physics：ACTIVE_TAG；FollowBone：Saba kinematic 规则 |
| deactivation time | Physics：snapshot（==0）；FollowBone：不恢复 | `NormalizeCanonicalActivation(mode)` | Physics：0；FollowBone：Saba kinematic 规则 |
| mass / inertia / shape / joint | 不可变布局，不写回 | 只校验 | 与创建期一致 |
| AABB / overlapping pairs / manifolds | 写回后的 transform | `RebuildCollisionWorldDeterministic` | 确定性重建（含 pairs） |
| solver warm-start | 旧求解缓存 | `ClearSolverHistoryDeterministic` | 清空 |
| accumulator | Bullet `m_localTime` | `ResetSimulationTime` | 0 |

## 7. canonical 的可验证定义

三个概念完全分离：

```text
snapshot.canonical（claim）
    仅由 Capture 在边界上写入 true；Restore 把它当作必须满足的前置条件
    （§3.1 第 4、13 项），不修改它。

runtimeCanonicalBoundary（实例状态）
    RestoreState 全部 Phase 0-6 成功后置 true；
    **只有写入阶段（Phase 1-6）失败/异常才置 false 并进入 Poisoned**；
    Phase 0 校验拒绝保留调用前状态（§8.1）。

deterministicPrepared（状态机）
    R1.2B RestoreState 完成后恒为 false（§9.3）。
    R1.2C 恢复完整 Checkpoint 后才允许置 true。
```

因此不存在“输入 canonical 声明 == 输出边界 == 状态机”的循环：

```text
输入：canonical claim（校验）
输出：runtimeCanonicalBoundary（写回结果）
状态机：deterministicPrepared（R1.2C 才恢复）
```

## 8. 事务性与失败语义

### 8.1 校验拒绝原子性（R1.2B 唯一的“原子”承诺）

```text
Phase 0 任一失败 → 世界状态完全不变，返回 SnapshotMismatch /
InvalidSnapshot / InvalidState / NoPhysics / UnsupportedReplayProfile。
调用前的 runtimeCanonicalBoundary、deterministicPrepared、时间标签与
diagnostics 全部保留。
```

状态码区分：

```text
快照内容前置不满足（值/结构/canonical 声明）→ InvalidSnapshot
快照与当前布局/配置不匹配                       → SnapshotMismatch
当前实例动画前置不满足                           → InvalidState
```

### 8.2 写入期灾难性失败 → Poisoned

Phase 1-6 若抛出异常，不承诺回滚（Bullet 可能处于中间态）。实例进入
**Poisoned** 状态：

```text
runtimeCanonicalBoundary = false
deterministicPrepared = false
此后确定性入口的准入规则：
    RestoreState                     → Poisoned
    StepMotionFrameExact             → Poisoned
    EvaluateTick(其他策略)           → Poisoned
    PrepareFrameZero()               → 允许，作为恢复入口
    EvaluateTick(0, ResetAtTarget)   → 允许，作为恢复入口
观察接口：
    CapturePhysicsSnapshot           → Poisoned（不修改 output）
    HashPhysics                      → invalid
    ReadStepDiagnostics              → 允许只读，返回 Ok，
                                       并在 diagnostics 标记 poisoned=true
恢复入口成功后清除 Poisoned；恢复入口失败则继续保持 Poisoned。
（R1.2C 提供 Checkpoint 重建路径）
```

契约措辞统一为“校验拒绝不修改世界 + 写入期 Poisoned”，**不使用
“原子写回/部分成功”**。

`PhysicsStepDiagnostics` 在 R1.2B 实现阶段增加 `bool poisoned = false`
字段，用于只读诊断表达 Poisoned 状态。

### 8.3 错误状态映射

| 条件 | 状态码 |
| ---- | ------ |
| 无 MMDPhysics 世界 | `NoPhysics` |
| 物理禁用 | `UnsupportedReplayProfile` |
| schemaVersion 不匹配 | `SnapshotMismatch` |
| 指纹/当前世界不匹配（含逐体篡改） | `SnapshotMismatch` |
| 数量/索引/jointCount 不匹配 | `SnapshotMismatch` |
| 物理配置指纹不匹配 | `SnapshotMismatch` |
| mode 或 definitionMass 与当前布局定义不一致 | `SnapshotMismatch` |
| definitionMass 非 finite / 负值 | `InvalidSnapshot` |
| 非 finite / 非法 basis / 非法数值 | `InvalidSnapshot` |
| canonical claim 缺失或前置条件不满足 | `InvalidSnapshot` |
| 动画前置不满足 | `InvalidState` |
| 实例处于 Poisoned | `Poisoned` |
| Phase 1-6 异常 | `Poisoned`（实例需重建） |

## 9. 恢复后的语义

### 9.1 Restore 后立即 Capture

允许。动画前置（§3.1 第 14 项）保证当前动画已与 `snapshot.motionFrame`
一致，因此：

```text
全部模式的可观察字段位级一致（transform/interpolation/四类速度）；
FollowBone 仅排除 activation/deactivation（信息性，由
NormalizeCanonicalActivation 规范化）。
```

`motionFrame` / `physicsTick` 按快照写回；`canonical` 由
`runtimeCanonicalBoundary` 决定。

### 9.2 Restore 后 ReadStepDiagnostics

```text
executedSubsteps == 0
remainingAccumulator == 0
```

### 9.3 Restore 后状态机

**R1.2B 完成后 `deterministicPrepared = false`。** 直接
`StepMotionFrameExact` 返回 `InvalidState`；调用方若想从头重放，显式
`EvaluateTick(target, ReplayFromStart)`（内部 PrepareFrameZero，会覆盖
恢复状态——这是显式选择）。R1.2C 的 `ReplayFromCheckpoint` 会先恢复
动画/覆盖/fingerprint 并**重建** Kinematic target，再置 `prepared=true`
并继续。

### 9.4 骨骼/蒙皮

Phase 6 完成时，WISTERIA Pose 与 Saba 骨骼已反映恢复后的物理状态。

### 9.5 恢复后第一步

R1.2B 不提供公开的“直接从恢复状态步进”路径（`StepMotionFrameExact`
在 `prepared=false` 时返回 `InvalidState`）。

为验证隐藏历史确实被清空，测试构建（
`#if defined(WISTERIA_DETERMINISM_TEST_HOOKS)`）开放一个 Adapter 内部
探针：

```cpp
// 测试专用：不修改公开 deterministicPrepared 契约，不重新求值动画，
// 只把恢复后的 Bullet 世界推进 exactSubsteps 个固定子步。
TimelineStatus StepRestoredPhysicsForProbe(
    std::uint32_t exactSubsteps
);
```

T5 / T18 通过该探针做“恢复同一快照 → 各走一步 → exactHash 一致”的验收；
它不开放动画、Morph、IK 或状态机路径。

## 10. 确定性等价承诺边界

R1.2B **承诺**：

```text
1. 校验拒绝不修改世界；
2. 只接受 canonical 快照；
3. 恢复后 dynamic 可观察字段与快照一致（动画前置保证下的 Capture）；
4. 隐藏缓存按 §5 清理；
5. canonical 三概念按 §7 分离；
6. 写入期失败 → Poisoned，恢复路径明确。
```

R1.2B **不承诺**：

```text
restore → replay == from-start
```

等价性需要真实轨迹验证（关节顺序、Bullet 浮点路径、历史清理的实际效果），
是 R1.2C 的验收项。

## 11. vendored Saba 窄接口最小范围

原则：**先试 public API，缺什么补什么；任何新增窄接口先写进本清单；
禁止向 WISTERIA 上层暴露 `bt*` 类型**。

| 窄接口 | 用途 | 触发条件 |
| ------ | ---- | -------- |
| `PmxRigidBodyDefinition` 访问器（mode、definitionMass、bone index、offset、group/mask、shape 类型+尺寸、margin、linear/angular damping、restitution、friction） | layoutFingerprint / physicsConfigurationFingerprint 与恢复模式判定 | `MMDRigidBody` 私有字段无 public 访问器 |
| `PmxJointDefinition` 访问器（type、A/B、frames、limits、springs、所有 type-specific 静态参数） | layoutFingerprint | `MMDJoint` 私有字段无 public 访问器 |
| `MMDPhysics::RebuildCollisionWorldDeterministic()` | 语义化碰撞世界重建：AABB 更新、algorithms/manifolds/pairs 清理与确定性重建（§5 Phase 4） | `updateSingleAabb` / `cleanProxyFromPairs` 等零散调用不足以满足语义 |
| `MMDPhysics::ClearSolverHistoryDeterministic()` | 语义化 warm-start 清理：非接触约束 applied impulses、6DOF/spring 内部状态、solver seed/临时缓存（§5 Phase 4） | `btSequentialImpulseConstraintSolver::reset()` 不足 |
| `MMDRigidBody::SelectMotionStateForMode(mode)` | Phase 1：选择 Kinematic/Dynamic motion state 并设置必要 collision mode；不设置 ACTIVE_TAG/deactivation | `SetActivation` 语义混合了两件事 |
| `MMDRigidBody::NormalizeCanonicalActivation(mode)` | Phase 5：Physics → ACTIVE_TAG + deactivation 0；FollowBone → Saba canonical kinematic 规则 | 裸 `setActivationState` 会被 DISABLE_DEACTIVATION / DISABLE_SIMULATION 拒绝 |
| `MMDPhysics::ResetSimulationTime()` | accumulator 清零 | 已有（R1.2A） |

`RebuildCollisionWorldDeterministic` / `ClearSolverHistoryDeterministic` /
`SelectMotionStateForMode` / `NormalizeCanonicalActivation`
是**实现前置要求**，不是待验证假设；实现后必须通过 T18 的
divergent-history one-step 测试。

## 12. 自动化测试矩阵与冻结标准

### 12.1 测试矩阵（R1.2B 实现后必须全绿）

| 编号 | 用例 | 断言 |
| ---- | ---- | ---- |
| T1 | Capture（canonical）→ 扰动 → Restore → Capture（动画一致） | 全部模式的 transform/interpolation/velocity 位级一致；FollowBone 仅排除 activation/deactivation |
| T1a | 任意旋转 Capture→Restore→Capture（含近 180°、world 与 interpolation 分别验证） | 9 个 basis 分量位级一致 |
| T2 | Restore 幂等：连续两次 Restore 同一快照 | 两次后 Capture 相同（FollowBone 的信息性 activation/deactivation 字段不参与比较） |
| T3 | Restore 后 ReadStepDiagnostics | substeps==0、accumulator==0 |
| T4 | Restore 后零步 Capture | 与恢复后立即 Capture 相同 |
| T5 | 同一快照两个实例各 Restore，经 `StepRestoredPhysicsForProbe(4)` 走一步 | **Physics exactHash** 一致（B 内可验证的同起点一步） |
| T6 | 非 finite position | `InvalidSnapshot`，世界不变 |
| T7 | 非法 rotationBasis：NaN/Inf、列长错误、列不正交、determinant≈-1（world 与 interpolation 分别覆盖） | `InvalidSnapshot`，世界不变 |
| T8 | 刚体数量不匹配 | `SnapshotMismatch`，世界不变 |
| T9 | 布局不匹配：同数量同 definitionMass，但 shape/Mode/关节拓扑不同，或 damping/friction/restitution 不同 | `SnapshotMismatch` |
| T9b | 相同 PMX 布局，分别修改 gravity / effective margin / fixed timestep / solver iterations 或 mode / split impulse 或其他有效 solver 字段 / compatibility profile | `SnapshotMismatch`，世界不变 |
| T10 | schemaVersion 不匹配 | `SnapshotMismatch` |
| T11 | 篡改 definitionMass/mode，保留旧 fingerprint | `SnapshotMismatch`（逐体直接比较失败） |
| T12 | 跨物理布局恢复 | `SnapshotMismatch`；相同物理布局的不同模型在 R1.2B 允许（R1.2C 用 modelAssetFingerprint 收紧） |
| T13 | canonical=false 快照 | `InvalidSnapshot` |
| T14 | canonical 前置不满足（force/torque≠+0.0f 位模式 / Physics activation≠ACTIVE_TAG / Physics deactivationTime≠+0.0f / physicsTick≠frame×4） | `InvalidSnapshot`（FollowBone activation 由 NormalizeCanonicalActivation 规范化，不作输入合法性条件） |
| T15 | FollowBone 恢复后跟随动画 | 动画改变后位置随骨骼 |
| T16 | 动画帧与 snapshot.motionFrame 不一致，或 FollowBone transforms 不匹配 | `InvalidState`，世界不变 |
| T17 | Restore 后直接 StepMotionFrameExact | `InvalidState`（prepared=false） |
| T18 | 长时间/不同历史实例恢复同一快照，经 `StepRestoredPhysicsForProbe(4)` 走一步（divergent history） | 一步 **Physics exactHash** 一致 |
| T19 | 目标实例先制造 DISABLE_DEACTIVATION 历史，再用合法 canonical 快照恢复 | 恢复成功且动态刚体为 ACTIVE_TAG、deactivationTime==0；DISABLE_DEACTIVATION 期间捕获的非 canonical 快照被拒绝 |
| T20 | 写入期故障注入（测试钩子） | 实例 Poisoned；RestoreState/Step/普通 EvaluateTick 返回 Poisoned；PrepareFrameZero 与 EvaluateTick(0, ResetAtTarget) 作为恢复入口可用 |
| T21 | 连续 Capture/Restore 1000 次 | 无漂移（每轮后 Capture 一致；FollowBone 信息性字段不参与比较） |
| T22 | Mode 2（PhysicsWithBone）独立 fixture | 恢复后合并回写行为与创建期一致 |
| T23 | FollowBone 刚体 PMX 原始 definitionMass 非零 | Capture/Restore 合法；Bullet 仍按 kinematic 运行（运行时质量由 Mode 决定） |
| T24 | ASan+UBSan 下 T1-T23 | 0 sanitizer 报告 |

### 12.2 冻结标准

```text
Windows MSVC CORE / FULL_ASSETS：CTest 5/5
Linux g++ CORE / FULL_ASSETS：CTest 5/5
Linux ASan+UBSan（-O1）：integration 全 PASS、0 sanitizer 报告
契约复查通过（无自相矛盾的承诺）
```

## 13. 明确不实现（R1.2B 边界重申）

- `FrameCheckpoint` / `ReplayFromCheckpoint`（R1.2C）；
- 动画/Morph/IK override / ReplayConfig / Kinematic target 的恢复（R1.2C）；
- `DeterminismFingerprint` 完整实现（R1.2C；R1.2B 只有 layoutFingerprint）；
- restore→replay 等价性证明（R1.2C）；
- Snapshot 的 C ABI 导出（R1.4）；
- 跨线程 Restore；
- Bullet 高级参数控制面；
- 刚体名称匹配 / 布局热迁移；
- 任意 force/torque 恢复（canonical 快照恒为零）。
- `modelAssetFingerprint`（R1.2C；R1.2B 只拒绝跨物理布局，不拒绝同布局
  不同模型）。
- `BT_USE_DOUBLE_PRECISION` 构建的 snapshot restore（R1.2B 冻结为
  `sizeof(btScalar)==4`，双精度 schema 留待未来）。

## 14. 实现前置验证（升级为硬性前置，不再叫“假设”）

```text
V1. RebuildCollisionWorldDeterministic 满足 §5 Phase 4 全部语义，
    并以 T18（divergent-history one-step）证明。
V2. ClearSolverHistoryDeterministic 清空非接触约束 applied impulses、
    6DOF/spring 内部累计状态与 solver 内部状态，并以 T18 证明。
V3. SelectMotionStateForMode / NormalizeCanonicalActivation 能可靠地把
    恢复后的动态刚体置于 ACTIVE_TAG、FollowBone 置于 Saba kinematic 规则
    （不被 DISABLE_DEACTIVATION / DISABLE_SIMULATION 拒绝），T19/T23 验证。
V4. PmxRigidBodyDefinition / PmxJointDefinition 访问器返回的数据与
    PMX 文件一致（含 definitionMass、damping、restitution、friction、
    关节类型），T9/T11/T23 验证。
V5. Mode 2 恢复后 DynamicAndBoneMergeMotionState 回写行为与创建期一致，
    T22 验证。
```

任一项不通过，R1.2B 不得进入实现验收；缺什么就补窄接口，不降级契约。
