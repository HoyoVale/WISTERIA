# R1.3B — MMD 社区实现对照契约（MMD_COMMUNITY Phase 0B）

> 状态：**Phase 0B Contract Frozen（2026-08-07）**。R1.3 Phase 0A
> （Frozen + Implemented + Validated，2026-08-07）之后的主线。
> 本契约只冻结 **Phase 0B 的范围、证据门槛与裁决流程**，不冻结任何
> 社区规则内容；每一条规则必须携带可复现证据包才能进入 `MMD_COMMUNITY`。
>
> v1 评审结论（2026-08-07）：
> **Direction / Evidence model / Rule gating / Scope：APPROVED**；
> **Clock semantics / Execution normalization / Coordinate normalization /
> Acceptance semantics / Profile revision policy：NEEDS FIX**。
> v2 只做增补与修正，不重写设计。
>
> v2 冻结前闭合（评审轮 2，2026-08-07）：帧边界顺序（Frame 0 特例 +
> N>=1 精确执行序）、角速度伪矢量公式、跨实现 PMX source identity、
> reference artifact 身份（package version/integrity 与 source commit
> 分离）、中间 Trace 可观测性标志。八项拍板点全部转为 Frozen decisions。
>
> 前置规范源：
> [R1_3_MMD_COMPAT_CONTRACT.md](R1_3_MMD_COMPAT_CONTRACT.md)
> （Phase 0A Frozen）、[R1_2_DETERMINISTIC_TIMELINE_CONTRACT.md](R1_2_DETERMINISTIC_TIMELINE_CONTRACT.md)。
> 已有工具与历史文档：
> `tools/reference_trace/README.md`（babylon-mmd 对照 harness，已跑通）、
> `docs/architecture/MMD_PHYSICS_COMMUNITY_ADOPTION_PLAN.md`
> （历史计划，本契约是其执行版，冲突处以本契约为准）。

## 0. 阶段定位

```text
R1.2：确定性时间线与状态所有权（完成）
R1.3 Phase 0A：MMD 物理兼容与后端治理（完成）
R1.3 Phase 0B：社区实现对照（本契约）
R1.4：稳定 Runtime 接口、序列化与外部集成边界
      （Checkpoint 序列化、C ABI、帧转换边界）
```

Phase 0B 的一句话定义：

> 把 `SabaBaseline / MMD_RAW` 与 babylon-mmd、nanoem、libmmd 等社区
> 实现做逐刚体、逐帧的确定性轨迹对照，用证据裁决第一批候选规则；
> 不裁决“哪个实现是官方答案”，只裁决“哪些规则有足够证据进入
> `MMD_COMMUNITY` 并携带独立开关”。

## 1. v1 → v2 变更记录

```text
1. 统一 Clock：motionFrame / physicsTick / simulatedSeconds，
   冻结 30Hz 动作 = 4 × 120Hz 物理 tick 的映射
2. 叶瞬光首次 300-frame 结果降级为 Historical Preliminary
   Observation，统一时间轴后复验
3. 增加 environmentMode：NormalizedComparison /
   NativeCompatibilityAudit；executionProfile 不同只能当观察证据
4. 冻结坐标转换：ReferenceCoordinateNormalization v1
   （位置/线速度/旋转基/骨骼矩阵的反射公式 + synthetic golden test）
5. 参考实现分级：Runnable Trace / Source Semantics / Historical
   Reference
6. 对照矩阵要求改为：babylon 强制 Runnable + 每条规则至少一个独立
   secondary source；nanoem runnable 视 feasibility spike 决定
7. 完成标准改为“裁决 2–4 个候选规则”，允许 0 条 COMMUNITY admission
8. 每次 COMMUNITY 有效行为变化都 bump profileRevision；
   同一 revision 一经发布不可变
9. Diagnostic 行为若被吸收，必须经契约评审晋升为正式语义名称
   （如 FullTransformWriteback），FullTransformDiagnostic 只作诊断入口
10. 许可证拆成 Evidence eligibility 与 Code reuse eligibility 两个维度
```

### v2 → Frozen 闭合记录（评审轮 2，2026-08-07）

