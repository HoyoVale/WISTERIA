# P0 实验：Bullet 2.75 约束兼容层（MMD_COMPAT 首个里程碑）

> 状态：已归档（2026-08-04）。该实验对应的 `mmd/physics_compat/*` 与旧
> `MmdPhysicsInstance` 已在整体切换到 Saba 后删除；本文保留实验结论，
> 供将来评估 Bullet 版本补偿时参考。

## 背景与证据

WISTERIA 的“长时运行后裙摆/飘带约束违规逐渐增大”问题，与 Bullet 版本行为差异高度相关。

社区事实：

- MMD 本体使用 Bullet 2.75。
- babylon-mmd 文档明确说明：Bullet 3.25 的约束行为已变化，部分模型约束会失效；提供
  `disableOffsetForConstraintFrame` 选项恢复 2.75 行为。
- babylon-mmd Ammo.js 插件源码注释：2.75 没有 `m_useOffsetForConstraintFrame` 字段，
  2.76 引入并默认 true；把该字段置 false 可恢复 2.75 行为。
- babylon-mmd 仓库的 `constraint-fix.patch` 把 `btGeneric6DofConstraint.cpp`
  `get_limit_motor_info2` 中：

  ```cpp
  btVector3 c = m_calculatedTransformB.getOrigin() - transA.getOrigin();
  ```

  改回 2.75 行为：

  ```cpp
  btVector3 c = m_calculatedTransformA.getOrigin() - transA.getOrigin();
  ```

- three.js MMDPhysics、Saba、babylon-mmd、MikuMikuPhysics 全部使用老类
  `btGeneric6DofSpringConstraint`，而不是 WISTERIA 当前使用的
  `btGeneric6DofSpring2Constraint`。
- 社区普遍：动态刚体禁止休眠、相连刚体碰撞不禁用（`addConstraint(..., false)`）、
  关节全轴 `setParam(BT_CONSTRAINT_STOP_ERP, 0.475)`。

## P0 假设

把 MMD 弹簧关节切到老类 `btGeneric6DofSpringConstraint`，并按 babylon-mmd 的
2.75 兼容参数（`setUseFrameOffset(false)` + StopERP 0.475）创建，同时给 vendor
Bullet 3.25 打上一行补丁，可以显著降低长时运行中的约束违规累积。

## 代码接入点

1. vendor Bullet 补丁（一行）：
   `third-party/bullet3/src/BulletDynamics/ConstraintSolver/btGeneric6DofConstraint.cpp`
   第 746 行，仅影响老约束类及其子类。
2. `PhysicsWorld::CreateSpring6DofConstraint`：新增 legacy 分支，默认行为不变。
3. `PhysicsSpring6DofDesc` / `PhysicsSixDofDesc`：新增 2.75 兼容字段。
4. `MmdPhysicsRuntimePolicy`：新增 `MmdPhysicsBullet275Compatibility` 与
   `MmdCompatDefaults()` 工厂。
5. `MmdPhysicsInstance`：在 MMD_COMPAT 下传递 legacy 约束参数、动态刚体禁休眠、
   关闭 linked-body 碰撞屏蔽。
6. 回归测试：新增 `TestP0Bullet275CompatibilityLongRunWhenAvailable`，用叶瞬光 +
   VMD 跑 720 帧 A/B。

## 实验设计

固定条件：

- 模型：叶瞬光（`models-copy/mmd/叶瞬光_pmx/叶瞬光.pmx`）
- 动作：皮卡皮卡皮卡丘+ / 身体动作.vmd
- 固定步：1/60（与当前 Scene 默认一致，P0 不引入 1/65/1/120）
- 重力：保持当前值（单位/重力审计留到 P1）
- 帧数：720 帧（12 秒）

七个对照臂（前四个是主臂，后三个用于隔离因素）：

| 臂 | 约束后端 | 动态休眠 | linked collision | adaptive |
|---|---|---|---|---|
| adaptive-default | Spring2（现状） | 允许 | 禁用 | 开启 |
| compat-bullet275 | 老类 + 2.75 参数 | 禁止 | 禁用（保持现状） | 开启 |
| raw-adaptive-default | Spring2（现状） | 允许 | 禁用 | 关闭 |
| raw-compat-bullet275 | 老类 + 2.75 参数 | 禁止 | 不禁用 | 关闭 |
| iso-legacy-constraint | 老类 + 2.75 参数 | 允许 | 禁用 | 开启 |
| iso-deactivation | Spring2（现状） | 禁止 | 禁用 | 开启 |
| iso-linked-collision | Spring2（现状） | 允许 | 不禁用 | 开启 |

指标：

- `maxLinearViolation`
- `maxAngularViolationDeg`
- `severeJoints`（超过稳定失败阈值的关节数）
- `recoveries`（恢复次数，adaptive 臂）
- `finite` / `stabilized`（无 NaN、无 SAFE FREEZE）
- Mode 2 最大平移差（辅助观察）

## 验收标准

- 现有 `wisteria_tests.exe` 全部通过（含叶瞬光 495 刚体 / 568 关节初始化回归）。
- 四个 A/B 臂均无 NaN、无 SAFE FREEZE。
- 输出可复现的对照数值，并记录到测试日志。
- P0 不要求 legacy 一定更优；目标是得到可复现的差异数据，支撑后续取舍。

## P0 结果（2026-08-03）

模型：叶瞬光 + 身体动作.vmd，720 帧 @ 1/60，重力保持 -9.8。

| 臂 | maxLinearViol | maxAngularViolDeg | severe | finite/stabilized |
|---|---|---|---|---|
| adaptive-default | 0.916 | 33.6 | 4 | true / true |
| compat-bullet275 | 0.723 | 47.2 | 2 | true / true |
| raw-adaptive-default | 1.442 | 45.2 | 24 | true / true |
| raw-compat-bullet275 | 1.475 | 26.0 | 13 | true / true |
| iso-legacy-constraint | 0.723 | 47.2 | 2 | true / true |
| iso-deactivation | 0.916 | 33.6 | 4 | true / true |
| iso-linked-collision | 3.044 | 120.5 | 84 | true / true |

结论：

1. legacy 约束 + 2.75 参数单独生效时：adaptive 下最大线性违规下降（0.916 → 0.723）、
   严重关节数下降（4 → 2），但角向违规上升（33.6° → 47.2°）；raw 下角向违规反而
   明显下降（45.2° → 26.0°）。整体是“混合但可继续优化”的结果。
2. 动态刚体禁休眠在本场景没有可测差异（iso-deactivation 与默认逐位一致），说明
   当前动画驱动下刚体基本不进入休眠；该改动保留但优先级降低。
3. 开启 linked-body 碰撞是明显的负向因素（severe 4 → 84，角向 33.6° → 120.5°）。
   WISTERIA 现有的近邻/裙摆语义过滤已经替代了部分“禁碰撞”行为，因此
   `MmdCompatDefaults()` 保留 `disableLinkedBodyCollisions = true`，碰撞行为留到
   P1 单独审计。
4. 下一步应把 legacy 约束与 MMD 条件（1/65 或 1/120 固定步、-98 重力）组合再做
   A/B，babylon 的 2.75 兼容本来就是在这些条件下设计的。

## 后续（P1，不在本实验范围）

- 单位/重力审计（-9.8 vs -98）
- fixed step 1/65 与 1/120 对照
- Mode 2 / reset / warmup 社区语义对照
- solver / split impulse 参数审计
