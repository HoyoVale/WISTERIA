# MMD 适配层重写计划

## 背景

当前 MMD 物理运行时集中在 `src/mmd/physics/mmd_physics_instance.cpp`
（约 5000 行 / 190KB），包含大量自研 adaptive 逻辑（recovery、chain 语义、
CCD、近邻/裙摆碰撞过滤、诊断）。这些逻辑是“自己探索”的产物，缺少社区对照，
且 P0 实验证明它们会掩盖/混淆真正的 Bullet 兼容问题。

资源导入层（PMX/VMD/Assimp MMD importer、`MmdPhysicsAsset`、`VmdImporter`）
与底层 `PhysicsWorld`（格式无关的 Bullet 封装）质量稳定，予以保留。

## 决策记录（2026-08-03）

1. **保留 Bullet，不换其它物理引擎。**
   - MMD 的 6DOF 弹簧关节语义是 Bullet 特有的；Jolt/PhysX 等没有直接对应物。
   - 社区可对照实现全部基于 Bullet（three.js = Ammo.js 2.82、Saba = 系统 Bullet、
     MikuMikuPhysics = Bullet 2.82 r2704、babylon-mmd = 自编 3.25 + 2.75 补丁）。
   - babylon-mmd 实测 Havok 数值稳定性不适合 MMD。
   - 后续可把 Bullet 2.82/2.75 作为第二个后端做 A/B，但接口仍是 `PhysicsWorld`。
2. **以 Saba 为骨架重写 MMD 物理/动画适配层。**
   - Saba 是 C++/OpenGL/Bullet，语言与 WISTERIA 一致，核心库可直接链接。
   - `MMDPhysics.cpp` 约 900 行，结构清晰，是社区验证过的实现。
   - Saba 为 MIT 许可，已 vendor 到 `third-party/saba`。
3. **重写期间新旧实现并存，分阶段切换，绝不一次性替换。**

## 保留 / 删除 / 新建边界

### 保留（冻结接口）

| 模块 | 原因 |
|---|---|
| `src/assets/importer.cpp`、`manager.cpp` | 资源导入层，用户认可 |
| `src/mmd/vmd_importer.cpp` | VMD 导入，属于资源导入层 |
| `include/wisteria/mmd/physics/mmd_physics_asset.hpp` | 纯数据描述，运行时无关 |
| `src/physics/physics_world.*`、`physics_types.*` | 格式无关 Bullet 封装，有测试 |
| `src/animation/`（Pose、Animator、PoseBuffer 等） | 通用动画底座 |
| `Scene` / `Entity` 生命周期 | 只依赖 `PhysicsInstance` 抽象，不感知 MMD |

### 删除/重写

| 模块 | 现状 | 处置 |
|---|---|---|
| `src/mmd/physics/mmd_physics_instance.cpp` | 5000 行 adaptive 怪物 | 重写为小型 compat 核心 |
| `mmd_physics_policy.*` | 大量调参 | 收敛为少量兼容开关 |
| `mmd_physics_diagnostics.*` | 与 adaptive 耦合 | 保留最小诊断，删除启发式报告 |
| `src/mmd/mmd_pose_solver.cpp` | IK/append 与物理编排混合 | 按 Saba 流程重写编排 |

### 新建

```text
src/mmd/physics_compat/
├─ mmd_physics_world.cpp       世界、步进、重力（薄封装）
├─ mmd_rigid_body.cpp          三种 mode、motion state、回写
├─ mmd_joint.cpp               Spring6Dof + 2.75 参数
├─ mmd_physics_sync.cpp        每帧顺序、reset/warmup
└─ mmd_physics_compat.cpp      对 Scene/Entity 的薄接口
```

动画编排按 Saba 的 `UpdateAllAnimation` 顺序：

```text
VMD eval → Morph → NodeAnimation(physics 前) → Physics → NodeAnimation(physics 后)
```

IK/append 放入正确阶段，以 three.js MMDAnimationHelper 作为 warmup/IK 对照。

## 参考实现映射

| WISTERIA 新模块 | Saba 参考 | three.js 参考 |
|---|---|---|
| 世界/步进 | `MMDPhysics::Create/Update` | `MMDPhysics._stepSimulation` |
| 刚体 mode | `MMDRigidBody` + 两套 motion state | `RigidBody`（type 0/1/2） |
| 关节 | `MMDJoint`（Spring6Dof + 限位/弹簧） | `Constraint`（StopERP 0.475） |
| 每帧同步 | `UpdatePhysicsAnimation` | `updateFromBone → step → updateBone` |
| reset/warmup | `ResetPhysics`（全 kinematic + 1 步 + 清速度） | `warmup(cycles)` |

## 接口契约（重写期间保持不变）

- `MmdPhysicsAsset`：刚体/关节定义结构不变，新运行时直接消费。
- `PhysicsInstance`：`PrepareSimulation` / 子步 / `FinishSimulation` /
  `ResetSimulation` / `StabilizationRequest` 生命周期不变。
- `PhysicsWorld`：body/constraint 工厂不变；legacy Spring6Dof 作为可选开关。
- `Scene::InstantiateModel` / `Entity::SetMmdPhysics`：调用方式不变。

## 阶段计划与验收

### 阶段 1：最小 compat 核心（参考 Saba 重写）

- 范围：刚体创建、三种 mode、Spring6Dof、每帧同步、reset/warmup。
- 明确不做：recovery、chain profile、语义过滤、adaptive CCD、复杂诊断。
- 验收：叶瞬光 495 刚体 / 568 关节初始化回归通过；720 帧 A/B 无 NaN、
  无 SAFE FREEZE；与 P0 的 legacy 数据可比。

### 阶段 2：并行接入

- 新核心以 `MMD_COMPAT` profile 形态接入 `Entity::SetMmdPhysics`，与旧实现并存。
- 验收：同一模型两种实现都能跑，A/B 指标可复现。

### 阶段 3：切换与删除

- 默认切换到 compat 核心；删除旧 adaptive 代码。
- 验收：全部现有测试通过；adaptive 增强若仍需保留，以独立可选组件形式回归，
  不允许回到单文件 5000 行。

### 阶段 4：动画层与引擎后端

- 重写 pose/IK/物理编排。
- 可选：Bullet 2.82/2.75 作为第二个后端做 A/B。

## 测试安全网

- `wisteria_tests.exe` 全量回归。
- `TestDemoPmxVmdInitializationStabilizationWhenAvailable`：叶瞬光 720 帧。
- `TestP0Bullet275CompatibilityLongRunWhenAvailable`：7 臂 A/B，记录
  `maxLinearViolation` / `maxAngularViolationDeg` / `severeJoints`。

## 风险与开放问题

- Saba 是自研 PMX/PMD 解析器，而 WISTERIA 用 Assimp MMD importer；
  重写时只借鉴运行时结构，不引入第二套解析器。
- Saba 核心库依赖 Bullet 3.25 语义；若后续使用 2.82/2.75，需要单独验证。
- 是否保留少量 adaptive 增强（如 CCD）应在阶段 3 根据数据决定，而不是默认保留。
