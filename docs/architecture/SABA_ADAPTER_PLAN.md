# Saba 承接实现计划

## 目标

按 [SABA_ADAPTER_INTERFACE.md](./SABA_ADAPTER_INTERFACE.md) 的契约，分阶段把 MMD
链路切换到 Saba。每一阶段都有“自动验收”（`wisteria_tests.exe`）和“手动验收”
（demo 窗口），完成一个阶段更新一次测试与本文档。

## 阶段总览

| 阶段 | 内容 | 自动验收 | 手动验收 |
|---|---|---|---|
| 0 | 接口冻结：头文件骨架 | 编译通过 | 无 |
| 1 | `SabaMmdImporter` | 与 Assimp importer 数据对照 | 无 |
| 2 | Mesh 动态顶点 + Saba 蒙皮 | 顶点上传/蒙皮测试 | 裙摆/头发柔软度 |
| 3 | CameraTrack + VMD 相机 | 相机采样测试 | demo 相机跟随 |
| 4 | PhysicsInstance 自步进 + Saba 物理 | 自步进/长跑测试 | 物理手感对比 |
| 5 | 整体切换 | 全量回归 | 默认 demo 流畅运行 |

## 阶段 0：接口冻结

任务：

- 在 `include/wisteria/runtime/` 新建：
  - `runtime_model_base.hpp`
  - `mmd_runtime_model.hpp`
- 在 `include/wisteria/assets/` 新建 `saba_mmd_importer.hpp`（声明）；
- 在 `include/wisteria/rendering/mesh.hpp` 增加动态顶点上传声明；
- 在 `include/wisteria/animation/animation.hpp` 增加 `CameraTrack` 声明；
- 在 `include/wisteria/physics/physics_instance.hpp` 增加
  `OwnsSimulationStep()`；
- Scene 固定步循环对自步进实例的跳过逻辑（最小实现）。

自动验收：

- `wisteria_tests.exe` 全量 PASS（72+ 项）；
- 新增 `TestInterfaceCompilation`：包含所有新头文件并调用空实现/默认实现。

手动验收：无。

## 阶段 1：SabaMmdImporter

任务：

- 实现 `SabaMmdImporter`：PMX/PMD → `ImportedModelData`；
- 顶点、材质、骨骼（append/IK/deformAfterPhysics）、morph、刚体/关节全部转换；
- 保留 `AssimpMmdImporter` 对照。

自动验收：

- 新增 `TestSabaMmdImporterWhenAvailable`：叶瞬光 PMX 用两种 importer 导入，
  断言骨骼数、刚体数、关节数一致；打印差异矩阵（顶点数、材质数、IK 数）；
- 对 `pmx_physics.pmx` 等测试资产做字段级断言（刚体/关节数量、弹簧值）。

手动验收：无（数据层）。

## 阶段 2：Mesh 动态顶点 + Saba 蒙皮

任务：

- `Mesh::UploadDynamicVertices` 实现（动态 VBO 更新，只更新 position/normal）；
- `SabaMmdRuntimeModel::Initialize/Update`：用 Saba `PMXModel::Update` 做
  BDEF/SDEF/QDEF 蒙皮，结果上传到 Mesh；
- 先做“Saba 蒙皮 → 我们渲染”的最小 demo（替换 compat 窗口）。

自动验收：

- `TestMeshDynamicUpload`：上传后顶点/法线数据与输入一致；
- `TestSabaSkinningWhenAvailable`：叶瞬光运行若干帧，顶点有限、包围盒合理、
  与 Saba 参考输出误差 < 1e-3。

手动验收：

- 双窗口：Saba 蒙皮 vs compat（WISTERIA GPU 蒙皮），重点看裙摆/头发是否更柔软；
- 记录观察结果到本文档。

## 阶段 3：CameraTrack + VMD 相机

任务：

- `AnimationClip::CameraTrack` 实现（含贝塞尔插值）；
- `SabaMmdImporter` 从 VMD 提取相机轨道；
- `MmdRuntimeModel::ApplyCameraTrack` 应用到 `Camera`。

自动验收：

- `TestCameraTrackSampling`：关键帧线性/贝塞尔采样、越界钳制；
- `TestVmdCameraImportWhenAvailable`：真实 VMD 相机轨道字段断言。

手动验收：

- demo 中相机跟随 VMD 播放（可与 Saba viewer 对照）。

## 阶段 4：PhysicsInstance 自步进 + Saba 物理

任务：

- `PhysicsInstance::OwnsSimulationStep()` 接入 Scene 固定步循环；
- `SabaMmdRuntimeModel` 内部使用 `saba::MMDPhysics`（独立 world、120Hz、-98）；
- 阶段内保留 WISTERIA compat 作为对照实现（阶段 5 删除）。

自动验收：

