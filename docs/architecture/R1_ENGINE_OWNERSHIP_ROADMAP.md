# R1 引擎所有权路线图（校正版）

> 本文件是 R1 阶段的权威路线图。它是与外部模型运行时（Saba MMD）讨论、
> 交叉复核后定稿的校正版。文中所有「已完成」均以
> `R1_0_CORRECTED_BASELINE.md` 的验收记录为准，不以接口名或某个环境下的
> 绿色测试为准。

## 1. 一句话定位

WISTERIA 的目标不是做 MMD 播放器，而是做一套通用、可扩展、可由前端完整
控制的实时模型渲染引擎。Saba MMD 是第一套接入的模型运行后端。

核心原则：

> 算法可以借助第三方实现，但对象所有权、生命周期、运行调度、渲染状态和
> 导出能力必须属于 WISTERIA。

## 2. 目标拓扑

```text
模型文件
→ WISTERIA ModelAsset（共享、不可变）
→ WISTERIA ModelInstance（每实例可变状态）
→ WISTERIA Entity（场景实体）
→ Scene 调度
→ Runtime Backend 求值（Saba / 未来 glTF / VRM ...）
→ WISTERIA RenderProxy
→ 阴影与主渲染
→ 查询、快照和导出
```

第三方后端只负责专业求值：

- PMX/VMD 解析与 MMD 语义（骨骼、IK、Append/Grant、Morph、刚体/关节、Bullet 物理、蒙皮）；
- 输出中立帧状态（R1.1 起）。

第三方后端不拥有：

- Scene、Entity、公共资源句柄、GPU 资源生命周期、最终相机/灯光对象、
  渲染流程、对外 C ABI、模型实例生命周期。

## 3. 后端分类（R1.0A 校正）

R1.0 补丁将 `ModelBackendKind` 分为两类，校正后应明确为四类：

| Kind | 含义 | Runtime Driver |
| ---- | ---- | -------------- |
| `Static` | 真正无逐帧状态的 OBJ/PBR 模型 | 无（合理设计） |
| `WisteriaGeneric` | 带骨骼/Morph/AnimationClip 的 GLB/Assimp 模型 | 未来由 WISTERIA 通用 Animator 驱动（R1.5） |
| `SabaMmd` | PMX/VMD 专用 | `SabaMmdRuntimeModel` |
| 未来 `Gltf/Vrm` | 专用或复用 `WisteriaGeneric` | R1.5 之后 |

现状（R1.0 真实状态）：所有模型都有 `ModelInstance`，但只有 `SabaMmd`
的动态状态进入 Runtime Driver；`Static`/通用模型的骨骼动画与 Morph 仍由
Entity 旧路径（`SetMorphSet` / `SetSkeleton` / `Animator`）直接管理。
`Scene::InstantiateModel()` 的双分支（`backendDriven ? ... : ...`）是
明确的架构债务，列入 R1.5 处理。

## 4. R1.0 校正定位

> R1.0 完成了 Saba MMD 实例所有权、Scene 调度权和实例级渲染数据的
> 第一轮收口；尚未完成帧状态、ABI 安全、资产来源和全部动态模型路径的
> 统一。

已完成并验证（见校正基线）：

- 所有模型实例化时都有 `ModelInstance`；
- Saba PMX Runtime 由 WISTERIA 创建和销毁；
- Entity 持有 `ModelInstance`，Scene 调度 MMD Runtime；
- MMD 动态 Mesh 是实例级数据（`Mesh::CloneForInstance`）；
- 同资产多实例的动画、Pose、Morph、顶点和物理所有者独立；
- Renderer 不依赖 Saba 类型；
- Demo 不再承担手工桥接职责；
- Entity 级 C ABI 开始控制正在渲染的 MMD 实例（v0.7）。

未完成（R1.1 起处理）：

- Saba 仍直接接触部分 `Mesh/Camera/Light` 宿主对象；
- 完整帧状态未通过中立 Snapshot 交付；
- 大部分 C ABI 尚无统一异常边界（见 `C_ABI_SAFETY_MATRIX.md`）；
- 完整物理状态不可查询、不可导出；
- 通用骨骼动画和 Morph 仍有 Entity 旧路径；
- 真静态模型与通用动态模型未分类（`Static` 混合两类）；
- Saba Runtime 仍可能二次读取 PMX；
- 确定性 seek/replay 未建立；
- 完整资产测试曾被 SKIP 掩盖（R1.0A 已修复）。

## 5. 能力三层划分（不混写）

