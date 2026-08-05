# R1.1A — ModelFrameSnapshot 契约设计（修订版）

> 状态：**R1.1B–R1.1F 已实现（2026-08-06）**。本文件经两轮评审修订：第一轮纠正了
> RenderProxy 假设与几何已解耦事实；第二轮（本版）纠正三个所有权
> 问题：Camera/Light 是 Scene 级轨道而非模型实例状态、Morph 不能伪造
> Saba 权威有效权重、View/Snapshot 生命周期必须彻底分离。

## 1. 现状对照表

| 通道 | 当前能力 | 当前所有者 | 当前生命周期 | R1.1 工作 |
| ---- | -------- | ---------- | ------------ | -------- |
| Geometry | `ModelVertexFrame`（span 视图） | Saba 内部缓冲 | 下一次更新前有效 | WISTERIA 持久副本（显式捕获） |
| Pose | WISTERIA `Pose` | Runtime 持有 `unique_ptr<Pose>` | Runtime 生命周期 | 纳入帧契约（矩阵捕获） |
| Morph | 单项 `SetMorphWeight` / `MorphWeight`（raw） | Saba | Runtime 生命周期 | 枚举 + raw 快照（effective 可选） |
| Camera | `ApplyCameraMotion(Camera&)` | 直接改宿主 | 立即生效 | 数据 `CameraTrackSample`（Scene 级） |
| Light | `ApplyLightMotion(DirectionalLight&)` | 直接改宿主 | 立即生效 | 数据 `LightTrackSample`（Scene 级） |
| Physics | settings + 空标记实例 | Saba/Bullet | Runtime 生命周期 | 能力/配置信息，非帧状态 |
| Animation | motion frame/control | Runtime | Runtime 生命周期 | metadata / revision |

### 关键现状事实（已核实）

- `ModelVertexFrame` 的 span 指向 `saba::PMXModel` 内部缓冲；立即读立即传
  安全，跨帧保存不安全。当前上传由 `ModelInstance::UploadDynamicVertices`
  完成（立即模式）。
- `RuntimeModelBase::UploadDynamicVertices(Mesh&)` 与
  `ModelInstance::UploadDynamicVertices(Mesh&)` 两套近似逻辑并存，
  R1.1E 统一到 ModelInstance 并让旧入口退场。
- `MmdRuntimeModel` 接口仍要求后端直接认识 WISTERIA `Camera` /
  `DirectionalLight`。这是 R1.1 必须切断的宿主依赖。
- `Pose` 已有 `LocalMatrices / GlobalMatrices / SkinningMatrices` 与
  `Revision()`，类型已中立，但归 Runtime 所有。
- **backend-driven PMX 没有 Entity 侧 MorphState**：`Scene::InstantiateModel`
  仅当 `backendDriven == false` 才调用 `entity.SetMorphSet(...)`。Saba 的
  `MMDMorphManager` 只有按名称的 `GetWeight()`（raw weight）。
  `MorphState::EffectiveWeights()` 是 WISTERIA 独立算法，**不能**冒充
  Saba 的权威求值结果。
- `CameraTrack` / `LightTrack` 已有 `Sample(float, Keyframe&)`，输出是
  MMD 语义 keyframe。Saba 的 light 轨道已以 WISTERIA `LightTrack` 存在
  （`impl->lightTrack`，`m_position.z` 取反）；camera 通过
  `saba::MMDLookAtCamera` 换算成 `CameraParam`。

## 2. 设计目标

> 把现有瞬时 Runtime 输出整理为 WISTERIA 拥有的帧状态，并移除
> Camera/Light 的最后两类宿主对象依赖。

非目标（本轮不做）：

- 不引入 `RenderProxy` 抽象；
- 不重做 CPU Skinning 或骨骼同步；
- 不扩展高级 Bullet 物理参数；
- 不实现确定性 seek/replay（R1.2）；
- 不改变画面与运行行为；
- 不在 R1.1B 开放 Morph 枚举到 C ABI（等 R1.S 建立 ABI 安全后再开放）。

## 3. 两类契约（所有权分离）

**模型实例帧状态**（归 `ModelInstance`）：

```cpp
struct ModelFrameSnapshot
{
    ModelFrameMetadata metadata;
    PoseSnapshot pose;
    MorphSnapshot morphs;
    DeformedVertexSnapshot geometry;   // 显式捕获，非每帧
};
```

**场景级动画轨道**（归未来 `SceneAnimation / MmdSceneMotion`，R1.1B
暂从 Saba 采样作为过渡入口）：

