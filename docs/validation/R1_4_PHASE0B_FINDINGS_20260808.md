# R1.4 Phase 0B — 生产 VMD restore→continuation 等价性缺口（2026-08-08）

> 状态：**OPEN（引擎级，非 Stable ABI 层）**。
> 影响：Stable C ABI 跨进程 FULL E2E 的 N+1 字节相等不能作为门禁断言；
> N（restore 后重建 checkpoint）字节相等已由测试证明并保持强制。

## 发现

Stable C ABI 跨进程 E2E 的 FULL 变体（`production-pmx-yeshiguang` +
`production-vmd-body`，frame 30）：

```text
Process A: replay N → checkpoint N → step N+1 → checkpoint N+1
Process B: deserialize N → restore → checkpoint N → step N+1 → checkpoint N+1

N  wire bytes          == （PASS，restore 精确复现 checkpoint）
N+1 wire bytes         != （MISMATCH）
```

差异从 wire payload 的 fingerprint pose exact hash（offset 192）开始，
同时覆盖 vertex hash 与 physics rigid-body 数据（约 2.1 万字节）。

## 复现（引擎级，与 Stable ABI 无关）

用 C++ API 在 `SabaMmdRuntimeModel` 上复现，两条 VMD 装载路径都失败：

- 构造时传入 VMD（`CreateDeterministicRuntime(model, vmd)`）；
- `Initialize()` 之后 `LoadMotion(vmd)`。

流程：

```text
baseline:  from-start 到 frame 31 → hashes
source:    checkpoint at frame 30
diverged:  CaptureCanonicalAt(60) → ReplayFromCheckpoint(cp, 30)
           → StepMotionFrameExact(31)

结果：diverged 的 pose/vertex/physics exact hash != baseline
```

即：**restore 在 N 处被 hash 验证为精确复现（`RestoreCheckpointValidated`
Phase 5），但随后第一个 step 的确定性 continuation 与 from-start 分叉**。

## 范围

R1.2C equivalence matrix（E1–E11）只在 `pmx-physics` + 最小生成 VMD 上
建立并通过；`production-pmx-yeshiguang` + `production-vmd-body` 未被该
矩阵覆盖。本次 FULL E2E 首次在该资产对上暴露此缺口。

## 候选根因（未定论）

`VMDAnimation::Evaluate(float)` 是绝对帧求值（各 controller 直接在 t 上
求值），本身无明显路径依赖；分叉更可能来自 restore 未重建的动画/morph/IK
内部状态（restore 只做一次 `EvaluateAnimationFrameOnly(N)`，而 from-start
顺序求值 0..N），或 Saba `Begin/EndAnimation` 状态。

## 处理

- 不修改 Stable 层来掩盖此缺口；FULL 跨进程测试改为：N 字节相等为强制
  断言，N+1 作为诊断输出（`DIAGNOSTIC_MISMATCH`）。
- 引擎侧需要一次专项调查：先定位第一个分叉 step 中
  pose/vertex/physics 各自从哪个字段开始偏离，再决定
  restore 是否应顺序重建动画内部状态。
- 在结论前，R1.2C equivalence 声明继续限定为
  `pmx-physics` + 最小 VMD。
