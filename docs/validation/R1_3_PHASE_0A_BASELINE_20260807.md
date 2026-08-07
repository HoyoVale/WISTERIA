# R1.3 Phase 0A 实现基线（2026-08-07）

> R1.3 契约（Phase 0A Contract Frozen，2026-08-07）后的实现验收基线。
> 契约：`docs/architecture/R1_3_MMD_COMPAT_CONTRACT.md`（v2 + 冻结闭合）。
> 本文件记录 Phase 0A 第一批实现（契约 §12 第 4–9 步）与验证结果。

## 1. 代码基线

- 分支 `wisteria2`，提交由用户管理；本文件记录未提交改动。
- 新增中立配置层：`include/wisteria/mmd/physics/mmd_physics_configuration.hpp`
  + `src/mmd/physics/mmd_physics_configuration.cpp`
- 新增单位审计：`include/wisteria/mmd/physics/mmd_physics_audit.hpp`
  + `src/mmd/physics/mmd_physics_audit.cpp`
- 新增 Trace 数据结构：`include/wisteria/mmd/physics/mmd_physics_trace.hpp`
- 新增 Trace 工具层（Runtime 不做 I/O）：
  `tools/trace/trace_jsonl.hpp/.cpp`、`tools/trace/trace_diff_main.cpp`
- Runtime 接入：`MmdRuntimeModel` 增加
  `SetMmdPhysicsConfiguration / GetMmdPhysicsConfiguration /
  CapturePhysicsTraceFrame`；`SabaMmdRuntimeModel` 实现并持有唯一权威配置，
  配置指纹升级 v2（只 Hash 有效行为，不 Hash Preset 标签）。
- Vendored Saba 窄接口：`MMDPhysics::SetLinkedBodyCollisionMode /
  ApplyLinkedBodyCollisionMode`，`MMDRigidBody::SetMode2PreserveTranslation`。

## 2. 实现事实

### 配置层（§4）

- `BuildPresetConfiguration` 是普通运行唯一入口；Phase 0A 三个 Preset 全部
  展开为 SabaBaseline（fixed step 1/120、max substeps 10、gravity -98、
  PmxMaskOnly、PreserveAnimatedTranslation、adaptive 全关），默认档
  `MMD_RAW`。
- `ValidateConfiguration` 拒绝匿名身份、非法数值、Reserved 模式，以及
  直接 Preset 中的诊断模式（FullTransformDiagnostic）。
- `DeriveDiagnosticConfiguration` 只能从 Preset 派生并携带
  `originPreset` 身份；Reserved 枚举（ForceEnableLinkedPairsDiagnostic、
  StrictBoneLength）一律拒绝。
- `ComputeEffectiveConfigurationFingerprint`（v2）：Hash backend/baseline
  behaviour、gravity、fixed step、substeps、linked collision、Mode 2、
  adaptive 标志；**不 Hash Preset 显示名/originPreset/profileRevision**。

### Runtime 接入（§5）

- `SetMmdPhysicsConfiguration` 在 Initialize 前应用；Initialize 后
  行为切换返回 `UnsupportedReplayProfile`（标签级切换仍允许）。
- 低层 `SetMmdPhysicsSettings` 的修改同步回权威配置。
- 指纹 v1 中硬编码的 linked-body/compatibility 标记已替换为配置贡献。

### Trace（§6）

- `CapturePhysicsTraceFrame` 只读捕获当前 canonical 边界：body
  world/interpolation/motion-state 三个变换、速度、骨骼局部/全局矩阵、
  joint raw/violation 误差（6DOF 约束公式）、contactPairs（ground 使用
  `UINT32_MAX` 哨兵）、三个 state hash、身份与时间线字段。
- JSONL writer/parser 与差分 CLI 在 `tools/trace`；差分工具能定位人为注入
  +0.001 的第一分叉（frame/body/positionError）。
- 修复：旋转误差先归一化列向量，避免非正交基自比产生假分叉。

### 单位审计（§7）

- 空集合合法（available=false/count=0）；负关节下限、零角度限制、零长度
  辅助骨均合法；modelHeight <= epsilon 与 medianBodySize <= epsilon 时
  相关比率 available=false，禁止除零。

### A/B 开关（§8）

- `DisableConstraintLinkedPairs`：PMX mask 永远是基础过滤，附加
  `!isConstraintLinked(A, B)`；通过约束 remove + add(disable=true) 应用，
  ground 不受影响。
- `FullTransformDiagnostic`：`DynamicAndBoneMergeMotionState` 增加
  preserve-translation 开关；关闭时完整回写物理平移+旋转。

## 3. 实现发现

- **CORE 夹具 pmx_physics 的 root 骨标记为“物理后变形”**：物理写回后
  `UpdateNodeAnimation(true)` 会用动画重新覆盖 local，因此 Mode 2 写回
  无法通过引擎 Pose 在该夹具上观察（基线旋转同样不可见）。写回生效断言
  使用 FULL_ASSETS 生产模型验证。