```cpp
struct SceneTrackSample
{
    std::optional<CameraTrackSample> camera;
    std::optional<LightTrackSample> light;
};
```

所有权规则：

- 模型实例不拥有场景相机/灯光轨道；多个人物共用一个 Camera VMD 时
  不应复制状态；
- 没有加载 PMX、只播放 Camera VMD 的场景由 Scene 轨道承载；
- 契约明确 `SceneTrackSample` 最终归 Scene 动画控制器，R1.1B 的
  Saba 采样只是过渡。

## 4. 两层数据形态

### 4.1 瞬时视图（零拷贝，仅更新周期内有效）

```cpp
struct ModelFrameView
{
    ModelVertexFrame geometry;   // 现有结构复用，span 指向 backend
    const Pose* pose = nullptr;
    std::uint64_t updateSerial = 0U;
};

// 有效期：ProduceFrameView 返回后，到 Runtime 下一次 Update/Reset/销毁。
```

用途：实时渲染链内部传递，高性能、不复制。**不直接暴露给长期持有的
C API。**

### 4.2 持久快照（WISTERIA 拥有，可查询/导出/哈希）

```cpp
// 有效期：CaptureSnapshot 完成后，到下一次 CaptureSnapshot 覆盖，
// 或 ModelInstance 被销毁。
```

C 门面安全规则（与 R1.S 联动）：

- 不长期返回内部裸指针；
- 采用「第一次查询数量 → 第二次复制到调用方缓冲区」；
- 或创建不可变 Snapshot Handle；
- 任何可能重新分配 Snapshot vector 的调用会使旧指针失效，必须文档化。

## 5. 逐通道契约

### 5.1 DeformedVertexSnapshot（原 GeometrySnapshot）

```cpp
struct DeformedVertexSnapshot
{
    std::vector<glm::vec3> positions;   // canonical/source 顶点顺序
    std::vector<glm::vec3> normals;
    std::uint64_t sourceRevision = 0U;
    bool captured = false;
};
```

规则：

- **顶点顺序**：使用 Runtime 的 canonical/source vertex order（PMX 原始
  顶点顺序）。Mesh topology、indices、UV 和 `SourceVertexIndices` 来自
  `ModelAsset`，不在快照内复制；
- 改名 `DeformedVertexSnapshot` 避免误认为包含完整几何拓扑；
- 未来需要与 GPU 最终绘制完全一致的导出时，再提供
  `RenderedMeshSnapshot` 或按 RenderPart 捕获；
- **不设置隐式拷贝阈值**：`CaptureSnapshot(CaptureMask)` 显式指定
  是否捕获 Geometry；调用方显式要求就不应因隐藏阈值少数据；
- 实时渲染保持 `ModelVertexFrame` 零拷贝路径，持久副本仅显式捕获。

### 5.2 PoseSnapshot 与 SkeletonSnapshot 分离

```cpp
// 资产级，只生成一次（不可变 Skeleton 元数据）
struct BoneDescriptor
{
    std::string name;
    int32_t parentIndex = -1;
    glm::mat4 bindLocalTransform{1.0f};
};

struct SkeletonSnapshot
{
    std::vector<BoneDescriptor> bones;
};

// 逐帧状态，只存矩阵，不重复字符串
struct PoseSnapshot
{
    std::vector<glm::mat4> localTransforms;
    std::vector<glm::mat4> globalTransforms;
    std::vector<glm::mat4> skinningTransforms;
    std::uint64_t poseRevision = 0U;
    bool captured = false;
};
```

命名修正：`Pose::GlobalMatrices()` 是骨架模型空间全局矩阵，不是 Scene
世界空间。字段名为 `globalTransforms`（或
`modelSpaceGlobalTransform`），真正的世界骨骼矩阵需再乘 Entity
Transform，不在本快照内。

### 5.3 MorphSnapshot（不伪造 effectiveWeight）

```cpp
struct MorphEntrySnapshot
{
    std::string name;
    MorphKind kind = MorphKind::Vertex;
    float rawWeight = 0.0f;                  // 该 Morph 自身被设置的权重
    std::optional<float> effectiveWeight;    // 仅后端能提供权威结果时才有值
};

struct MorphSnapshot
{
    std::vector<MorphEntrySnapshot> entries;
    std::uint64_t morphRevision = 0U;
    bool captured = false;
};
```

规则：

- `rawWeight`：该 Morph 自身权重；Group Morph 也记录 Group 自己的权重；
- `effectiveWeight`：**只有后端能给出权威求值结果时才提供**；R1.1
  第一版允许 `nullopt`；
