# WISTERIA 物理分层审计

基线：`WISTERIA(11)`  
目标：冻结已经可靠的底层抽象，把 MMD 兼容行为、实验性适配和模型特例从底层 Bullet 封装中隔离出来。

> **状态声明（2026-08-07）**：本文描述的是 **Legacy WISTERIA-owned
> physics path**（`MmdPhysicsInstance` + `PhysicsWorld` +
> `MmdPhysicsRuntimePolicy`），目前不参与 `SabaMmdRuntimeModel`
> 主运行链；保留为历史架构与未来通用 `PhysicsInstance` 参考。
> 活动 MMD 后端与 R1.3 Phase 0A 契约见
> [R1_3_MMD_COMPAT_CONTRACT.md](R1_3_MMD_COMPAT_CONTRACT.md)。

## 1. 审计结论

当前工程不需要重写物理底座。可长期保留的主链已经形成：

```text
PMX / VMD importer
        ↓
ModelAsset + MmdPhysicsAsset
        ↓
Entity + Pose + Transform
        ↓
PhysicsInstance（格式无关生命周期）
        ↓
MmdPhysicsInstance（MMD 适配器）
        ↓
PhysicsWorld（Bullet 所有权与通用刚体 API）
```

主要问题不是抽象缺失，而是 `MmdPhysicsInstance` 同时承担了太多性质不同的工作：

- PMX 刚体/关节到 Bullet 的标准映射；
- FollowBone / Physics / PhysicsWithBone 同步；
- Reset、稳定预演算与固定步观察；
- CCD、margin、求解器相关补偿；
- 裙摆/头发/尾巴分类；
- 重力和阻尼 profile；
- runaway 恢复；
- 语义碰撞过滤；
- 对齐、接触、冲量和蒙皮诊断。

这些功能不能继续以文件顶部常量和隐式规则的方式增长。

## 2. 可保留的底层抽象

### 2.1 `PhysicsWorld`

位置：

- `include/wisteria/physics/physics_world.hpp`
- `src/physics/physics_world.cpp`

可保留职责：

- Bullet world、shape、body、constraint 的唯一所有者；
- generation handle 的创建、校验和销毁；
- Static / Dynamic / Kinematic 通用运动类型；
- 刚体状态、冲量、重力、阻尼、CCD 等低层操作；
- 固定步 `StepFixed`；
- contact manifold、constraint feedback 和通用 debug line；
- 不认识 PMX、骨骼、裙摆、头发或 Mode 2。

判断：**稳定，可作为其他格式、车辆或角色控制器的公共后端。**

### 2.2 `PhysicsInstance`

位置：`include/wisteria/physics/physics_instance.hpp`

可保留职责：

- Entity 级、格式无关的物理生命周期；
- render frame 前准备；
- 每个固定子步前后的观察点；
- render frame 后回写；
- Reset 和可选稳定预演算；
- 通用 debug line 输出。

Scene 只依赖这个接口，而不依赖 MMD 数据结构。该边界允许未来增加 glTF 角色、车辆或其他物理适配器。

判断：**稳定，是当前架构中最重要的隔离层。**

### 2.3 `PhysicsTypes` 与 handle

位置：`include/wisteria/physics/physics_types.hpp`

可保留内容：

- shape/body/constraint 描述符；
- `PhysicsBodyHandle`、`PhysicsConstraintHandle`；
- body/contact/constraint 运行时状态；
- frame/world 统计结构。

判断：**数据结构本身可保留，但部分默认值属于策略泄漏，见第 4 节。**

### 2.4 `MmdPhysicsAsset`

位置：

- `include/wisteria/mmd/physics/mmd_physics_asset.hpp`
- `src/mmd/physics/mmd_physics_asset.cpp`

可保留职责：

- PMX 物理元数据的不可变资源表示；
- rigid body mode、shape、bone mapping、group/mask、质量和阻尼；
- joint frame、限制和弹簧；
- bind transform 与 body/bone 双向偏移。

判断：**稳定。它描述模型作者的数据，不应该被运行时启发式直接改写。**

### 2.5 Scene 固定步生命周期

位置：`src/scene/scene.cpp`

可保留职责：

- accumulator；
- catch-up 限制；
- Kinematic 目标的子步采样；
- Scene 统一推进共享 PhysicsWorld；
- 稳定预演算与实时 accumulator 分离。

判断：**稳定。MMD adapter 不应私自推进共享世界。**

### 2.6 Bullet 与 GLM 转换

位置：

- `include/wisteria/physics/physics_bullet_conversion.hpp`
- `src/physics/physics_bullet_conversion.cpp`

判断：**稳定。格式单位和坐标约定应在适配层审计，转换函数本身保持通用。**

## 3. MMD 适配器中应保留的标准职责

位置：

- `include/wisteria/mmd/physics/mmd_physics_instance.hpp`
- `src/mmd/physics/mmd_physics_instance.cpp`

应继续属于 MMD adapter 的内容：

- PMX rigid body mode 到 Static/Dynamic/Kinematic 的映射；
- PMX group/mask、质量、摩擦、反弹和阻尼的提交；
- PMX joint frame 与限制的构造；
- FollowBone 的动画驱动；
- Physics 与 PhysicsWithBone 的 post-physics 骨骼回写；
- Impulse Morph；
- Entity transform、skeleton root、body-to-bone 空间转换；
- Reset 到当前 Pose；
- MMD 专用诊断数据。

这些是格式语义，而不是模型特例。

