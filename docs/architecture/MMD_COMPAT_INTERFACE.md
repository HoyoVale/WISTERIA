# MMD Compat 接口契约（草案 v1）

> 状态：已归档（2026-08-04）。该草案对应的 compat 实现已随整体切换到
> Saba 而删除；保留本文仅作接口演进记录。

## 目标

为“按 Saba 骨架重写 MMD 适配层”定义第一版接口边界：

- 新核心不感知 `Entity` / `Scene`，只依赖格式无关的 `PhysicsWorld` 和
  `Pose` / `Transform` / `MmdPhysicsAsset`。
- 旧 `MmdPhysicsInstance`（adaptive 版）保留到阶段 3，新核心以独立模块并存。
- 所有 Bullet 类型继续藏在 `PhysicsWorld` 后面，新核心不直接 include bt*。

## 分层

```text
Entity / Scene
  └─ PhysicsInstance 抽象
       └─ MmdCompatPhysicsInstance     薄壳：对接 Entity 生命周期
            └─ MmdCompatRuntime        核心：骨骼↔物理同步
                 └─ PhysicsWorld       格式无关 Bullet 封装（已有）
```

数据流：

```text
MmdPhysicsAsset（导入层，冻结）
  → MmdCompatRuntime::Create()
      → PhysicsWorld::CreateBody / CreateSpring6DofConstraint
  → 每帧
      PrepareSimulation → UpdateFromBones
      PrepareSimulationSubstep（Scene 每子步）→ 更新 kinematic 目标
      FinishSimulation → UpdateBones
```

## Saba 概念映射

| Saba | WISTERIA 新接口 | 说明 |
|---|---|---|
| `MMDModel` | `Pose` / `Skeleton` | 骨骼层级与全局矩阵 |
| `MMDNode` | `Pose` 内的骨骼索引 | 不做独立节点类 |
| `MMDPhysics` | `MmdCompatRuntime` | 世界创建、步进、刚体/关节注册 |
| `MMDRigidBody` | 隐藏于 `MmdCompatRuntime::Impl` | 三种 mode、offset、回写 |
| `MMDJoint` | 隐藏于 `MmdCompatRuntime::Impl` | Spring6Dof + 2.75 参数 |

## 接口清单（v1 草案）

### `MmdCompatSettings`

```cpp
struct MmdCompatSettings
{
    std::string name = "mmd-compat-v1";

    glm::vec3 gravity{0.0f, -9.8f, 0.0f};
    float fixedTimeStep = 1.0f / 60.0f;
    int maxSubSteps = 4;

    bool legacySpringConstraint = false;        // P0 开关
    bool disableOffsetForConstraintFrame = false;
    float constraintStopErp = 0.475f;
    bool disableDynamicDeactivation = false;
    bool disableLinkedBodyCollisions = true;

    MmdPhysicsWithBoneSyncMode physicsWithBoneSync =
        MmdPhysicsWithBoneSyncMode::RotationOnly;
};
```

### `MmdCompatRuntime`

```cpp
class MmdCompatRuntime
{
public:
    MmdCompatRuntime(PhysicsWorld& world,
                     const MmdPhysicsAsset& asset,
                     Pose& pose,
                     const Transform& transform,
                     const MmdCompatSettings& settings);
    ~MmdCompatRuntime();

    bool Create();
    void Destroy() noexcept;

    void UpdateFromBones();      // 骨骼 → kinematic/目标
    void Step(float deltaTime);  // 推进 Bullet
    void UpdateBones();          // 物理 → 骨骼
    void Update(float deltaTime);

    void Reset();
    void SetGravity(const glm::vec3& gravity);

    std::size_t RigidBodyCount() const noexcept;
    std::size_t JointCount() const noexcept;
    PhysicsBodyState BodyStateAt(RigidBodyIndex index) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
```

### `MmdCompatPhysicsInstance`

```cpp
class MmdCompatPhysicsInstance final : public PhysicsInstance
{
public:
    MmdCompatPhysicsInstance(PhysicsWorld& world,
                             const MmdPhysicsAsset& asset,
                             Pose& pose,
                             Transform& transform,
                             const MmdCompatSettings& settings = {});
    ~MmdCompatPhysicsInstance() override;

    void PrepareSimulation(float deltaTime) override;
    void PrepareSimulationSubstep(float alpha, float fixedTimeStep) override;
    void FinishSimulation() override;
    void ResetSimulation() override;

    std::size_t RigidBodyCount() const noexcept;
    std::size_t JointCount() const noexcept;
    PhysicsBodyState BodyStateAt(RigidBodyIndex index) const;

private:
    MmdCompatRuntime runtime;
};
```