- 不能由 ModelInstance 用 WISTERIA 独立 `ExpandWeights()` 冒充 Saba
  最终状态（backend-driven PMX 无 Entity MorphState，Saba 只有 raw
  weight）；
- Driver 提供中立枚举/读取接口（见 §7），而非复用 Entity MorphState。

### 5.4 Camera / Light Sample（Scene 级）

```cpp
struct CameraTrackSample
{
    float frame = 0.0f;
    glm::vec3 interest{0.0f};
    glm::vec3 rotation{0.0f};   // MMD 欧拉，单位见下
    float distance = 0.0f;
    float viewAngle = 0.0f;
    bool perspective = true;    // 仅查询/导出；当前应用层不支持正交
};

struct LightTrackSample
{
    float frame = 0.0f;
    glm::vec3 color{1.0f};
    glm::vec3 position{0.5f, 1.0f, 0.5f};
};
```

规则：

- **去掉 `available`**：外层 `std::optional` 已表达缺失，不重复；
- **坐标约定**：使用 WISTERIA MMD coordinate convention。具体写出：
  - Camera：interest/rotation/distance/viewAngle 为 MMD 语义；
    rotation 单位度，viewAngle 单位度；camera 位置由应用层
    look-at 换算得到；
  - Light：`color` 已 clamp 到 [0,1]；`position` 已应用
    `z = -VMD.z` 的 WISTERIA 约定（`LoadLightMotion` 现状），
    不再是原始 VMD 坐标；
  - **perspective 字段**：当前 `CameraParam` 只有透视 FOV，无正交
    投影模式；Saba `ApplyCameraMotion` 也未应用该字段。契约声明
    「perspective 保留用于查询/导出；正交 MMD Camera 当前
    unsupported」。
- 采样结果**不携带插值类型**：采样值已是插值后结果；原始轨道导出
  未来走 `CameraTrackAsset / CameraTrack::Keys()`。

## 6. 元数据与 revision（三种概念分离）

```cpp
struct ModelFrameMetadata
{
    std::uint64_t updateSerial = 0U;   // 每次 Update 调用都增加
    std::uint64_t stateRevision = 0U;  // 可观察状态变化才增加
    double motionFrame = 0.0;
    bool motionPaused = false;
    bool motionLooping = false;
    bool valid = false;
};
```

通道级 revision：

```text
geometryRevision ← ModelVertexFrame::revision
poseRevision     ← Pose::Revision()
morphRevision    ← 后端 Morph 状态 revision（Saba 侧新增）
cameraRevision   ← Track revision + sample frame
lightRevision    ← Track revision + sample frame
```

关键修正：**不用 `frame` 字段**——它与 `motionFrame` 含义重叠且 MMD
支持小数帧；`updateSerial` 与 `stateRevision` 分离，暂停状态下
updateSerial 增加但 stateRevision 不变，按需复制与确定性 hash 才有意义。

## 7. 接口归属

**Camera/Light 采样不进 `IModelRuntimeDriver` 基础接口**（普通静态模型
不应被迫实现空函数）：

短期（过渡）放 `MmdRuntimeModel`：

```cpp
virtual std::optional<CameraTrackSample>
    SampleCameraMotion(float frame) const = 0;
virtual std::optional<LightTrackSample>
    SampleLightMotion(float frame) const = 0;
```

更好的能力接口（未来）：

```cpp
class ICameraTrackProvider
{
    virtual std::optional<CameraTrackSample>
        SampleCamera(float time) const = 0;
};

class ILightTrackProvider
{
    virtual std::optional<LightTrackSample>
        SampleLight(float time) const = 0;
};
```

Runtime 通过 capability 查询暴露：`TryGetCameraTrackProvider()` /
`TryGetLightTrackProvider()`。最终迁移到独立 Scene Motion 对象。

Morph 中立枚举接口（Driver 侧）：

```cpp
virtual std::size_t MorphCount() const noexcept = 0;
virtual bool DescribeMorph(
    std::size_t index,
    MorphDescriptor& output
) const = 0;
virtual bool ReadMorphState(
    std::size_t index,
    MorphRuntimeState& output
) const = 0;
```

或一次性写入：

```cpp
virtual void CaptureMorphState(MorphSnapshotWriter& writer) const = 0;
```

## 8. Camera 转换归属（MMD 语义，非通用 Scene）

`MMDLookAtCamera` 换算属于 MMD 语义，放 `src/mmd/`：

