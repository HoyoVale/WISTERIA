# R1.3B Phase 0B Step 9 — 首批轨迹采集与对照结果（2026-08-07）

> 契约：`docs/architecture/R1_3B_MMD_COMMUNITY_COMPARISON_CONTRACT.md`
> §12 Step 9。本文件记录冻结 corpus 的首批 WISTERIA ↔ babylon-mmd
> 逐刚体对照结果；executionProfile 不同（reference-continuous vs
> deterministic-cold-step），**全部只作观察证据，不声称因果**。

## 1. 方法

```text
WISTERIA 侧：wisteria_trace_export（MMD_RAW preset，
  PrepareFrameZero + StepMotionFrameExact，canonical JSONL，sample 0/10/.../300）
reference 侧：bundle_trace.cjs（共享 frame driver，
  NormalizedComparison，gravity -98，1/120，synthetic-ground-box-v1，
  bodies.csv 按 sourceRigidBodyIndex 导出）
对照：compare_traces.mjs（按 motionFrame + sourceRigidBodyIndex 对齐，
  位置欧氏距离 + 旋转角；参考侧已与 WISTERIA canonical 坐标一致）
```

## 2. 对照结果（300 motionFrames = tick 1200 = 10s）

| 资产 | First divergence | Max divergence | missing |
| ---- | ---------------- | -------------- | ------- |
| corpus-asset-01 + motion-01 | frame 0 / body 0 / pos 0.310 / rot 1.32° | frame 10 / body 7 / pos 2.870 / rot 29.90° | 0 |
| corpus-asset-02 + motion-02 | frame 0 / body 0 / pos 0.335 / rot 6.19° | frame 0 / body 425 / pos 7.575 / rot 47.24° | 0 |
| corpus-asset-03 + motion-03 | frame 0 / body 0 / pos 1.063 / rot 33.27° | frame 190 / body 85 / pos 28.374 / rot 165.59° | 0 |
| corpus-asset-02 no-VMD | frame 0 / body 1 / pos 3.0e-8 / rot 0.024° | frame 300 / body 425 / pos 8.050 / rot 57.97° | 0 |

## 3. corpus-asset-02 no-VMD 专项复验（原 Historical Preliminary）

```text
统一 Clock + 坐标修正后：
  frame 0：两侧位置差 ≤ 3e-8、旋转差 ≤ 0.024°
  （validated-basis 度量，远小于物理分歧阈值；初始条件已对齐）
  frame 300：最大位置差 8.05（body 425）、旋转差 57.97°
  分歧随时间单调增大

结论：原“mode-2 刚体水平收拢”分歧在统一时间轴下复验成立——
  不是时间单位换算假象；旧数值（±10.5→±6.8 @ 2.5s）被新结果取代
  （10s 时最大逐刚体差 8.05）。首分叉时间与刚体需逐 tick 追踪。
```

## 4. 采集过程中发现并修复的两个 reference 适配器 bug

```text
1. 坐标反射过度：pinned babylon-mmd 的刚体世界变换与 WISTERIA
   canonical 坐标直接一致；reference 适配器原施加 Z 反射导致
   frame 0 全部刚体位置差 = 2×|z|、旋转 180° 假分叉。
   已改为不反射（bounds 同理）。ReferenceCoordinateNormalization
   公式保留供确需反射的适配器使用。
2. ammo.js 临时对象复用：btMatrix3x3.getRow() 每次返回同一个临时
   btVector3，延迟读取导致三行全变成最后一行（退化基 + 假旋转差）。
   已改为每次调用后立即拷贝三个分量。
```

## 5. 待调查项（Step 9 后续 A/B）

```text
1. VMD 运行的 frame 0 残差（asset-01 pos 0.31、asset-02 pos 0.34 /
   rot 6.19°、asset-03 pos 1.06 / rot 33.27°）：
   动画 frame 0 采样 / morph / IK 初始化是否等价；
2. asset-03 max divergence frame 190 body 85（pos 28.37）：
   是否裙摆/长发 mode-2 链；需要逐刚体轨迹追踪；
3. 下一步 A/B：linked-body 与 Mode 2 开关在 corpus 上的
   WISTERIA 侧 on/off 轨迹（同 executionProfile 才构成因果证据）。
```

