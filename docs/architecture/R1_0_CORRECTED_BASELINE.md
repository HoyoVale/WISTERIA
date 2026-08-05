# R1.0 校正基线

> 本文记录 R1.0 经交叉复核后的准确定位、测试验证结果与已知边界。
> 后续所有开发以此基线为准。

## 1. 定位

R1.0 完成了 WISTERIA 对 Saba MMD 实例所有权、Scene 调度权和实例级
渲染数据的第一轮收口，但尚未完成帧状态、ABI 安全、资产来源和全部动态
模型路径的统一。

## 2. 已验证成立的事实

- 所有模型实例化时都有 `ModelInstance`；
- Saba PMX Runtime 由 WISTERIA 创建和销毁；
- Entity 持有 `ModelInstance`；
- Scene 调度 MMD Runtime；
- MMD 动态 Mesh 为实例级数据；
- 同资产多实例的动画、Pose、Morph、顶点和物理所有者独立；
- Renderer 不依赖 Saba 类型（`src/rendering/` 零 Saba 引用）；
- Demo 不再承担手工桥接职责；
- Entity 级 C API 控制正在渲染的 MMD 实例（v0.7）。

## 3. 已确认未完成

- Saba 仍直接接触部分 `Mesh/Camera/Light` 宿主对象；
- 完整帧状态未通过中立 Snapshot 交付；
- 大部分 C ABI 尚无统一异常边界；
- 完整物理状态不可查询、不可导出；
- 通用骨骼动画和 Morph 仍有 Entity 旧路径；
- 真静态模型与通用动态模型未分类；
- Saba Runtime 可能二次读取 PMX；
- 确定性 seek/replay 未建立；
- 完整资产测试曾被 SKIP 掩盖（本基线已修复）。

## 4. 测试修正记录

### 问题

`TestDirectPmxMaterialImportWhenAvailable()` 原断言
`morphInstance.HasMorphState()`，与 R1.0「backend 驱动 PMX 的 Morph 归
Saba Runtime 管理」冲突。补丁验证环境缺少 `assets/models/mmd/仪玄_pmx/
仪玄.pmx`，该用例 SKIP，掩盖了冲突。

### 修正

断言改为验证新架构语义：

- Entity 持有 `ModelInstance`；
- runtime 解析为 `saba-mmd`；
- 实例级 Mesh 已分配；
- 命名 Morph 可经 runtime 设置。

### 验证结果（R1.0A 基线）

| 平台 | CTest | Direct PMX material |
| ---- | ----- | ------------------- |
| Windows（MSVC RelWithDebInfo） | 4/4 | PASS（真跑） |
| Linux（WSL + llvmpipe） | 4/4 | PASS（真跑） |

## 5. 物理能力真实暴露面

`MmdPhysicsRuntimeSettings`：`fixedTimeStep` / `maxSubSteps` / `gravity` /
`enabled`；`wisteria_physics_reset` 只重置物理。Mode/CCD/margin/solver
等未进入通用 Runtime 契约。

## 6. 已知未覆盖项（保留为后续阶段）

- GuardAbi 未覆盖的全部导出函数（见 `C_ABI_SAFETY_MATRIX.md`）；
- 静态/通用模型双路径；
- 资产 manifest 的 FULL_ASSETS 档位；
- Context 显式资源根。

## 7. 归档校验

`script/cleanup_r1_assets.*` 为补丁携带的可选清理脚本，默认不执行。