```text
A. Clock 冻结 Frame 0 特例与 N>=1 精确执行顺序（§4.1）
B. angularVelocity 冻结 ω' = det(S)·S·ω = -Sω（§5）
C. 跨实现 identity 固定使用 PMX source
   rigidBody / bone / joint index（§4.4）
D. reference artifact identity 区分 source commit 与
   package version / integrity（§4.3）
E. 中间 Trace schema 增加 observable availability，
   unavailable 只能 NOT_COMPARABLE（§6）
```

## 2. 目标与非目标

### Phase 0B 目标

```text
1. 参考实现选型、分级与许可证核实
   （babylon-mmd / nanoem / libmmd，各 pin 到具体 commit）
2. 统一 Clock 与坐标归一化验证（synthetic calibration fixture）
3. 冻结对照 corpus：3 类资产 × {无 VMD, 有 VMD}，
   PMX/VMD 文件 hash、初始 transform、固定步全部固定
4. 参考轨迹导出与 WISTERIA JSONL Trace 对齐：
   逐刚体（world / interpolation / motion-state）、骨骼、
   joint raw/violation、contact topology
5. Trace diff 工具升级：
   first state divergence / first contact-topology divergence /
   first motion-state divergence / first bone divergence
6. 裁决首批候选规则（linked-body、Mode 2、单位/重力、
   首份对照中已发现的分歧）
7. 规则进入 `MMD_COMMUNITY`：每一条都携带完整证据包
8. 产出规则登记表与对照报告（traces/ 归档）
```

### 明确不做（Phase 0B）

```text
- 声称哪个实现是“MMD 官方正确答案”
- 引入无证据的社区规则
- 修改 SabaBaseline / MMD_RAW 的既有行为
  （Phase 0B 只产生 MMD_COMMUNITY 增量，且必须独立开关）
- 把参考实现代码移植进 Runtime
  （看懂行为 → 写成中立行为规范 → WISTERIA 独立实现）
- 重写 Bullet / Saba / babylon-mmd
- Checkpoint 序列化、C ABI、帧转换边界（R1.4）
- 跨机器 / 跨构建 exact replay 承诺
- 用“视觉更像 MMD”作为唯一证据
```

## 3. 参考实现分级与许可证

### 3.1 分级

```text
Runnable Trace        能产生逐刚体/逐帧轨迹，参与对照矩阵
Source Semantics      独立源码语义参考，参与规则证据，不要求可运行
Historical Reference  仅作历史/设计线索，不是数值真值
```

| 实现 | 分级 | 许可证（已核实） |
| ---- | ---- | ---------------- |
| babylon-mmd | **Runnable Trace（强制）** | MIT |
| nanoem（physics 组件） | **Source Semantics（强制）**；是否升级 Runnable 由 feasibility spike 决定 | physics 组件 MIT/X11；应用层（emapp/win32/macos 等）MPL |
| libmmd | **Historical Reference** | Boost Software License 1.0（宽松，但作者标记 obsolete） |
| saba | 内部基线（WISTERIA 已 vendor） | MIT |
| blender_mmd_tools | 模型制作/修复侧参考，不作数值真值 | GPLv3（仅证据，不复制） |
| Bullet 官方 | API/求解器语义查证 | zlib |

### 3.2 许可证两维度（v2 冻结）

```text
Evidence eligibility   是否可作为行为事实、源码位置、设计语义的证据
Code reuse eligibility 是否允许复制/改写具体实现代码进入 WISTERIA

证据可引用：MIT/X11、Apache-2.0、Boost-1.0、BSD、zlib
            （GPL/MPL 覆盖代码默认只作证据，不复制实现）
代码可复用：默认全部禁止——Phase 0B 不移植参考代码，
            只输出中立行为规范，由 WISTERIA 独立实现
```

每个被引用仓库必须记录 commit hash、许可证全文位置与两种 eligibility。

## 4. 统一 Clock 与对照 corpus

### 4.1 统一 Clock（v2 冻结）