```cpp
// include/wisteria/mmd/mmd_camera_conversion.hpp
CameraParam ToCameraParam(
    const CameraTrackSample& sample,
    const CameraParam& fallback      // 保留 NearClip/FarClip 等宿主设置
);
DirectionalLightData ToLightData(const LightTrackSample& sample);
```

黄金回归测试：新 WISTERIA 转换输出 == 旧 Saba `MMDLookAtCamera` 输出。
覆盖：frame 0、中间插值帧、大旋转、负 distance、极端仰角、多组 FOV。

## 9. Physics 能力信息（非帧状态）

从 `ModelFrameSnapshot` 移出——这些是能力/配置，不是每帧状态：

```cpp
struct ModelRuntimeCapabilities
{
    PhysicsBackendCapabilities physics;
};

struct ModelPhysicsRuntimeInfo
{
    bool enabled = false;
    MmdPhysicsRuntimeSettings settings{};
};
```

等 R1.2 真正有刚体 Transform、速度、关节误差时，再把动态
`PhysicsSnapshot` 放入帧快照。本轮只暴露真实可控面（timestep/substeps/
gravity/enabled）+ 可用性。

## 10. 生命周期与时序（修正后）

```text
Scene::Update()
  → ModelInstance::Update(dt)
      → runtime->Update(dt)
      → runtime->ProduceFrameView()   // 瞬时视图（零拷贝）
      → ModelInstance 更新实例 Mesh
      → metadata.updateSerial++
      → 可观察状态变化则 stateRevision++

Scene 渲染
  → Renderer 读取 RenderPart / 实例 Mesh（视图已消费）

C ABI 查询/导出
  → ModelInstance::CaptureSnapshot(mask)  // 显式生成持久副本
```

生命周期规则：

```text
ModelFrameView
  有效期：ProduceFrameView 返回后 → Runtime 下一次 Update/Reset/销毁

ModelFrameSnapshot
  有效期：CaptureSnapshot 完成后 → 下一次 CaptureSnapshot 覆盖，
  或 ModelInstance 销毁
```

## 11. 兼容迁移接口（R1.1B–R1.1F 顺序）

| 补丁 | 内容 | 验收 |
| ---- | ---- | ---- |
| R1.1B | Light 数据化：`SampleLightMotion` 替换宿主写入；WISTERIA MMD 应用层负责转换/写 `DirectionalLight` | 单实例画面一致；无 `DirectionalLight&` 宿主写入残留 |
| R1.1C | Camera 数据化：建 `src/mmd/mmd_camera_conversion`，旧 Saba 输出做黄金对照，再替换宿主写入 | 黄金对照通过；无 `Camera&` 宿主写入残留；正交 Camera 明确 unsupported |
| R1.1D | Model Frame State：`ModelFrameView` + `ModelFrameSnapshot` + Pose/Geometry 显式捕获 + raw Morph 捕获 + channel revision | 双实例缓冲独立；两实例不同帧互不覆盖；快照可查询/哈希 |
| R1.1E | 清理重复上传适配器：`RuntimeModelBase::UploadDynamicVertices(Mesh&)` 弃用；修正 Saba 头文件过期注释 | 上传统一走 ModelInstance |
| R1.1F | 运行时能力信息：`ModelRuntimeCapabilities` + `ModelPhysicsRuntimeInfo` | 前端可查真实能力；unavailable 明确标记 |

每补丁保持：不改画面、小步、双平台 CTest 通过、单实例画面一致。

## 12. 回答十个设计问题（修订后）

1. `ModelVertexFrame` 与 Snapshot：**并存**，视图/持久两层；
2. Snapshot 内存所有者：**ModelInstance**（帧状态）；Camera/Light 轨道
   归 **Scene 级对象**；
3. 顶点是否每帧复制：**否**，`CaptureSnapshot(mask)` 显式按需捕获；
4. Pose 复制还是视图：**快照复制矩阵，渲染走视图**；骨骼元数据
   （名称/父索引/bind）在 SkeletonSnapshot 只生成一次；
5. Camera/Light 坐标系：**WISTERIA MMD convention**（写出轴向/单位，
   light 的 z 取反；perspective 当前 unsupported）；
6. Morph 枚举与 Group：**枚举复用 ModelAsset 定义 + Saba raw weight**；
   effectiveWeight 仅后端权威可提供，不伪造；
