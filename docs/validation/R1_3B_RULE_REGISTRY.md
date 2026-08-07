# R1.3B Phase 0B — 规则登记表（Rule Registry）

> 契约：`docs/architecture/R1_3B_MMD_COMMUNITY_COMPARISON_CONTRACT.md`
> §7.5 / §12 Step 10。本文件为人工评审 Source of Truth；需要自动化时
> 再生成 JSON sidecar。
>
> 裁决枚举：ADMITTED_COMMUNITY / ADMITTED_ADAPTIVE / REJECTED /
> INCONCLUSIVE / REFERENCE_SPECIFIC。任何 effective 行为变化都会
> bump profileRevision（COMMUNITY v1 == RAW）。
>
> 证据链：社区源码/轨迹发现差异 → 候选规则 → WISTERIA 独立实现开关 →
> 同 environmentMode/executionProfile A/B → Admission decision。

## 源码证据引用基线

```text
saba          vendored（MIT），commit 由 WISTERIA 仓库管理
babylon-mmd   3f523d392c176d5c9c9f9264f622d0631c1d298e（MIT）
nanoem        30acffaa29f5d2eb9e997d69418f2e4b97b5894f
              （nanoem component MIT；emapp MPL，仅证据）
libmmd        091e70c55dc4c6f2e7ad8d46fea92ce3d1849ba5（Boost 1.0，
              Historical Reference）
```

## LB-01：Linked-body collision

```text
rule-id：LB-01
问题：两个由 constraint 连接的刚体是否应互相碰撞（PMX mask 之外
      是否另有禁碰规则）

源码证据：
  saba    MMDPhysics.cpp：m_world->addConstraint(mmdJoint->GetConstraint())
          → 不传 disableCollisionsBetweenLinkedBodies（基线 PmxMaskOnly）
  babylon mmdAmmoPhysics.js（joint 创建）：
          collision: true // do not disable collision between the two rigid bodies
          mmdAmmoJSPlugin.js：world.n(joint, !impostorJoint.joint.jointData.collision)
          → 默认不禁碰；PMX joint 的 collision 标志可逐关节禁碰
  nanoem  physics_bullet.cc：world->m_world->addConstraint(joint->m_internalConstraint)
          → 不传 disable（与 saba 一致）
许可证：三者均可作 Evidence（MIT / MIT / MIT）

开关：MmdPhysicsCompatibilityProfile::linkedBodyCollision
      （PmxMaskOnly / DisableConstraintLinkedPairs）
测试：TestBulletLinkedBodyCollisionDisable（Bullet 语义）
      TestR13LinkedBodyAbSmoke（确定性 + ground 不受影响）
      TestR13TraceDiffExtendedLocators（接触拓扑定位）
轨迹 A/B（WISTERIA 同 executionProfile）：
      凑企鹅 VMD：First contact-topology divergence = motionFrame 1 / pair (0,1)
      蕾米埃尔 VMD：frame 10 / pair (0,248)
      （见 R1_3B_STEP9_TRACES_20260807.md §7.2–7.4）

裁决：
  PmxMaskOnly
    → ADMITTED_COMMUNITY（v1 基线确认：saba/babylon 默认/nanoem
      三方一致；无行为变化，不 bump revision）
  DisableConstraintLinkedPairs
    → REJECTED for COMMUNITY（社区默认均不禁碰；该开关是 WISTERIA
      A/B 诊断）。若未来作为 ADAPTIVE 增强，需先经契约评审。
  babylon 逐关节 collision 标志
    → INCONCLUSIVE（是否映射 PMX joint 数据、saba/nanoem 是否支持，
      需要 corpus joint 数据核验）
```

## M2-01：Mode 2 写回（PhysicsWithBone）

```text
rule-id：M2-01
问题：Mode 2 刚体回写骨骼时，平移应来自物理还是保持动画

源码证据：
  saba    MMDPhysics.cpp DynamicAndBoneMergeMotionState::ReflectGlobalTransform：
          btGlobal[3] = global[3]（保持动画平移，只写物理旋转）
  babylon mmdAmmoPhysics.js syncBones（PhysicsWithBone）：
          先保存 bone world translation → 以 ZeroVector 平移写物理旋转 →
          恢复动画平移（与 saba 语义一致）
  nanoem  emapp/src/model/RigidBody.cc（emapp component，MPL evidence-only）：
          FROM_BONE_ORIENTATION_AND_SIMULATION_TO_BONE →
          物理提供朝向，骨骼平移保持动画/父链
许可证：MIT / MIT / MPL（仅证据）

开关：MmdPhysicsCompatibilityProfile::mode2
      （PreserveAnimatedTranslation / FullTransformDiagnostic /
       StrictBoneLength=Reserved）
测试：TestR13Mode2AbSmoke（确定性）
      TestR13Mode2WritebackPose（FULL_ASSETS 生产模型写回生效）
轨迹 A/B（WISTERIA 同 executionProfile）：
      叶瞬光 no-VMD：FullTransformDiagnostic 只改变骨骼写回
      （First bone divergence frame 10 / bone 17 / delta 0.092，
        无 body COM / 接触拓扑变化）
      （见 R1_3B_STEP9_TRACES_20260807.md §7.1）

裁决：
  PreserveAnimatedTranslation
    → ADMITTED_COMMUNITY（v1 基线确认：saba/babylon 实现一致，
      nanoem 语义一致；无行为变化，不 bump revision）
  FullTransformDiagnostic
    → REJECTED for COMMUNITY（社区实现均保持动画平移；该模式只作
      诊断）。若证据支持完整写回为正式语义，须按契约 §7.4 先晋升
      为 FullTransformWriteback（契约评审）再讨论 ADAPTIVE。
  StrictBoneLength
    → 保持 Reserved（无算法定义；libmmd strict 仅提供线索）
```

## 开放项（下一步完成证据包）

```text
1. babylon 逐关节 collision 标志的 corpus 数据核验（LB-01 INCONCLUSIVE）；
2. 分叉源逐刚体归属：凑企鹅 pair (0,1) / body 8、蕾米埃尔 joint 696
   （Step 9 §7.4 遗留）；
3. 两处 ADMITTED_COMMUNITY 均为 v1 基线确认（无行为变化）——
   等待第一条真正改变 COMMUNITY 行为的规则出现时 bump v2。
```

## 状态

```text
登记日期：2026-08-07
证据整理：DeepSeek（Codex）
裁决确认：待用户（Floral Wisteria）拍板
```