```text
motionFrame    VMD 动作帧（30Hz 主循环单位）
physicsTick    物理固定步（120Hz）
simulatedSeconds = motionFrame / 30

映射（唯一）：
  motionFrame 0   = physicsTick 0    = 0.000s
  motionFrame 1   = physicsTick 4    = 0.033s
  motionFrame 10  = physicsTick 40   = 0.333s
  motionFrame 300 = physicsTick 1200 = 10.000s

帧边界执行顺序（唯一，v2 冻结）：

motionFrame 0：
  Evaluate animation at frame 0
  → kinematic sync
  → NO physics step
  → output boundary physicsTick 0

motionFrame N（N >= 1）：
  input boundary  = motionFrame N-1 / physicsTick 4(N-1)
  sample animation at motionFrame N
  → kinematic sync
  → execute physics ticks 4(N-1)+1 ... 4N（恰好 4 个 120Hz tick）
  → dynamic writeback
  → output boundary = motionFrame N / physicsTick 4N

Frame 0 是 prepared boundary，不是 stepped frame。

synthetic clock calibration fixture 必须验证：

frame 0   → 累计 0 ticks
frame 1   → 累计 4 ticks
frame 2   → 累计 8 ticks
frame 300 → 累计 1200 ticks

有 VMD 时：在 motionFrame N 对齐动画采样语义，然后执行该帧对应的
4 个 120Hz physics ticks（顺序同上）。
```

任何对照报告禁止单独使用模糊的 `frame`；必须同时给出
`motionFrame / physicsTick / simulatedSeconds`。

**叶瞬光首次 300-frame 对照（±10.5 → ±6.8）降级为
Historical Preliminary Observation**：该结果来自 babylon harness 的
120Hz tick 计数（300 tick = 2.5s），与 WISTERIA motionFrame 300
（= 10s）不是同一时间轴，统一 Clock 后必须复验。

### 4.2 对照 corpus

```text
3 类资产（从 FULL_ASSETS 与可授权资产中选取）：
1. 简单标准模型（少刚体、Mode 0/1 为主）
2. 复杂裙摆/长发/尾巴模型（Mode 2 密集）
3. Mode 1 / Mode 2 混合明显、带关节限制的模型

每个资产固定：
- PMX 文件 hash、VMD 文件 hash（沿用 R1.2C AssetIdentity 语义）
- 初始 transform（模型原点、朝向、缩放）
- 比较时间点：motionFrame 0 / 10 / 30 / 100 / 300
  （长跑可加 720 / 1200，对应 simulatedSeconds 24 / 40）
```

### 4.3 两种 Evidence Mode（v2 冻结）

```text
NormalizedComparison
  用于隔离某一条 MMD 行为：时间轴、重力、ground、solver/execution
  policy 尽可能对齐；不能对齐的差异必须显式记录。
  例：同为 gravity=-98、120Hz、同一 ground policy 下对比 Mode 2。

NativeCompatibilityAudit
  各播放器按自己的原生 MMD 行为运行：保留各自的 gravity、ground、
  reset、solver defaults，研究这些差异本身。
  例：babylon-mmd 原生无 MMD ground vs saba 有 y=0 静态平面。
```

每份 Trace 必须携带环境头：

```text
environmentMode
executionProfile
gravity
fixedTimeStep
groundPolicy
sourceRepositoryCommit      源码仓库 commit（如 noname0310/babylon-mmd @ abcdef...）
referencePackageName        实际运行的发布包（如 babylon-mmd）
referencePackageVersion     发布包版本（如 3.x.y）
referencePackageIntegrity   发布包 integrity（如 sha512-...，来自 lockfile）
physicsPackageName          物理后端包（如 ammojs-typed）
physicsPackageVersion       物理后端版本
physicsBackendVersion       物理引擎二进制版本
```

规则源码证据引用 commit A 时，实际 trace 必须运行同一 commit 或明确
记录发布包差异（npm artifact 与源码 commit 不是同一份代码）。

规则：

```text
executionProfile 不相同的两条轨迹，可以作为观察证据，
但不能直接声称某个 compatibility switch 是因果来源。
```

### 4.4 跨实现身份（v2 冻结）

```text
Cross-implementation identity is the source PMX index.

- 刚体：sourceRigidBodyIndex（PMX rigid body index）
- 骨骼：sourceBoneIndex（PMX bone index）
- 关节：sourceJointIndex（PMX joint index）

Runtime-local object order MUST NOT be used as cross-reference identity。

如果实现重排了对象：
  adapter 必须提供 runtimeIndex → sourcePmxIndex 映射；
如果对象无法映射：
  标记 unmapped；
  禁止按向量位置静默比对。
```