- `TestSceneOwnsSimulationStep`：自步进实例不被共享 StepFixed 驱动；
- `TestSabaMmdPhysicsLongRunWhenAvailable`：叶瞬光 720 帧 finite、无崩溃；
- 全量回归。

手动验收：

- 双窗口：Saba 物理 vs compat 物理，对比裙摆稳定性、手感；
- 记录观察结果到本文档。

## 阶段 5：整体切换

任务：

- Entity/Scene 支持 `RuntimeModelBase`；
- demo 默认使用 `SabaMmdRuntimeModel`；
- 旧 `MmdPhysicsInstance` / adaptive 代码归档或删除（保留 git 历史即可）；
- 更新 README 与架构文档。

自动验收：

- `wisteria_tests.exe` 全量 PASS；
- `TestSabaFullChainWhenAvailable`：Saba 导入 → 运行时 → 蒙皮 → 物理，
  叶瞬光 720 帧。

手动验收：

- 默认 demo（单窗口或双窗口）流畅运行、无错误日志；
- 用户确认观感与 `simple_mmd_viewer_glfw` 一致或更好。

## 验收机制

### 自动验收（每次阶段完成）

```powershell
cmake --build build --config RelWithDebInfo --target wisteria_tests -- -m
.\build\RelWithDebInfo\wisteria_tests.exe
```

要求：`PASS=全部`、`FAIL=0`、退出码 0。

### 手动验收（每次阶段完成）

```powershell
.\build\RelWithDebInfo\wisteria.exe            # 双窗口/默认 demo
.\build\RelWithDebInfo\simple_mmd_viewer_glfw.exe `
  -model "assets\models\mmd\叶瞬光_pmx\叶瞬光.pmx" `
  -vmd "assets\motions\皮卡皮卡皮卡丘+\身体动作.vmd"
```

用户检查清单：

- 无崩溃、无 `[ERROR]` 日志；
- 物理稳定（裙摆/头发不飞、不僵、不穿模）；
- 观感与 Saba viewer 可对比；
- 每个阶段的观察记录追加到本文档。

## 当前状态

