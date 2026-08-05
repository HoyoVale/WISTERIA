# MMD 物理社区兼容基线（#5 Phase 0/1）

> 状态：进行中。本文记录单位/重力审计结论、可复现基线协议与 Saba 路径的
> 能力盘点，作为社区兼容矩阵（mode/阻尼/CCD/语义过滤）落地前的对照基准。

## 1. 单位与重力审计（Phase 1.1）

**结论：当前默认重力 `-98` 与参考实现一致，不修改。**

证据：saba `MMDPhysics.cpp` 构造世界时硬编码

```cpp
m_world->setGravity(btVector3(0, -9.8f * 10.0f, 0));
```

即 saba 的 Bullet 世界以 10:1 尺度运行（PMX 厘米坐标下，-9.8 m/s² 等价于
世界单位 -98）。WISTERIA `SabaPhysicsSettings` 默认 `gravity{0, -98, 0}`
与 saba 参考一致；C ABI `wisteria_set_physics_settings` 传 0 保留运行时默认。
社区采用计划中“单位审计完成前不把重力改为 -98”的警告针对的是通用
`MmdPhysicsInstance` 路径（其世界尺度需单独审计），不是 Saba 路径。

## 2. 可复现基线协议

### 资产

- 模型：`叶瞬光.pmx`（`WISTERIA_ASSET_ROOT/models/mmd/叶瞬光_pmx`，含路径
  回退到 `#U53f6#U77ac#U5149_pmx`）；
- 动作：无（physics-only，位移完全由刚体世界在重力下沉降驱动）；
- 固定步：`1/120 s`，`maxSubSteps = 10`；
- 总帧数：300，采样间隔：10 帧。

### 指标与断言

- 顶点有限（`DiagnoseVertices().finite`）；
- `maximumDisplacementFromBind < 5.0`（实测收敛于 `~0.068`，70 倍余量，用于
  抓 runaway）；
- 收敛：frame 200 与 frame 300 的位移差 `< 0.01`；
- Pose 全部骨骼局部矩阵有限。

### 轨迹导出（Phase 0.2 骨架）

`WISTERIA_PHYSICS_TRACE=<path>` 导出 CSV：

```text
frame,min_x,min_y,min_z,max_x,max_y,max_z,max_displacement
```

供与 babylon-mmd / libmmd / nanoem 的参考轨迹逐帧对照（RMS/max 指标见
`MMD_PHYSICS_COMMUNITY_ADOPTION_PLAN.md` Phase 0.3）。

## 3. Saba 路径能力盘点

| 旋钮 | 状态 |
|---|---|
| fixedTimeStep / maxSubSteps | 引擎可配置（`SabaPhysicsSettings` + C API） |
| gravity | 引擎可配置；默认 -98 与 saba 一致 |
| 语义碰撞过滤 | saba 内部 `MMDFilterCallback`，引擎不可配置 |
| Mode 0/1/2（物理相对 IK 的执行相位） | saba 内部 `UpdatePhysicsAnimation` 固定处理，引擎不可配置 |
| 阻尼 / CCD / recovery / adaptive 增强 | saba 路径未实现；仅通用 `MmdPhysicsInstance` 有类型定义，未接入 |

## 4. 下一步

1. 用参考实现（babylon-mmd / libmmd）对同一资产导出轨迹，与本基线指标
   对照，形成社区差异矩阵；
2. 按矩阵定义 Mode 0/1/2、阻尼、CCD、语义过滤在 saba 路径上的语义；
3. 引擎实现（patch saba 或桥接通用世界）后，扩展 `SabaPhysicsSettings` 与
   C ABI（`wisteria_physics_capabilities` 对应位随之启用）；
4. 特殊模型的外部 JSON/TOML profile 最后接入。