- Trace 的 bones 记录的是引擎发布的 Pose（与 R1.2C Pose hash 一致），
  不是 Saba 节点在物理写回瞬间的原始 global。

## 4. 测试结果

### 四套矩阵（Phase 0A Complete，2026-08-07）

```text
Windows CORE          CTest 5/5，unit 27/27，
                      integration 全 PASS（含全部 R1.3 用例）
Windows FULL_ASSETS   CTest 5/5，integration 含
                      R1.2C IK restore 回归 + R1.3 Mode 2 writeback pose
Linux CORE            CTest 5/5（WSL g++ 11.4 + llvmpipe）
Linux FULL_ASSETS     CTest 5/5，integration 101.75s
```

### Linux 说明

```text
WSLg 默认的 Mesa D3D12 渲染器与 LLVM 冲突
（"Option 'spirv-expand-step' registered more than once"）会使
integration 在 GL 初始化处 abort。门禁以
LIBGL_ALWAYS_SOFTWARE=1（llvmpipe）运行，与 R1.2 基线一致。
```

## 5. Final Validation Fix（评审轮 3，2026-08-07）

评审结论：主体工程成功，Final Validation Fix Required。六项窄修复已全部
落地并复跑四套矩阵：

```text
1. ValidateConfiguration 拒绝 Phase 0A 未实现配置：
   gravityScale != 1.0 与任意 adaptive=true 一律 Invalid
2. Saba Runtime 校验 backend/baseline 身份：
   非 "saba-mmd" / "saba-baseline-v1" 一律 InvalidState
3. Initialize 后相同 effective config 的标签切换只更新 metadata，
   不再触碰 Bullet（不重选 motion state、不重新激活、不 remove/re-add
   约束）；新增逐位状态不变断言
4. CapturePhysicsTraceFrame 只在 canonical boundary 返回 true，
   失败不改 output（candidate 模式）
5. 审计不再静默过滤 NaN/Inf：MmdPhysicsAuditRange/Result 增加
   finite 与 nonFiniteCount，坏样本计数并保留合法样本统计
6. Linked-body 行为证据：
   - 新增 Bullet 层单元测试（两体重叠 + constraint：disable=false 有接触、
     disable=true 无接触、ground 接触不受影响）
   - CORE 集成测试断言 ground 接触数量与接触拓扑在两种模式下不变；
     pmx_physics 的 6 个相连对从不重叠，故无相连接触（已记录说明）
7. 删除 MmdPhysicsDiagnosticOverrides 中的死字段 trace
   （MmdPhysicsTraceOptions 保留为 Phase 0B 工具层预留）
```

修复后四套矩阵复跑：

```text
Windows CORE          CTest 5/5
Windows FULL_ASSETS   CTest 5/5
Linux CORE            CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
Linux FULL_ASSETS     CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
```

## 6. 未完成 / 后续

- Linux 门禁须以 `LIBGL_ALWAYS_SOFTWARE=1` 运行（WSLg D3D12/LLVM 冲突）。
- Trace joint 误差目前覆盖 6DOF 族约束（Saba 全部关节均为此类）。
- `ForceEnableLinkedPairsDiagnostic`、`StrictBoneLength` 按契约保持
  Reserved，未实现。
- 跨 Profile Checkpoint 实验入口按契约 Phase 0A 不实现。
- 尚无 CORE 夹具包含“关节相连且重叠”的刚体对；真正的 PMX 级
  linked-collision 行为夹具留给 Phase 0B 社区对照时补充。

**Phase 0A 完成（2026-08-07）**：Windows/Linux × CORE/FULL_ASSETS
四套矩阵全部 CTest 5/5；Final Validation Fix 六项全部闭合。
下一阶段：Phase 0B 社区实现对照（单独契约）。

## 7. Final Validation 2（评审轮 4，2026-08-07）

源码级复查发现状态机与身份模型仍可被绕穿，三项 P0 + 两项 P1 已修复：

