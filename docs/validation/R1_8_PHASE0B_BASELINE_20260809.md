# R1.8 Phase 0B — Generic Deterministic Timeline 实现基线（2026-08-09）

> 状态：**COMPLETED**。
> 契约：`docs/architecture/R1_8_GENERIC_DETERMINISTIC_RUNTIME_CONTRACT.md`
> （FROZEN v1.0，Phase 0A CLOSED）。

## 1. 一句话

`WisteriaGenericRuntimeDriver` 现在实现了
`IDeterministicFrameStepper`：30Hz canonical 坐标式求值，
`PrepareFrameZero` + 顺序 `StepMotionFrameExact`，每步产生恰好一个
canonical interval root-motion delta（由编排层消费），并对
Generic Deterministic Mode v1 subset 之外的状态显式拒绝。

## 2. 代码改动

```text
include/wisteria/animation/animator.hpp + src/animation/animator.cpp
  IsDeterministicSubsetCompatible()：
    single clip / 无 transition / playing / 不暂停 / speed==1 /
    无 float/bool 参数 / 无 in-flight trigger / 无 MMD IK override /
    无 state machine
  EvaluateCanonicalFrame(prev, cur, loop)：
    绝对时间求值（loop wrap / non-loop clamp）；
    root delta 来自 canonical interval [prev, cur]，与
    "上一次实际 Animator time" 无关；delta 存入 pendingRootMotion

include/wisteria/runtime/determinism.hpp
  TimelineStatus::UnsupportedDeterministicState

include/wisteria/runtime/frame_snapshot.hpp
  DeterministicBackendCapabilities（authoritative）
  ModelRuntimeCapabilities.deterministic 新增；
  checkpoint 域降级为迁移镜像（禁止双真相源）

include/wisteria/runtime/wisteria_generic_runtime_driver.hpp
  + src/runtime/wisteria_generic_runtime_driver.cpp
  继承 IDeterministicFrameStepper
  PrepareFrameZero：config 校验（30/120/0）→ Reset → subset gate →
    t=0 canonical 求值 → pending root = identity → prepared
  StepMotionFrameExact：状态机（InvalidState / DeterminismViolation /
    NonSequentialFrame / 帧域上限 2^20 / subset gate）→
    interval [(N-1)/30, N/30] 求值 → pending delta
  Capabilities：deterministic.supportsExactFrameStepping 按是否有
    timeline 上报；checkpoint 保持关闭（0C 开）

src/runtime/saba_mmd_runtime_model.cpp
  Capabilities()：deterministic 全开 + checkpoint 镜像

src/native/wisteria_stable_runtime.cpp
  capability 映射改读 deterministic（authoritative）

tests/runtime_tests.cpp
  +5 个 R1.8 用例（见验证）
```

## 3. 验证结果（2026-08-09）

### 3.1 Windows（MSVC RelWithDebInfo）

```text
runtime：5/5 R1.8 用例 PASS；全套 PASS
unit：全 PASS（Animator 回归）
integration：全 PASS（Saba capability 镜像回归）
```

### 3.2 WSL Ubuntu 22.04（GCC）

```text
runtime：5/5 R1.8 用例 PASS
unit：全 PASS
```

## 4. 语义验收点

```text
1. 两实例 PrepareFrameZero + 顺序步进 1..60 → 每帧 pose 一致
2. 非顺序帧 / 未 prepare / config 漂移 → 明确状态码拒绝
3. loop：frame 30（整 clip 周期）wrap 到 t=0；frame 31 继续
4. non-loop：越过 clip 末尾后 clamp，time/pose 不再变化
5. root motion：frame 0 identity；frame N delta 来自
   [(N-1)/30, N/30]；消费至多一次
6. subset gate：Pause / speed!=1 / float 参数 / trigger /
   state machine / crossfade → UnsupportedDeterministicState
7. capability：timeline 资产上报 exact stepping；checkpoint 关闭；
   checkpoint 镜像与 deterministic 一致
8. 帧域上限 2^20（float 精度边界），超出拒绝
```

## 5. Phase 0B 边界确认

```text
未做：Generic snapshot/restore、checkpoint payload kind 2（0C）、
      OfflineFrameSequence 通用化（0D）、四矩阵（0E）
```

## 6. 下一步

Phase 0C：Generic snapshot/restore + checkpoint payload kind 2
（animator 状态 + morph overrides + root motion 配置/pending delta）。

