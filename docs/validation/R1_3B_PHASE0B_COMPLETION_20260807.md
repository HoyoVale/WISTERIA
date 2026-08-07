# R1.3B Phase 0B — 完成报告（2026-08-07）

> 契约：`docs/architecture/R1_3B_MMD_COMMUNITY_COMPARISON_CONTRACT.md`。
> 状态：**Phase 0B 首轮完成（第一轮社区对照）**。
> 规则登记表：`docs/validation/R1_3B_RULE_REGISTRY.md`（已冻结裁决）。

## 1. 核心结论

```text
MMD_COMMUNITY v1 remains behavior-identical to MMD_RAW v1。

LB-01 Linked-body collision
  PmxMaskOnly → ADMITTED_COMMUNITY（baseline-confirmed）
  DisableConstraintLinkedPairs → REJECTED for COMMUNITY（keep diagnostic）
  Babylon internal collision boolean → REFERENCE_SPECIFIC

M2-01 Mode 2 writeback
  PreserveAnimatedTranslation → ADMITTED_COMMUNITY（baseline-confirmed）
  FullTransformDiagnostic → REJECTED for COMMUNITY（keep diagnostic）
  StrictBoneLength → RESERVED

没有新增 effective COMMUNITY behavior → 不 bump profileRevision，
Step 11（COMMUNITY 首条规则实现）标 N/A。
```

本轮最有价值的结果：经独立社区源码（saba / babylon-mmd / nanoem）与
受控 WISTERIA A/B 实验验证，**SabaBaseline 在 linked-body collision 与
Mode 2 writeback 两个关键语义上本来就站在社区主流行为上**。

## 2. 步骤状态

```text
Step 1  Contract Frozen                   完成
Step 2  license / commit / package pin    完成
Step 3  synthetic clock calibration       完成
Step 4  coordinate normalization golden   完成
Step 5  babylon reference exporter        完成
Step 6  WISTERIA extended diff            完成（含 joint presence）
Step 7  nanoem feasibility                完成（Source Semantics）
Step 8  corpus freeze                     完成（alias 公开 + hash 双身份）
Step 9  首批轨迹采集 + A/B                 完成（含 asset-02 复验）
Step 10 规则裁决与证据包归档                完成（LB-01 / M2-01 APPROVED）
Step 11 COMMUNITY 首条规则实现              N/A（无新增 effective behavior）
Step 12 四套矩阵 + reference sanity        完成（本报告）
Step 13 完成评审 / registry 冻结            完成（本报告）
```

## 3. 证据链摘要

```text
坐标：canonical target = WISTERIA；
  babylon rigid-body mapping = IdentityToWisteriaCanonical（已核验）
时钟：motionFrame N = physicsTick 4N = N/30 s（共享 frame driver）
身份：SHA-256（跨实现）+ FNV-1a64（WISTERIA 内部），corpus gate 校验
对照：sourceRigidBodyIndex 对齐，0 missing；
  asset-02 no-VMD frame0 pos ≤3e-8 → frame300 max 8.05（分歧随时间扩大）
A/B：linked-body 改变 contact topology（asset-01 pair (0,1) 自 frame 1）；
  Mode 2 diagnostic 只改变 bone writeback（asset-02 bone 17）
```

## 4. 四套矩阵（全部 CTest 5/5）

```text
Windows CORE          5/5（unit / runtime / integration / abi / render）
Windows FULL_ASSETS   5/5
Linux CORE            5/5（LIBGL_ALWAYS_SOFTWARE=1）
Linux FULL_ASSETS     5/5（LIBGL_ALWAYS_SOFTWARE=1）
```

## 5. Reference evidence sanity

```text
npm run calibrate   PASS（samples=301 publishes=301 ticks=1200）
npm run coordinate  PASS（13 项 golden）
corpus gate         PASS（asset-02 no-VMD：identity/frames/basis 全通过，
                      first 3e-8 / max 8.05 / 57.97°）
```

## 6. 遗留项（不阻塞首轮收尾，转入后续）

```text
1. 分叉源逐刚体归属：asset-01 pair (0,1) / body 8、
   asset-03 joint 696（Step 9 §7.4）——属于第二轮或 R1.4 期间调查；
2. nanoem Runnable：feasibility 已评估，Source Semantics 满足本轮；
3. NativeCompatibilityAudit：reference harness 明确 Unsupported，
   留待 gravity/ground 专项；
4. VMD frame 0 残差（asset-01/02/03）待动画初始化等价性调查；
5. 资产合规：HEAD 已 alias 化；历史中的真实名称清理属单独决策。
```

## 7. 下一步

```text
R1.3 Phase 0A/B 均已完成。按 R1.3 契约 §0，进入 R1.4：
  Checkpoint 序列化、C ABI、帧转换边界。
Phase 0B 第二轮可在出现新的社区假设时重新开启。
```