```text
P0-1 canonical 状态机统一失效
  - 新增 SabaMmdRuntimeModel::InvalidateDeterministicBoundary()
    （lastBoundaryCanonical / deterministicPrepared / expectedNextFrame）
  - Update()（实时路径）与全部非确定性 mutator 调用它：
    SetPhysicsSettings / SetMmdPhysicsSettings、LoadMotion / ClearMotion /
    SetMotionLooping / Pause / Resume / RestartMotion / SetMotionFrame、
    SetMorphWeight / SetMorphOverride / ClearMorphOverride /
    ClearAllMorphOverrides、SetMmdIkEnabled、ResetMmdPhysics
  - 唯一例外：effective-equivalent metadata-only 标签切换不失效
  - 测试：PrepareFrameZero → Update → Trace / Checkpoint / Step 全部拒绝；
    每个 mutator 逐一验证 canonical 失效

P0-2 直接 Preset 身份不可伪造
  - ValidateConfiguration：originPreset 为空 → 行为必须逐字段等于
    BuildPresetConfiguration(preset)（指纹相等 + profileRevision==1）
  - originPreset 非空 → 必须等于 preset 小写名，且 runtime/adaptive/
    gravityScale 保持 preset 值（只允许 linkedBody / mode2 A/B 偏差）
  - 低层构造函数与 SetPhysicsSettings 的越界设置自动转为
    custom-from-<preset> 身份，不再冒充 MMD_RAW
  - 测试：篡改 gravity / linkedBody 的 MMD_RAW、profileRevision=999、
    伪造 custom 运行时均被拒绝；legacy 构造自定义设置报 custom 身份

P0-3 Trace / Checkpoint 不可被实时历史污染
  - 与 P0-1 同源：canonical 门禁现在覆盖 Update 与全部 mutator

P1-1 审计 finite 全量传播
  - modelBounds / gravity / fixedTimeStep / collisionMargin 的非有限值
    都会使 MmdPhysicsAuditResult.finite=false，不再只表现为 available=false
  - 测试覆盖四条 NaN 注入路径

P1-2 joint violation 改为 Euclidean norm
  - linearViolation = ||per-axis excess||，angularViolationDeg 同理，
    与契约 §6.3 一致（原来是 L1 求和）
  - Trace schema 测试增加 violation <= raw error norm 一致性断言
```

契约文档同步（Frozen contract 与实现一致）：

```text
- MmdPhysicsDiagnosticOverrides 移除 trace 字段
  （MmdPhysicsTraceOptions 保留为 Phase 0B 工具层）
- §4 原则补充 metadata-only 标签切换语义与统一失效规则
```

修复后四套矩阵复跑（2026-08-07）：

```text
Windows CORE          CTest 5/5
Windows FULL_ASSETS   CTest 5/5
Linux CORE            CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
Linux FULL_ASSETS     CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
```

## 8. Closure Fix（评审轮 5，2026-08-07）

最后一个 P0：权威 Configuration 自洽性。低层构造与
`SetMmdPhysicsSettings` 产生的 `custom-from-*` 配置此前会被自己的
`ValidateConfiguration()` 拒绝（custom 身份 + 合法 runtime override 不
被认可）。

修复（方案 A，最小改动）：

```text
1. custom-from-* 配置允许承载合法 runtime override
   （gravity / fixedTimeStep / maxSubSteps / enabled）与已实现 A/B 开关；
2. 直接 Preset 仍必须严格等于 BuildPresetConfiguration(preset)；
3. 仍禁止 gravityScale != 1、adaptive=true、Reserved 模式、
   非 finite / 非法数值；
4. 新增权威不变量测试：
   - legacy 构造函数自定义设置 → Get → ValidateConfiguration == true
   - SetMmdPhysicsSettings 自定义设置（post-init）→ Get → Validate == true
   - pre-init roundtrip：Get(A) → Set(A) → Ok
5. 契约同步 custom 配置语义；
6. canonical 失效矩阵补充代表性 mutator
   （ResumeMotion / SetMotionLooping / RestartMotion / ClearMotion）。
```

修复后四套矩阵复跑（2026-08-07）：

```text
Windows CORE          CTest 5/5
Windows FULL_ASSETS   CTest 5/5
Linux CORE            CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
Linux FULL_ASSETS     CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
```

**R1.3 Phase 0A — Frozen + Implemented + Validated（2026-08-07）**。
停止 Phase 0A 审查，进入 Phase 0B 社区实现对照（单独契约）。

## 9. Guard Fix（评审轮 6，2026-08-07）

Closure Fix 不变量（Get == true ⇒ Validate == true）的最后一道旁路：
低层 settings 入口此前无输入校验，`fixedTimeStep=0` 或 NaN gravity 会直接
写入权威配置并送进 Bullet。

修复：

```text
1. SetPhysicsSettings 先构造 candidate（同步 runtime、必要时转
   custom-from-*），ValidateConfiguration 失败则整体 no-op，
   不改配置、不改 Bullet；
2. Initialize 在创建模型/Bullet 之前校验当前权威配置，非法返回 false；
3. GetMmdPhysicsConfiguration 仅在配置合法时返回 true，
   失败不改 output；
4. originPreset 头注释更新（legacy 构造与低层 setter 也会产生 custom）；
5. 负向测试：
   - SetMmdPhysicsSettings(fixedTimeStep=0) → 配置不变且仍合法
   - SetMmdPhysicsSettings(gravity=NaN) → 配置不变且仍合法
   - constructor(非法 settings) → Initialize == false、Get == false
```

修复后四套矩阵复跑（2026-08-07）：

```text
Windows CORE          CTest 5/5
Windows FULL_ASSETS   CTest 5/5
Linux CORE            CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
Linux FULL_ASSETS     CTest 5/5（LIBGL_ALWAYS_SOFTWARE=1）
```

**R1.3 Phase 0A — Frozen + Implemented + Validated（2026-08-07，
Guard Fix 闭合）**。停止 Phase 0A 审查，正式进入 Phase 0B。
