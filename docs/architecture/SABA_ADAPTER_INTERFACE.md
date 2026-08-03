# Saba 承接接口契约（v1）

## 目标

把 MMD 链路（解析、动画、物理、顶点蒙皮）逐步切换到 Saba，同时保留 WISTERIA
自己的渲染器、Scene、资源层。切换的关键不是“把 Saba 塞进现有类”，而是定义三层
接口：格式无关的底层抽象 → MMD 中间层抽象 → Saba 具体实现。

## 分层总览

```text
导入层（格式无关接口）
  ModelImporter ──────────→ ImportedModelData
    ├─ SabaMmdImporter（新，PMX/PMD/VMD/VPD 全走 Saba 解析）
    └─ AssimpMmdImporter（现有，保留作对照）

资源层（格式无关，保持扁平，基本冻结）
  ModelAsset / Mesh / Material / Skeleton / Bone / MorphSet / AnimationClip
    └─ 可选扩展：MmdSkinningExtension、CameraTrack

运行时层（新增“父类/接口”，本契约的核心）
  RuntimeModelBase
    ├─ MmdRuntimeModel
    │   ├─ SabaMmdRuntimeModel   ← 承接 Saba PMXModel/MMDPhysics/VMD
    │   └─ WisteriaMmdRuntimeModel（现有 compat，作对照/回退）
    └─ GltfRuntimeModel（未来）

Entity / Scene 只依赖 RuntimeModelBase + PhysicsInstance
```

## 1. 导入层接口

### `SabaMmdImporter`

```cpp
class SabaMmdImporter final : public ModelImporter
{
public:
    ImportedModelData Import(const std::filesystem::path& path) override;
};
```

职责：

- 内部使用 `saba::PMXFile` / `saba::PMDFile` 解析；
- 输出仍为 `ImportedModelData`（顶点/材质/骨骼/morph/物理），上层资源层不变；
- VMD/VPD 解析同样由 Saba 完成，但**只提取数据**，运行时仍由 WISTERIA 自己驱动，
  或由 `SabaMmdRuntimeModel` 驱动（见第 3 节）；
- 保留 `AssimpMmdImporter` 作为交叉验证对照。

## 2. 资源层扩展（三个接缝接口）

### 2.1 `Mesh` 动态顶点上传

Saba 的蒙皮（BDEF/SDEF/QDEF）是 CPU 计算，每帧产出 position/normal。Mesh 需要
新增动态更新能力，而不改变静态网格路径：

```cpp
class Mesh
{
public:
    // 每帧上传 Saba 计算好的顶点；只更新 position/normal，不重建 VBO。
    void UploadDynamicVertices(
        std::span<const glm::vec3> positions,
        std::span<const glm::vec3> normals
    );
    bool HasDynamicVertexSource() const noexcept;
};
```

SDEF/QDEF 的原始数据（S/C/R、双四元数）由 Saba 运行时持有，不进 Mesh；
Mesh 只接收“已经蒙皮好的顶点”。

### 2.2 `AnimationClip::CameraTrack`

```cpp
struct CameraKeyframe
{
    float time = 0.0f;
    glm::vec3 interest{0.0f};   // 注视点
    glm::vec3 rotation{0.0f};   // 欧拉角
    float distance = 0.0f;      // 距离
    float viewAngle = 0.0f;     // 视角
    bool perspective = true;
    std::array<KeyframeInterpolation, 4> interpolation{}; // x/y/z/dist
};

class CameraTrack
{
public:
    explicit CameraTrack(std::vector<CameraKeyframe> keys);
    bool Sample(float time, CameraKeyframe& output) const;
};
```

`AnimationClip` 增加 `std::vector<CameraTrack> cameraTracks`（一般只有一个，
但保留多轨能力）。

### 2.3 `PhysicsInstance` 自步进

```cpp
class PhysicsInstance
{
public:
    // 返回 true 表示实例自己驱动 stepSimulation（Saba 独立 world），
    // Scene 对该实例跳过共享 PhysicsWorld::StepFixed。
    virtual bool OwnsSimulationStep() const noexcept { return false; }
};
```

