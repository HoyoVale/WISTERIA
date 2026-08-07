# nanoem feasibility spike（R1.3B Phase 0B，2026-08-07）

> 契约：`docs/architecture/R1_3B_MMD_COMMUNITY_COMPARISON_CONTRACT.md` §12
> 第 7 步。结论：**nanoem 保持 Source Semantics（第一轮强制）**；
> Runnable 路径存在且可行，但工程量中等，推迟到 Phase 0B 第二轮
> 或 Mode 2 分歧需要第三参考时再评估。

## 1. 冻结身份

```text
仓库：https://github.com/hkrn/nanoem
commit：30acffaa29f5d2eb9e997d69418f2e4b97b5894f
许可证（LICENSE.md 原文）：
  nanoem component            MIT/X11（LICENSE.MIT）
  emapp/win32/macos/glfw/sapp  MPL（LICENSE.MPL）
```

## 2. 源码语义（已核实，可作独立第二参考）

`nanoem/ext/physics_bullet.cc`（MIT 组件头注释确认）：

```text
- btDiscreteDynamicsWorld + btSequentialImpulseConstraintSolver
- btRigidBody 构造（motion state + shape + local inertia）
- 约束类型：btGeneric6DofConstraint / btGeneric6DofSpringConstraint /
  btConeTwistConstraint / btHingeConstraint / btPoint2PointConstraint /
  btSliderConstraint
- stepSimulation(delta, maxSubSteps, fixedTimeStep)
- setGravity / setGravityFactor（重力因子独立于世界重力）
- 地面（m_groundPlaneShape + m_nullRigidBody）
- 刚体 map 与 upcast 遍历（rigid_bodies / btRigidBody::upcast）
```

接口面：`nanoem/ext/physics.h`（PhysicsContext 等），上层通过
`nanoem_model_t` 驱动。

**源码归属（证据登记必须准确）**：

```text
nanoem/ext/physics_bullet.cc（MIT）
  → Bullet backend / gravity / ground / solver / rigid body /
    constraint 语义

emapp/src/model/RigidBody.cc（emapp component，MPL，仅证据不复制）
  → PMX rigid-body transform type / Mode 1/2 骨骼↔物理反馈语义：
    synchronizeTransformFeedbackFromSimulation(...)
    NANOEM_MODEL_RIGID_BODY_TRANSFORM_TYPE_FROM_SIMULATION_TO_BONE
    NANOEM_MODEL_RIGID_BODY_TRANSFORM_TYPE_FROM_BONE_ORIENTATION_AND_SIMULATION_TO_BONE
```

nanoem 的 Mode 2 写回语义主要在 emapp 层（MPL），不在 physics_bullet.cc；
引用时按 MPL evidence-only 登记。

### 原生默认 ground（已核实，不是 plane）

```text
nanoem 原生默认 ground：
  大静态 box（btVector3(50, 50, 50)，margin 0.5），
  world transform 置于 (0, -halfExtentY, 0)，顶面约在 y=0；
physics_bullet.cc 同时存在 btStaticPlaneShape + null-body 结构，
但 m_defaultGroundShapePtr 实际指向 groundBoxShape。
```

## 3. Runnable 评估

### 有利因素

```text
- sandbox/ 下有 MIT 控制台测试程序（model.cc / motion.cc / validate.cc /
  softbody.cc），无窗口依赖，由 CMake 构建；
- 物理后端由 NANOEM_ENABLE_BULLET 宏开关控制；
- 源码语义接口清晰，逐刚体读取可基于 physics.h + btRigidBody。
```

### 成本与风险

```text
- 需构建完整 nanoem SDK：CMake + dependencies 子模块
  （glm / bullet / protobuf-c 等），首次构建耗时且依赖网络；
- 需编写 nanoem 侧逐刚体导出适配器（等价于 reference adapter，
  输出 WISTERIA canonical coordinate + sourceRigidBodyIndex）；
- 需要锁定依赖版本与 commit，重复 Phase 0B 工具链 pin 流程；
- 估算：一个可用的逐刚体 trace harness 约 1–2 天。
```

### 结论

```text
Phase 0B 第一轮：Source Semantics（强制），不强行打包成 runnable。
升级 Runnable 的触发条件（满足其一即重新评估）：
1. 叶瞬光 Mode 2 分歧在统一 Clock + 逐刚体对照后仍无法在
   babylon / WISTERIA 之间裁决；
2. 某候选规则需要第三个独立运行时轨迹才能满足证据包。
```

## 4. 对候选规则的价值

```text
- Mode 2 写回：emapp/src/model/RigidBody.cc（MPL evidence-only）的
  FROM_SIMULATION_TO_BONE / FROM_BONE_ORIENTATION_AND_SIMULATION_TO_BONE
  提供与 saba/babylon 不同的第三方实现语义，可作源码级三方对照；
- 重力因子：setGravityFactor 语义值得纳入单位/重力裁决证据；
- 约束构造：6DOF/弹簧/锥/铰链/滑块齐全，可用于 joint 语义对照。
- 原生 ground：默认 ground box（非 plane）应在
  NativeCompatibilityAudit 中与 Saba plane、babylon synthetic box 三方对照。
```