7. revision：**updateSerial / stateRevision / 通道 revision 三分离**；
8. 时效：**View 与 Snapshot 各自独立有效期**（见 §10）；
9. 旧上传入口：**R1.1E 弃用并统一到 ModelInstance**；
10. 本轮不处理物理：**高级 Bullet 参数全部排除**，能力信息非帧状态。

## 13. 未决问题（实现前确认）

- Morph 枚举接口选 `DescribeMorph + ReadMorphState` 还是
  `CaptureMorphState(writer)`；
- Camera Track 的未来 Scene Motion 所有权：R1.1B 过渡期从 Saba 采样，
  何时、以什么形式迁入 `SceneAnimation`；
- `CaptureSnapshot` 的 C ABI 暴露时机（与 R1.S generation handle 联动）。

## 14. 实现记录（R1.1B–R1.1F，2026-08-06）

### R1.1B Light 数据化

- `MmdRuntimeModel` 新增 `SampleLightMotion(float) const` 返回
  `std::optional<LightTrackSample>`；删除 `ApplyLightMotion` /
  `ApplyLightTrack` 宿主写入；
- 新增 `src/mmd/mmd_light_conversion.cpp`：`ToLightData(LightTrackSample)`
  → `DirectionalLightData`（color clamp + position→direction，复现旧逻辑）；
- Saba `impl->lightTrack` 本就是 WISTERIA `LightTrack`，迁移成本最低；
- demo/integration 测试改走「采样 + 应用」两步。

### R1.1C Camera 数据化

- `MmdRuntimeModel` 新增 `SampleCameraMotion(float) const` 返回
  `std::optional<CameraTrackSample>`；删除 `ApplyCameraMotion` /
  `ApplyCameraTrack`；
- 新增 `src/mmd/mmd_camera_conversion.cpp`：`ToCameraParam(sample, fallback)`
  复现 Saba `MMDLookAtCamera` 换算（translate |distance| + Y/-Z/X 旋转序）；
- 黄金回归测试 `TestMmdCameraConversionMatchesSaba`：5 组用例（frame 0、
  中间插值、大旋转、负 distance、极端仰角）与 Saba 输出完全一致；
- demo 相机轨道改走 `SampleCameraMotion` + `ToCameraParam`。

### R1.1D Model Frame State

- 新增 `include/wisteria/runtime/frame_snapshot.hpp`：`ModelFrameSnapshot`
  （pose/morphs/geometry）+ 各子结构 + `CaptureMask`；
- `IModelRuntimeDriver` 新增 `ProduceFrameView()`（瞬时零拷贝视图）；
- `ModelInstance` 持有帧状态：`LastFrameView()` / `CaptureSnapshot(mask)` /
  `LastSnapshot()`；Geometry 仅显式捕获，不设隐式阈值；
- Morph 经中立枚举接口（`MorphCount` / `DescribeMorph` / `ReadMorphState` /
  `MorphRevision`）读取 raw weight；effectiveWeight 恒 nullopt（Saba 不提供
  权威求值，不伪造）；kind 从 ModelAsset MorphSet 按名称解析；
- `SetMorphWeight` 递增 morphRevision。

### R1.1E 清理旧上传适配器

- 删除 `RuntimeModelBase::UploadDynamicVertices(Mesh&)`；
- 动态几何上传唯一入口为 `ModelInstance::UploadDynamicVertices`；
- Saba 底层蒙皮测试改为显式验证 vertex frame 可上传（统一逻辑）。

### R1.1F 能力信息

- `frame_snapshot.hpp` 新增 `PhysicsBackendCapabilities` /
  `ModelRuntimeCapabilities` / `ModelPhysicsRuntimeInfo`（非帧状态）；
- `IModelRuntimeDriver` 新增 `Capabilities()` / `PhysicsInfo()`；
- Saba 如实上报：fixedTimeStep/maxSubSteps/gravity/enabled/reset 支持，
  solver tuning/CCD/snapshot 等高级能力恒 false（R1.2 前不扩）。

### 验收结果（双平台，RelWithDebInfo）

| 平台 | CORE | FULL_ASSETS |
| ---- | ---- | ----------- |
| Windows（MSVC） | 4/4 | 4/4 |
| Linux（WSL + llvmpipe） | 4/4 | 4/4 |

新增/更新测试：`MMD camera conversion golden regression`、
`R1 engine-owned MMD instances`（帧状态 + 能力断言）、
`Saba motion/camera/light interface`（采样路径）。

### R1.1 Fixup（2026-08-06，评审后）

修复 4 个 P0 与次要项：