`Scene::Update` 的固定步循环修改为：

```text
对每个实体：
  若 physicsInstance == nullptr 或 !OwnsSimulationStep()：
      PrepareSimulationSubstep(...)
若存在自步进实例：跳过共享 StepFixed（共享 world 为空）
否则：physicsWorld->StepFixed(...)
```

## 3. 运行时层接口

### `RuntimeModelBase`

所有格式模型运行时的共同抽象：

```cpp
class RuntimeModelBase
{
public:
    virtual ~RuntimeModelBase() = default;

    // 从已导入资源构建运行时（加载动画 clip、创建物理等）。
    virtual bool Initialize() = 0;

    // 推进一帧：动画采样 + IK/append + 物理 + 顶点蒙皮。
    virtual void Update(float deltaTime) = 0;

    virtual void Reset() = 0;

    // 当前骨骼姿态（渲染器通过它取 skinning 矩阵）。
    virtual Pose& GetPose() = 0;

    // 是否需要每帧上传动态顶点。
    virtual bool NeedsDynamicVertexUpload() const noexcept = 0;

    // 把蒙皮结果写入 Mesh（若 NeedsDynamicVertexUpload()）。
    virtual void UploadDynamicVertices(Mesh& mesh) = 0;

    // 可选物理实例；Scene 通过 PhysicsInstance 驱动或跳过。
    virtual PhysicsInstance* TryGetPhysicsInstance() noexcept = 0;
};
```

### `MmdRuntimeModel`

MMD 中间层，暴露 MMD 特有语义：

```cpp
class MmdRuntimeModel : public RuntimeModelBase
{
public:
    // IK 开关（VMD IK 状态轨）。
    virtual void SetMmdIkEnabled(BoneIndex bone, bool enabled) = 0;

    // 相机轨道应用。
    virtual void ApplyCameraTrack(
        const CameraTrack& track,
        float time,
        Camera& camera
    ) = 0;

    // 顶点蒙皮类型（用于日志/调试）。
    virtual MmdSkinningKind SkinningKind() const noexcept = 0;

    // 物理实例（Saba 或 WISTERIA compat 均可）。
    virtual PhysicsInstance* GetMmdPhysics() noexcept = 0;
};
```

### `SabaMmdRuntimeModel`

```cpp
class SabaMmdRuntimeModel final : public MmdRuntimeModel
{
    // 内部持有：
    //   saba::PMXModel / saba::PMDModel
    //   saba::MMDPhysics（独立 Bullet world，120Hz，-98）
    //   saba::VMDAnimation / saba::VMDCameraAnimation
    //
    // 桥接：
    //   Saba 节点矩阵 → Pose（每帧同步）
    //   Saba CPU 蒙皮结果 → Mesh::UploadDynamicVertices
    //   Saba MMDPhysics → PhysicsInstance（OwnsSimulationStep() == true）
};
```

### `WisteriaMmdRuntimeModel`

现有 `MmdCompatPhysicsInstance` + `MmdCompatRuntime` 的包装，作为对照/回退实现。

## 4. 数据流

```text
PMX/PMD/VMD/VPD
  → SabaMmdImporter（或 AssimpMmdImporter 对照）
  → ImportedModelData → ModelAsset（资源层不变）
  → SabaMmdRuntimeModel（或 WisteriaMmdRuntimeModel）
      → 动画采样 → IK/append → 物理 → 蒙皮
  → Pose（skinning 矩阵）+ Mesh::UploadDynamicVertices
  → Renderer（现有）
```

## 5. 接口冻结规则

- 本契约 v1 只定义接口，不实现；
- 阶段实现中发现接口不合理，允许修订，但必须先更新本文档；
- `ImportedModelData`、`ModelAsset`、`Skeleton/Bone`、`MorphSet` 的资源层接口
  尽量不改；MMD 专属扩展走 optional 字段或新扩展对象。