没有 source identity 的逐体 diff 结论（如“body 274 分叉”）不具备
跨实现证据效力。

## 5. 坐标归一化（ReferenceCoordinateNormalization v1）

坐标系反射（saba 与 babylon-mmd 的 Z 轴相反）：

```text
S = diag(1, 1, -1)
H = diag(1, 1, -1, 1)

position:        p'  = S p
linear velocity: v'  = S v
rotation basis:  R'  = S R S
bone transform:  T'  = H T H
```

规则：

```text
- angular velocity 是 axial vector / pseudovector，反射下冻结为：
    ω' = det(S) · S · ω = -S · ω
  Z-reflection 下：
    ω = (ωx, ωy, ωz)  →  ω' = (-ωx, -ωy, +ωz)
  （与 linear velocity v' = (+vx, +vy, -vz) 不同）
- 跨实现旋转以 rotation basis（3×3 矩阵）为标准表示，
  不直接比较经过符号转换的 quaternion；
- 所有 reference adapter 输出 WISTERIA canonical coordinate。
```

验收：synthetic transform fixture golden test 必须覆盖
translation / X / Y / Z rotation / combined transform /
linear velocity / 正 X / 正 Y / 正 Z angular velocity；
通过后才能采真实资产证据。

## 6. 对照指标与最小差异报告

```text
逐刚体：
  world / interpolation / motion-state 三套变换
  linearVelocity / angularVelocity
骨骼：
  local / global 矩阵
Joint：
  rawLinearError / linearViolation / rawAngularErrorDeg /
  angularViolationDeg（Phase 0A 公式，Euclidean norm）
Contact topology：
  接触对集合（含 ground 哨兵），pair 增删即拓扑分叉

汇总输出：
  First divergence: frame / body / positionError / rotationErrorDeg
  Maximum divergence: frame / body / positionError / rotationErrorDeg
  First contact-topology divergence: frame / pairA / pairB
  First motion-state divergence: frame / body
  First bone divergence: frame / bone / maxMatrixDelta
  Joint error delta: joint / linear / angular
```

验收：人为注入分叉（body 位置、接触对、骨骼矩阵、motion-state）必须被
对应 diff 定位；Phase 0A 已具备 body 级定位，Phase 0B 扩展到其余三类。

### 字段可观测性（v2 冻结）

```text
worldTransformAvailable
interpolationTransformAvailable
motionStateAvailable
linearVelocityAvailable
angularVelocityAvailable
jointMetricsAvailable
contactTopologyAvailable
```

规则：

```text
- Reference adapter 只能输出它能权威观察到的字段；
  不能为满足统一 schema 而推导、填零或伪造；
- diff 时：两侧 available → 比较；
  任一侧 unavailable → 报告 NOT_COMPARABLE；
  绝不能当作 0 比较；
- nanoem 若第一轮仅为 Source Semantics，不因缺少数值 trace
  而被强行包装成 runnable。
```

## 7. 规则裁决流程（Rule Admission）

### 7.1 候选规则证据包（六项缺一不可）

```text
1. 源码位置：repo + commit + file/line
   （Saba / babylon-mmd / nanoem / libmmd 等）
2. 许可证确认：Evidence eligibility 成立，记录许可证全文位置
3. 问题解释：它解决了什么、在哪个实现上观察到什么差异
4. 独立开关：MmdPhysicsCompatibilityProfile /
   MmdPhysicsAdaptivePolicy 字段（新字段需契约评审）
5. 自动化测试：单元 + 集成，且默认档行为不因此改变
6. 轨迹 A/B：同一资产/动作、同一 environmentMode 与 executionProfile
   下 开关前 vs 开关后 的轨迹对比
```

证据链分层（v2 冻结）：

```text
Cross-implementation comparison
  建立外部观察与源码假设（发现差异）

WISTERIA controlled A/B（同一 environmentMode 与 executionProfile）
  建立候选 WISTERIA 规则的因果证据（证明开关导致预期变化）

社区源码/轨迹发现差异 → 候选规则 → WISTERIA 独立实现开关 →
同环境 A/B → Admission decision
```