## 4. 仍需从底层默认值中剥离的策略泄漏

本轮没有擅自改变以下行为，只将其列入兼容性审计。

### 4.1 `PhysicsStepSettings` 默认值

当前默认包含：

- 15 solver iterations；
- split impulse；
- ERP / ERP2；
- maximum error reduction；
- restitution velocity threshold。

结构应继续留在通用 physics 层，但**默认 preset 的选择**不应永久由通用类型替 MMD 决定。后续建议提供：

```text
PhysicsStepSettings::BulletDefaults()
MmdPhysicsWorldPreset::CommunityCompat(...)
MmdPhysicsWorldPreset::WisteriaAdaptive(...)
```

而不是让所有 PhysicsWorld 都继承当前 MMD 调试阶段的参数。

### 4.2 世界重力 `-9.8`

`PhysicsWorld` 可以拥有默认重力，但“一个 PMX 单位对应多少世界长度”和 MMD 兼容重力应由 MMD world/profile 明确决定。需要做完整单位审计后再改。

### 4.3 `disableCollisionsBetweenLinkedBodies = true`

当前所有 constraint descriptor 默认关闭相连刚体碰撞。该字段作为通用能力合理，但默认值可能改变 PMX 作者通过 group/mask 表达的意图。

后续应由 MMD compatibility profile 显式选择，不能继续依赖 descriptor 的隐式默认值。

### 4.4 size-derived Box margin

PhysicsWorld 支持显式 margin 是正确抽象；“未指定时如何从尺寸推导”属于后端默认策略。MMD 适配应可以显式覆盖，社区兼容模式不得静默使用 WISTERIA 的密集裙摆经验值。

## 5. 明确属于配置/实验层的内容

以下内容不应写死进 PhysicsWorld，也不应被描述为 PMX 标准：

- recovery 阈值、持续时间、冷却和熔断；
- 自适应 CCD 候选和启停阈值；
- 同链近邻碰撞过滤；
- 裙摆名称与主/辅助层语义过滤；
- skirt/hair/tail/accessory 自动分类；
- chain gravity scale 和最低阻尼；
- `DECORATIVE_FALLBACK`；
- Mode 2 的 `FULL_BODY`、`TRANSLATION_DELTA` 实验模式；
- 局部 body margin、安全半径和模型覆盖修复；
- 单模型碰撞 pair override。

这些能力可以保留，但必须处于：

```text
WISTERIA_ADAPTIVE 或 MODEL_PROFILE
```

而不是伪装成 MMD 标准行为。

## 6. 本轮完成的结构调整

### 6.1 目录模块化

```text
include/wisteria/
├─ animation/
├─ assets/
├─ common/
├─ core/
├─ mmd/
│  └─ physics/
├─ physics/
├─ platform/
├─ rendering/
│  └─ primitives/
├─ scene/
└─ vendor/

src/
├─ animation/
├─ assets/
├─ common/
├─ core/
├─ mmd/
│  └─ physics/
├─ physics/
├─ platform/
├─ rendering/
│  └─ primitives/
└─ scene/
```

所有项目头文件统一使用：

```cpp
#include "wisteria/<module>/<file>.hpp"
```

这样模块依赖可从 include path 直接识别，避免再次退回平铺目录。

### 6.2 MMD 公共类型拆分

从原 `mmd_physics_instance.hpp` 拆出：

- `mmd_physics_modes.hpp`：运行模式枚举；
- `mmd_physics_diagnostics.hpp`：统计和诊断 DTO；
- `mmd_physics_policy.hpp`：可注入的 WISTERIA 自适应策略。

### 6.3 策略注入接口

现有默认行为保持：

```cpp
entity.SetMmdPhysics(world, asset);
```

也可以显式注入：

```cpp
MmdPhysicsRuntimePolicy policy =
    MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults();
policy.recovery.enabled = false;
policy.ccd.adaptive = false;
policy.enableChainProfiles = false;

entity.SetMmdPhysics(world, asset, policy);
```

当前策略对象是代码级注入点。外部 JSON/TOML profile 应在兼容层设计冻结后增加，避免现在把错误字段固化为公开文件格式。

## 7. 本轮刻意没有做的事情

`mmd_physics_instance.cpp` 仍接近 5000 行。本轮没有机械拆成多个 `.cpp`，原因是社区兼容行为尚未冻结。如果现在按现有启发式拆散，会把暂时性规则扩散到更多内部接口。

行为基线确定后再拆为：

```text
mmd_physics_runtime.cpp       对象创建、销毁、生命周期
mmd_physics_sync.cpp          FollowBone / Mode 1 / Mode 2
mmd_physics_reset.cpp         reset、warmup、stabilization
mmd_physics_diagnostics.cpp   对齐、接触、约束、日志
mmd_physics_adaptive.cpp      recovery、chain profile、语义过滤
```

这一步应在兼容模式的回归样本建立后执行。

## 8. 最终边界

```text
通用底层
PhysicsWorld + PhysicsInstance + PhysicsTypes

MMD 标准适配
MmdPhysicsAsset + MmdPhysicsInstance 的标准同步/构建部分

社区兼容 preset
单位、重力、阻尼/弹簧换算、Reset、Mode 2 quirks、linked collision

WISTERIA 自适应
恢复、CCD、链分类、裙摆过滤、诊断增强

模型 profile
按模型指纹提供人工确认的局部 override
```

后续任何物理改动都应先回答：它属于哪一层，是否能在纯 `MMD_COMPAT` 模式下关闭。
