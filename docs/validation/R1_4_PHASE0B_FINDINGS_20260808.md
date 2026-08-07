# R1.4 Phase 0B — 生产 VMD restore→continuation 等价性缺口（2026-08-08）

> 状态：**RESOLVED（2026-08-08，R1.2C integrity fix）**。
> 影响：Stable C ABI 跨进程 FULL E2E 的 N+1 字节相等不能作为门禁断言；
> 已修复：N 与 N+1 均恢复字节相等并升级为强制门禁。

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

## 根因（已定位并修复）

分阶段诊断（stage probe）结论：

```text
physics 前的 node hash        A == B
physics 前的 morph hash       A == B
边界 N 的 active motion state A == B
第一个 physics step 之后      A != B（首个分歧刚体仅 ~1e-6，
                              随后被约束链放大到 ~1.7）
```

即动画/morph/IK/motion-state 全部无罪；分歧产生在 Bullet step 内部。
根因是 **Cold Canonical Boundary 没有归一化碰撞世界的内部顺序状态**：

- from-start 的 broadphase 树（btDbvtBroadphase）、已注册
  manifold/algorithm 集合及其 LIFO 池 free-list 顺序随 30 帧增量演化；
- restore 在恢复时额外做一次世界重建，留下分配历史相关的内部状态
  （pair-cache 哈希表、池 free-list 顺序），且与 from-start 的演化状态
  不同 → 下一帧 pair 迭代与求解顺序不同 → 微小数值分歧随后被约束链放大
  （密集模型上明显，pmx-physics 上恰好一致）。该分歧在 Linux/GCC 上
  可复现，Windows/MSVC 上此前恰好一致。

## 修复（R1.2C integrity fix，四部分）

1. `StepFrameExact` 每次 step 开始（cold boundary）执行
   `ClearContactManifoldsDeterministic()` + `ClearSolverHistoryDeterministic()`
   + `RebuildCollisionWorldDeterministic()`，from-start 与 restore 都从
   canonical collision world 进入求解；
2. Saba 的 broadphase 改用无历史的 `btSimpleBroadphase`
   （pair 枚举按对象索引序，彻底消除树/uid 历史依赖）；
3. 世界重建时重置 manifold 与 collision-algorithm 两个 LIFO 池的
   free-list（新增 `btPoolAllocator::freeAllMemory` +
   `btCollisionDispatcher::resetCollisionPools`），创建顺序变为
   canonical；
4. `RestorePhases` 不再在恢复时额外重建世界（step 起点已重建），
   避免第二次重建引入分配历史差异；FollowBone 的 activation 状态
   改为从 snapshot 逐字恢复，保证 N 处 checkpoint 字节相等。

修复后：

```text
production VMD restore(30) → step(31) == from-start → step(31)
pose / vertex / physics exact hash 全部相等
```

回归测试：`TestR12CProductionVmdContinuationEquivalence` 覆盖两个独立生产
资产对（叶瞬光 + body VMD；凑企鹅 + penguin VMD）；Stable C ABI FULL
跨进程 E2E 升级为 `--require-n1` 强制门禁。

## 范围

R1.2C equivalence matrix（E1–E11）只在 `pmx-physics` + 最小生成 VMD 上
建立并通过；`production-pmx-yeshiguang` + `production-vmd-body` 未被该
矩阵覆盖。本次 FULL E2E 首次在该资产对上暴露此缺口。

## 处理

- 引擎修复：StepFrameExact 每步起点做确定性 collision-world 重建。
- 回归：两个生产资产对的 restore→continuation 等价测试；
  Stable C ABI FULL 跨进程 E2E 强制 N/N+1 字节相等。
- R1.2C equivalence 声明恢复为对任意 Saba MMD 资产的 cold-boundary 语义。

## Final Canonicalization Guard（第二轮，2026-08-08）

审查指出 `btSimpleBroadphase` 的 handle 分配是 LIFO free-list：
每次 rebuild 会翻转 handle/object 映射，而 `calculateOverlappingPairs`
按 handle 索引迭代，pair 顺序因此依赖 rebuild 次数奇偶。上一轮三条测试
路径恰好都落在同 parity，掩盖了该历史泄漏。本轮补：

1. 真正实现 `btSimpleBroadphase::resetPool(dispatcher)`：
   在确认 `m_numHandles == 0` 后把 free-list 重置为
   `0 → 1 → 2 → ...`，`RebuildCollisionWorldDeterministic()` 在 remove-all
   后调用它，使 rebuild 成为幂等 canonical 操作；
2. 恢复 `RestorePhases` Phase 4 的
   `RebuildCollisionWorldDeterministic()`（R1.2B raw RestoreState 的
   canonical-world 语义不因 R1.2C 修复被削掉）；
3. 新回归：`diverged history = 59 / 60 / 61` 三种 parity 下
   restore(30) → step(31) 都必须与 from-start 相等；
4. 新回归：同一状态 rebuild 1 次 vs 2 次后 step 的 exact hash 必须相等
   （直接封死 rebuild-count parity）；
5. `ComputeConfigurationFingerprint` 更新为
   `physics ABI / deterministic world revision = 2`、
   `broadphase = btSimpleBroadphase (2)`，旧 DBVT 身份不再撒谎；
6. `DeterministicCompatibilityRevision / SabaCompatibilityRevision /
   BulletCompatibilityRevision` 1 → 2
   （wire payload revision 保持 1，schema 未变）；
7. FULL Stable cross-process 保持 `--require-n1`；
8. R1.3 focused regression（RAW==COMMUNITY、linked-body A/B、
   Mode2 A/B、trace/export smoke）由既有集成测试覆盖并在四矩阵重跑；
   realtime 性能：四矩阵 FULL 集成耗时与修复前同量级
   （Windows FULL ≈ 88s / Linux FULL ≈ 100s），
   简单 broadphase 未使 realtime 路径显著恶化。