- **P0-1 循环依赖**：`frame_snapshot.hpp` 删除对 `runtime_model_base.hpp`
  的 include（本不需要），公共头可独立包含；
- **P0-2 Morph revision 冻结**：VMD 每帧直接写 Saba 内部 morph 权重，
  `Update()` 后递增 `morphRevision`；`LoadMotion` / `ClearMotion` /
  `RestartMotion` / `SetMotionFrame` 也递增；
- **P0-3 stateRevision**：`CapturePose/Morphs/Geometry` 返回是否更新，
  `stateRevision` 成为 ModelInstance 自己的单调序列，不再比较独立通道
  计数器大小；
- **P0-4 Light 强度保留**：`ToLightData(sample, fallback)` 带 fallback，
  只改 Color/Direction，保留宿主 Intensity；
- **metadata 填充**：`Update()` 同步 motionFrame/paused/looping；
  `valid = updateSerial > 0`，Reset 后失效；
- **Reset 失效**：`Reset()` 清空 `lastView` 并使 snapshot 失效；
- **perspective 不伪造**：`CameraTrackSample::perspective` 改
  `std::optional<bool>`，Saba 返回 nullopt（VMDCameraAnimation 丢弃该
  标志）；
- **FrameView 权威来源**：`ModelInstance::UploadDynamicVertices` 与
  `CaptureGeometry` 都读 `lastView.geometry`（同一 updateSerial 的数据）；
- **Morph/播放接口移入 `IModelRuntimeDriver`** 可选默认接口，
  `ModelInstance` 不再 dynamic_cast MMD 类型；
- **新增测试**：`Saba light VMD sampling`（构造 VMD light 帧）、
  `Morph revision advances on VMD update`、`stateRevision` 单调 +
  Reset 失效断言、`ToLightData` 强度保留断言。

修复后双平台（Windows/Linux，RelWithDebInfo）CORE + FULL_ASSETS 均
4/4 通过。

### R1.1 Final Fix（2026-08-06，复审后）

- **Reset 后 Capture 重新标为有效（真实 Bug）**：原实现用历史
  `updateSerial > 0` 判断快照有效，Reset 后再次 Capture 会重新变有效，
  且空 `lastView.geometry`（revision=0）会触发重写空几何。新增独立
  `frameValid` 标志：构造 false、Update 成功 true、Reset false；
  `snapshot.metadata.valid = frameValid`；`frameValid == false` 时
  Capture 直接返回无效快照、不捕获任何通道。
- **Camera fallback 显式化**：`ToCameraParam` / `ToLightData` 删除默认
  fallback 参数，调用方必须显式选择（demo 传当前宿主参数保留
  NearClip/FarClip/Intensity；测试传默认值）。
- **Morph 快照内容测试加强**：VMD 带两个 morph 关键帧（frame 0 w=0.0、
  frame 10 w=1.0），经 ModelInstance 捕获验证 rawWeight 实际变化
  （frame0≈0.05 → frame10=1.0），不仅是计数器递增。
- **Reset→Capture→Update 边界测试**：Reset 后 Capture 仍无效，再次
  Update 后恢复有效。

测试中发现并修正：VMD 默认循环导致 `SetMotionFrame(10)` 后 Update 被
`fmod` 卷回（测试需 `SetMotionLooping(false)`）；VMD light frame 是
uint32 非 float（测试构造修正）。

修复后双平台（Windows/Linux，RelWithDebInfo）CORE + FULL_ASSETS 均
4/4 通过。R1.1 至此可冻结。

### R1.1 Closeout（2026-08-06，R1.S 前收尾）

- **`stateRevision` 改名 `snapshotRevision`**：语义澄清为「持久快照内容
  版本」——仅在 `CaptureSnapshot` 重写请求通道时递增，不是模型可观察
  状态的全局 revision。尚未进入 C ABI，改名成本为零；
- **Reset 失效各通道 captured**：Reset 时把 pose/morphs/geometry 的
  `captured` 全部置 false，防止未来 Runtime 在 Reset 后从相同 revision
  重启时错误复用 Reset 前数据（vector 保留，下一有效帧强制重捕获）。

R1.1 冻结。下一阶段：R1.S（C ABI Safety）。

### 未决问题（转入 R1.2 / R1.S）

- Camera/Light 轨道最终迁入 `SceneAnimation`（当前过渡期从 Saba 采样）；
- `CaptureSnapshot` 的 C ABI 暴露（与 R1.S generation handle 联动）；
- 高级 Bullet 物理参数与完整刚体/关节快照（R1.2）。