任何物理/渲染能力表述必须区分：

| 层级 | 含义 |
| ---- | ---- |
| 后端内部存在 | Saba/Bullet 内部具备，但 WISTERIA 未必能控制 |
| WISTERIA Runtime 可控 | 已进入通用 C++ Runtime 契约 |
| C ABI 可控/可导出 | 前端可以稳定设置、查询和复现 |

只有第三层能写进前端 SDK 能力表。R1.0 中物理能力真实暴露面：

- `MmdPhysicsRuntimeSettings`：`fixedTimeStep` / `maxSubSteps` / `gravity` / `enabled`；
- `wisteria_physics_reset`（只重置物理，不动动画帧）；
- `wisteria_physics_capabilities` 能力位。

Mode 0/1/2、CCD、margin、solver、恢复策略等属于自研 Bullet 路径或
Saba 内部行为，未进入通用 Runtime 契约，不得写入 SDK 能力表。

## 6. 阶段顺序

```text
R1.0A  基线校正（本阶段）
R1.1   ModelFrameSnapshot 边界（与 R1.S 并行）
R1.S   C ABI Safety 与 generation handle（提前，不等 R1.4）
R1.2   确定性时间线与物理导出
R1.3   资产所有权完全收口
R1.4   C ABI 门面收口
R1.5   第二套动态 Runtime 验证（WisteriaGeneric / ProceduralTest）
```

### R1.1 — Frame Snapshot 边界

契约先行：

```cpp
struct ModelFrameSnapshot
{
    PoseSnapshot pose;
    MorphSnapshot morphs;
    DeformedGeometrySnapshot geometry;
    PhysicsSnapshot physics;
    std::optional<CameraTrackSample> camera;
    std::optional<LightTrackSample> light;
    FrameMetadata metadata;
};
```

迁移目标：删除 Saba → Mesh/Camera/Light 直接修改，改为
「Saba 求值 → 输出 Snapshot → ModelInstance 接收 → WISTERIA 更新」。

### R1.S — ABI Safety（与 R1.1 并行）

- 所有 C 导出函数必须 `noexcept`，统一经 `GuardAbi` 或可证明不抛的
  内部 helper；
- generation handle；
- Window/Scene/Entity/Model/Light 明确父子生命周期，删除裸指针跨句柄关系；
- destroy-order、double-destroy、stale-handle 测试。

### R1.2 — 确定性时间线

`EvaluateFrame()`、seek policy（PRESERVE / RESET_AT_TARGET /
REPLAY_FROM_START / REPLAY_FROM_CHECKPOINT）、固定步重放、Physics
Snapshot/Restore、checkpoint、Pose/Physics/Vertex hash、重复运行一致性。

### R1.3 — 资产所有权

Saba Runtime 不再二次读取 PMX；`ModelAsset` 保存后端私有只读 payload；
Runtime 从 payload 创建；一个资产多个独立实例；Context 显式管理
asset/shader/cache roots。

### R1.4 — C ABI 门面收口

统一 ModelInstance/Entity 门面；完整骨骼/Morph/物理/材质/帧快照查询；
旧 Headless API deprecated 后删除。

### R1.5 — 第二套动态 Runtime 验证

方案 A：`WisteriaGenericRuntimeDriver`（承接 GLB/Assimp 骨骼/Morph/
AnimationClip）；方案 B：极小 `ProceduralTestRuntimeDriver`（验证
Backend Registry、Snapshot、多实例）。真正静态模型保持
「ModelInstance + 无 Runtime Driver」，不制造空后端。

## 7. R1 最终验收标准

- 所有权：ModelAsset/ModelInstance/Runtime/GPU/Scene 调度权归 WISTERIA；
- 后端隔离：通用层不依赖 Saba 类型（`src/rendering/` 已满足）；
- 多实例：动画/Morph/IK/Pose/Physics/动态顶点/材质覆盖/可见性/Transform/销毁独立；
- 可查询可导出：骨骼/Morph/动画/刚体/关节/顶点/材质/相机/灯光/图像；
- C ABI：无 C++ 类型泄漏、无异常越界、无悬空句柄、资源路径不依赖 CWD；
- 确定性：同输入 → 一致 Pose/Physics/Vertex/Image hash。

## 8. 节奏控制

接下来不立即：开发更多模型格式、堆 MMD 特例、扩张不完整 C API、重写
Saba 算法、一次性拆掉兼容接口。所有后续补丁必须基于 R1.0A 校正后的
最新基线，不能以原始 R1.0 补丁为基线。