## 生命周期

1. `Entity::SetMmdPhysics` 创建 `MmdCompatPhysicsInstance`；
2. 构造时 `MmdCompatRuntime` 持有引用但不创建 Bullet 对象；
3. `Scene::Update(0.0f)` 触发首次同步与稳定；
4. 每帧由 Scene 驱动 `PrepareSimulation` / 子步 / `FinishSimulation`；
5. `ResetSimulation` 调用 `MmdCompatRuntime::Reset()`。

## 已确认决策（2026-08-03）

1. **reset/warmup：按 Saba 实现。**
   - 全部刚体切 kinematic → `ResetTransform` → 跑 1 步 1/60 →
     清速度/接触代理 → 恢复 dynamic。
   - 不沿用 Scene 的隐藏 30 步稳定机制。
2. **Mode 2：留开关，默认 Saba 语义。**
   - Saba 的 `DynamicAndBoneMergeMotionState` = 物理旋转 + 骨骼平移，
     对应 WISTERIA 的 `RotationOnly`。
   - v1 默认 `MmdPhysicsWithBoneSyncMode::RotationOnly`；
     `FullBody` / `TranslationDelta` 保留为可切换扩展。
3. **固定步所有权：由 compat settings 管。**
   - `MmdCompatSettings::fixedTimeStep` / `maxSubSteps` 是唯一来源。
   - `PhysicsWorld` 新增 `StepSimulation(timeStep, maxSubSteps, fixedTimeStep)`
     透传 Bullet accumulator，`MmdCompatRuntime::Step` 按 Saba 方式调用。
   - Scene 共享 world 的阶段，仍由 Scene 固定步循环驱动；
     独立 Saba 式步进方法已可用，后续若需要每模型独立 world 再演进。
4. **旧实现替换时机：接口确认后即可替换。**
   - `MmdCompatPhysicsInstance` 确认可用后，逐步替换旧
     `MmdPhysicsInstance`，不在阶段 3 等待。

## 未决问题（实现阶段再定）

- 诊断：第一版只保留 `BodyStateAt`，`LogAlignmentReport` 等旧诊断不迁移。
- 每模型独立 world vs 共享 Scene world：v1 先用共享 world + Saba 式
  `StepSimulation`；若出现步进/隔离问题，再演进为独立 world。

## 验收

- 头文件可编译，`wisteria_tests` 全量通过。
- 旧 `MmdPhysicsInstance` 行为不变（P0 A/B 继续可跑）。
- 阶段 2 接入后，`MmdCompatPhysicsInstance` 与旧实现可在同一 Scene 中 A/B。

## 实现状态（2026-08-03）

v1 核心已落地：

- `MmdCompatRuntime::Create`：刚体/关节创建完成，含 legacy Spring6Dof 与
  2.75 参数。
- 三种 mode 骨骼↔物理同步完成；Mode 2 默认 `RotationOnly`（Saba 语义），
  `FullBody` / `TranslationDelta` 保留开关。
- `Reset`：Saba 式全 kinematic + 1 步 1/60 + 清动力学（contact proxy 清理
  暂未实现，记录为后续项）。
- demo（`DemoScene`）已切换到 `MmdCompatPhysicsInstance`；旧实例代码与旧测试
  暂时保留，用于 A/B 回归。

叶瞬光 A/B 结果（720 帧，compat v1 = 纯 Saba 语义、无 adaptive）：

| 实现 | bodies | joints | finite | linearViol | angularViolDeg | severe |
|---|---|---|---|---|---|---|
| legacy | 495 | 568 | true | 0.916 | 33.6 | 4 |
| compat v1 | 495 | 565 | true | 1.405 | 44.5 | 25 |

compat v1 不含 adaptive 过滤，因此违规指标高于 legacy（与 P0 raw 结论一致）；
后续通过 legacy 参数 + 1/65/120 固定步 + 重力审计继续优化。