不要求 babylon 复制 WISTERIA Cold-Step 才能做研究；executionProfile
差异本身由 NativeCompatibilityAudit 承担。

### 7.2 裁决结果（v2 冻结）

```text
ADMITTED_COMMUNITY   进入 MMD_COMMUNITY
ADMITTED_ADAPTIVE    进入 WISTERIA_ADAPTIVE
REJECTED             证据不足或与目标冲突
INCONCLUSIVE         证据互相矛盾，继续观察
REFERENCE_SPECIFIC   只属于某个参考实现，不作为通用规则
```

**零 COMMUNITY admission 也是合法研究结果**：若证据显示 Saba 已合理、
社区实现互相矛盾或证据不足，Phase 0B 依然完成。

### 7.3 Preset revision 策略（v2 冻结）

```text
- 任何 effective preset behavior 变化都必须增加 profileRevision；
- 增加、删除、修改或回滚一条已发布 COMMUNITY 规则都属于 effective
  behavior change → 必须 bump revision，永远不复用旧 revision；
- 同一 revision 一经发布，其含义不可变；
- COMMUNITY v1 == RAW；
  rule A → COMMUNITY v2；
  rule B → COMMUNITY v3；
  rule C → COMMUNITY v4；
- Phase 0A validator 当前硬编码 profileRevision == 1；
  吸纳第一条 COMMUNITY 规则时，必须升级为
  “接受该 Preset 当前冻结 revision”（属于 Phase 0B 实现项）。
```

### 7.4 Diagnostic 晋升规则（v2 冻结）

```text
诊断开关可以成为证据来源，但不能不改名字就变成正式兼容语义。
例：若证据证明“完整平移+旋转写回”属于 COMMUNITY 行为，
必须先经契约评审晋升为正式语义（如 FullTransformWriteback），
FullTransformDiagnostic 继续只作诊断入口。
```

### 7.5 规则登记表

每条规则写入 `docs/validation/R1_3B_RULE_REGISTRY.md`（人工评审的
Source of Truth）：

```text
rule-id
裁决（§7.2 五选一）
源码证据（repo / commit / file:line）
许可证（Evidence eligibility）
开关字段
测试名
轨迹 A/B 链接
preset 身份变化（如 mmd-community-v1 → v2）
裁决日期与评审人
```

## 8. 首批候选规则清单（排期建议，不冻结内容）

```text
1. Linked-body collision 归属裁决
   PmxMaskOnly vs DisableConstraintLinkedPairs 谁是 COMMUNITY 行为；
   前置条件：专用 linked-pair 重叠 fixture（Phase 0A 已记录缺失）
2. Mode 2 写回归属裁决
   PreserveAnimatedTranslation vs FullTransformDiagnostic；
   StrictBoneLength 保持 Reserved，直到有算法定义
   （libmmd strict 模式只能提供定义线索，不是正确答案）
3. 单位与重力裁决
   回答“PMX 1 单位在 WISTERIA 中具体代表什么”；
   输入：Phase 0A 单位审计 + NativeCompatibilityAudit
4. 叶瞬光 mode-2 分歧专项（Historical Preliminary）
   统一 Clock + 逐刚体轨迹对比 → 第三参考（nanoem source semantics）
   → 判定归属；不得直接引用旧 frame300 数值
5. 其他候选（只列项，不冻结）
   damping / friction 默认值、激活策略、impulse morph、
   ground 行为（用 NativeCompatibilityAudit）、joint spring 语义
```

## 9. 工具链改动

```text
1. tools/reference_trace
   - 补 package.json + lockfile + reference commit pin
     （exact dependency versions；正式采证前必须完成）
   - trace.mjs 升级：逐刚体 position/rotation（每 motionFrame 每刚体），
     统一 Clock 输出 motionFrame / physicsTick / simulatedSeconds
   - 增加有 VMD 运行路径、固定资产清单、环境头输出
   - 输出 WISTERIA canonical coordinate（§5 归一化）
2. WISTERIA
   - tools/trace diff 升级：contact-topology / motion-state / bone
     三类差异定位（Phase 0A 已具备 body 级）
   - 不把 reference 工具链塞进 Runtime
3. 对照编排脚本
   - 跑 reference + WISTERIA → 统一中间格式 → diff 报告 → 归档
```