- 接口契约：已定（v1）；
- 实现进度：
  - **阶段 0：已完成（2026-08-04）**
    - `include/wisteria/runtime/runtime_model_base.hpp`：`RuntimeModelBase`；
    - `include/wisteria/runtime/mmd_runtime_model.hpp`：`MmdRuntimeModel` +
      `MmdSkinningKind`；
    - `include/wisteria/assets/saba_mmd_importer.hpp` +
      `src/assets/saba_mmd_importer.cpp`：导入器骨架（阶段 1 填充）；
    - `ModelImporter::Import` 改为 virtual，接口可扩展；
    - `Mesh::UploadDynamicVertices` / `HasDynamicVertexSource`（阶段 2 实现）；
    - `CameraTrack` / `CameraKeyframe`（阶段 3 实现采样）；
    - `PhysicsInstance::OwnsSimulationStep()` + Scene 固定步循环跳过逻辑；
    - 新增 `TestInterfaceCompilation`。
    - 自动验收：`wisteria_tests.exe` **74/74 PASS，FAIL=0**；
    - 手动验收：无（纯接口阶段）。
  - **阶段 1：已完成（2026-08-04）**
    - `SabaMmdImporter`：PMX 解析（骨骼/物理/morph/材质/网格/纹理）已实现；
      纹理路径使用 UTF-8 安全转换（MSVC ACP 坑已处理）。
    - 新增 `TestSabaMmdImporterWhenAvailable`：叶瞬光 Saba vs Assimp 对照。
    - 对照结果：rigidBodies=495、joints=568、materials=24、morphs=57、
      meshes=24 完全一致；骨骼 604 vs 605，差异为 Assimp 合成的根骨
      "Ye Shunguang"（Saba 604 为 PMX 原始骨骼数）。
    - 小资产 `pmx_physics.pmx`：3 刚体 / 6 关节通过。
    - 自动验收：`wisteria_tests.exe` **75/75 PASS，FAIL=0**；
    - 手动验收：数据层无窗口，等待阶段 2 接入渲染。
  - **阶段 2：已完成（2026-08-04）**
    - `Mesh::UploadDynamicVertices`：按 layout 偏移用 `glBufferSubData`
      更新 position/normal（未 attach 时只标记来源）。
    - `Renderer::UploadSkinning`：动态顶点源上传单位矩阵，避免 GPU 双蒙皮。
    - `SabaMmdRuntimeModel`：`saba::PMXModel` 驱动动画/IK/morph/CPU 蒙皮
      （BDEF/SDEF/QDEF），每帧上传到 `Mesh`；Saba 内部物理随
      `UpdateAllAnimation` 自步进（120Hz/-98 独立 world）。
    - `SabaMmdImporter` 补 `requiredBoneCount`。
    - **索引 winding 修复**：Saba 镜像 Z 坐标系后会把每个面索引反转
      （v2,v1,v0），importer 此前用原始顺序导致背面剔除丢弃全部三角形；
      已对齐（bind/index 对照 mismatches=0）。
    - demo：第二窗口改为 `MMD SABA MESH`（Saba 导入 + Saba 蒙皮渲染，
      灰模无纹理绑定）。
    - 新增 `TestMeshDynamicUpload`、`TestSabaSkinningWhenAvailable`。
    - 自动验收：`wisteria_tests.exe` **77/77 PASS，FAIL=0**；
    - 手动验收：双窗口 `LEGACY` vs `SABA MESH`，观察裙摆/头发柔软度
      （Saba 窗口为灰模，纹理绑定留后续）。
  - **阶段 2 鲁棒性固化（2026-08-04）**
    - `Mesh::RebuildInterleavedVertices` 纯函数 + 交错布局单测；
    - 多模型对照测试：叶瞬光/今汐/凑企鹅/爱弥斯/今汐皮肤，Saba 全部导入
      成功，3 个与 Assimp 对照一致，2 个记录 Assimp bind-space 限制；
    - 鲁棒性清单文档：
      `docs/architecture/MMD_PHYSICS_ROBUSTNESS.md`；
    - 自动验收：**78/78 PASS，FAIL=0**。
  - **性能优化（2026-08-04）**
    - 顶点子集化：上传量降 ~30 倍，帧率 49 → 65fps；
    - 性能路线图：`docs/architecture/MMD_PERFORMANCE_ROADMAP.md`。
  - **阶段 4：已完成（2026-08-04）**
    - `SabaPhysicsSettings`：fixedTimeStep/maxSubSteps/gravity 可配置并应用到
      `saba::MMDPhysics`；
    - 自步进契约：`SabaOwnedPhysicsInstance`（`OwnsSimulationStep=true`），
      Scene 跳过其共享 StepFixed 生命周期；
    - demo 对照开关：`WISTERIA_SABA_PHYSICS_FPS`、`WISTERIA_SABA_PHYSICS_MAXSTEPS`；
    - 新增 `TestSceneOwnsSimulationStep`、`TestSabaMmdPhysicsLongRunWhenAvailable`；
    - `SetPhysicsSettings` 可配置接口（构造参数或 `Initialize()` 前调用）；
    - 自动验收：**80/80 PASS，FAIL=0**；
    - 手动验收：120Hz vs 60Hz 对照。结论：两者都流畅，60Hz 帧率更高，
      物理手感可接受；已做成可设置接口，不再写死。
  - **阶段 5：整体切换完成（2026-08-04）**
    - demo 改为单窗口默认 `SabaMmdRuntimeModel`（`--alternate-model` 切换
      皮肤，`--morph-lab` 保留 Morph 诊断场景）；
    - 删除旧 MMD 物理运行层：
      `mmd/physics/{mmd_physics_instance,mmd_physics_policy,mmd_physics_modes,
      mmd_physics_diagnostics}` 与 `mmd/physics_compat/*`；
    - Entity/Scene 移除 `SetMmdPhysics`/`TryGetMmdPhysics` 等旧类型化接口，
      `ModelInstantiationOptions` 删除（旧“enablePhysics”语义随旧实现消失）；
    - 保留资源层：`MmdPhysicsAsset` / `MmdPhysicsTypes`（Saba importer 与
      ModelAsset 仍使用）；
    - 保留 Bullet 基础封装测试（PhysicsWorld/约束/调试绘制）与 Saba 链路测试；
    - 自动验收：**57/57 PASS，FAIL=0**（删除 23 个旧物理运行层测试）；
    - 手动验收：单窗口 Saba demo 流畅运行、无 `[ERROR]`；
      用户确认旧实现不再保留。
  - **接口扩展：动作/相机/灯光（2026-08-04）**
    - 用户决策：不做播放列表/编排系统，只抽象薄接口；
    - `MmdRuntimeModel` 新增单动作控制接口（Load/HasMotion、Loop、Pause/
      Resume、Restart、MotionFrame/MaxFrame），`SabaMmdRuntimeModel` 承接
      `saba::VMDAnimation`；
    - 相机：`CameraTrack::Sample` 实现（贝塞尔 + 越界钳制）；
      `LoadCameraMotion/ApplyCameraMotion` 承接 `saba::VMDCameraAnimation`；
    - 灯光：新增 `LightKeyframe/LightTrack` 数据层；
      `LoadLightMotion/ApplyLightMotion` 承接 `VMDFile::m_lights`；
    - 自动验收：**59/59 PASS，FAIL=0**；
    - 手动验收：接口层已由测试覆盖，demo 暂未接入相机/灯光（后续可选）。