## 6. 产物

```text
tools/trace/trace_export_main.cpp     WISTERIA canonical trace 导出 CLI
tools/reference_trace/compare_traces.mjs  跨实现逐刚体对照脚本
tools/reference_trace/trace.mjs       两个适配器 bug 已修复
```

## 8. Evidence Integrity Closure（2026-08-07）

```text
1. compare_traces.mjs 升级为证据门禁：
   - --corpus corpus.json --asset <id>：校验 reference env SHA-256、
     WISTERIA 首帧 FNV-1a64、motion identity、comparisonPoints 齐全；
   - 对称帧集：任一侧缺帧 → EVIDENCE INVALID（exit 1）；
   - 旋转基合法性：finite / 单位列 / 正交 / det=+1，
     非法 → INVALID_ROTATION_BASIS（绝不当作 0°）。
2. C++ exporter 严格校验 --frames（>=0 整数）与
   --sample-interval（>=1 整数），整串消费，非法 exit 2。
3. diff 工具新增 joint-presence locator（与 numeric joint delta 分离）。
4. 资产合规：registry / corpus.json / 本报告只保留 alias + hash，
   真实名称与路径 PRIVATE；历史中的旧名称属单独清理决策。
5. 复验：closure 后重跑 corpus-asset-02 no-VMD 代表性对照，
   first divergence position 3e-8 与 max divergence
   8.05 / 57.97° 不变；rotation 度量因 comparator 改为
   validated-basis 直接点积，报 0.024°（同方向，结论不变）。
```

## 7. A/B 开关证据（WISTERIA 侧，同 executionProfile）

方法：`wisteria_trace_export --linked-body / --mode2` 生成开关两侧
canonical JSONL，`wisteria_trace_diff` 对照。两侧均为
`deterministic-cold-step-v1`（同 executionProfile），因此构成
候选规则的**因果证据**（Rule Admission 六项中的轨迹 A/B）。

### 7.1 Mode 2：corpus-asset-02 no-VMD（300 frames，sample 10）

```text
A = PreserveAnimatedTranslation（基线）
B = FullTransformDiagnostic

First bone divergence：frame 10 / bone 17 / maxMatrixDelta 0.092
无 body COM 分叉、无接触拓扑/关节变化

解释：开关只改变骨骼写回（诊断语义），不改变 Bullet 刚体；
frame 10 起骨骼姿态分叉 —— 开关产生预期因果效果。
```

### 7.2 Linked-body：corpus-asset-01 + motion-01（300 frames，sample 10）

```text
A = PmxMaskOnly（基线）
B = DisableConstraintLinkedPairs

First divergence：frame 10 / body 8 / pos 0.016 / rot 1.29°
First contact-topology divergence：frame 10 / pair (0,1)
First motion-state divergence：frame 10 / body 8
First bone divergence：frame 10 / bone 56
Max divergence：frame 20 / body 8 / pos 0.061 / rot 5.87°
```

### 7.3 Linked-body：corpus-asset-03 + motion-03（300 frames，sample 10）

```text
First divergence：frame 10 / body 6 / pos 0.115 / rot 15.30°
First contact-topology divergence：frame 10 / pair (0,248)
Joint error delta：joint 696 / linear 1.85 / angular 127.91°
Max divergence：frame 300 / body 556 / pos 1.876 / rot 105.21°
```

### 7.4 逐帧首分叉（corpus-asset-01 sample 1）

```text
First contact-topology divergence：motionFrame 1 / pair (0,1)
First body divergence：motionFrame 5 / body 8 / pos 0.043
Max body divergence：motionFrame 18 / body 8 / pos 0.233
```

结论：

```text
1. DisableConstraintLinkedPairs 在真实 corpus 资产上产生可观测、
   可复现的接触拓扑变化（asset-01 pair (0,1) 自 motionFrame 1 起）——
   linked-body A/B 证据成立；
2. FullTransformDiagnostic 只改变骨骼写回、不改变 Bullet 状态——
   诊断语义与实现一致；
3. 候选规则进入 COMMUNITY/ADAPTIVE 的裁决还需要：
   - 逐刚体追踪分叉源（body 8 / pair (0,1) / joint 696 的归属）；
   - 与 nanoem（Source Semantics）对照确认社区语义。
```