## 10. 测试与验收

```text
规则级：
  每条被裁决规则 = 单元 + 集成 + 轨迹 A/B 前后记录
对照矩阵（v2 修订）：
  babylon-mmd 强制 Runnable；
  每条候选规则至少一个独立 secondary source
  （nanoem Source Semantics 保底，Runnable 视 feasibility）；
  libmmd 仅 Historical Reference，不进入矩阵要求；
  3 类资产 × {无 VMD, 有 VMD} × motionFrame 300
门禁：
  Windows/Linux × CORE/FULL_ASSETS CTest 5/5 继续全绿；
  reference_trace 作为可选对照作业，不阻塞 C++ 门禁；
  产物归档到 traces/（本地；公开资产合规见 §12 拍板点 7）
完成标准：
  2–4 个候选规则获得完整证据包并完成最终裁决
  （裁决允许 §7.2 五种结果，含 0 条 COMMUNITY admission）；
  规则登记表与对照报告产出
```

## 11. 与 R1.4 的边界

```text
- Phase 0B 期间不实现 Checkpoint 序列化、C ABI、帧转换边界；
- 新规则需要新行为字段时：字段必须可归属（COMMUNITY vs ADAPTIVE），
  进入 fingerprint 的方式沿用 Phase 0A 设计（只 Hash 有效行为）；
- Phase 0B 的 Trace 证据全部来自 Phase 0A 的 canonical 轨迹设施，
  不允许用实时历史做证据；
- profileRevision 门禁升级（§7.3）属于 Phase 0B 实现项，
  不改变 Phase 0A 已冻结的 fingerprint 语义。
```

## 12. 推荐执行顺序

```text
1. 本契约评审（已完成：Phase 0B Contract Frozen，2026-08-07）
2. 许可证与 commit pin 核实
   （含 tools/reference_trace package.json + lockfile）
3. synthetic clock calibration fixture
   （证明两个 harness 的 motionFrame N 语义一致）
4. ReferenceCoordinateNormalization v1 golden test
5. reference_trace 升级：逐刚体导出 + 有 VMD 路径 + 环境头
6. WISTERIA diff 工具升级（contact / motion-state / bone）
7. nanoem feasibility spike（升级 Runnable 与否）
8. 对照 corpus 冻结（3 类资产 + hash）
9. 首批候选规则 A/B 轨迹采集（含叶瞬光专项复验）
10. 规则裁决与证据包归档
11. MMD_COMMUNITY 首条规则实现 + 测试
12. 四套矩阵 + 对照报告
13. Phase 0B 完成评审 → 规则登记表冻结
```

## 13. 拍板裁决（Frozen decisions，2026-08-07）

```text
1. 参考实现
   批准但收窄：babylon-mmd 强制 Runnable；
   nanoem 强制 Source Semantics，Runnable 由 feasibility spike 决定；
   libmmd 仅 Historical Reference
2. 许可证
   修改：不写“仅 MIT/Apache”；
   宽松兼容许可（MIT/X11、Apache-2.0、Boost-1.0、BSD、zlib）可作证据；
   GPL/MPL 默认只作证据不复制；Code reuse 默认全禁
3. StrictBoneLength
   批准：继续 Reserved；libmmd 只提供定义线索
4. linked-pair 重叠 fixture
   批准：Phase 0B 首批工程项
5. 默认档
   批准：COMMUNITY 规则生效后默认仍 MMD_RAW；
   Phase 0B 结束后可重新评估
6. reference CI
   批准但加 pin：可选、不阻断 C++；
   必须 exact dependency versions + lockfile + reference commit pin
7. 对照资产合规
   批准：FULL_ASSETS 仅本地归档；公开报告用 synthetic/CC0；
   公开 registry 使用 asset alias + hash，不泄露私有文件名
8. 规则登记表
   批准：R1_3B_RULE_REGISTRY.md 为人工评审 Source of Truth；
   需要自动化时再生成 JSON sidecar
```
