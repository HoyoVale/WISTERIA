# MMD 动作 / 相机 / 灯光接口设计

> 状态（2026-08-04）：**不做播放列表/编排系统**。下面第 1、2 节是调研记录，
> 第 3 节是最终落地的薄接口方案；第 4 节起的“播放列表 v1”设计仅作参考，
> 不实施。

## 0. 决策

- 动作管理：抽象 `MmdRuntimeModel` 上的单动作控制接口（加载/循环/暂停/恢复/
  重开/帧位置），由 `SabaMmdRuntimeModel` 承接 Saba 的 `VMDAnimation`。
- 相机动画：`CameraTrack` 采样 + `LoadCameraMotion/ApplyCameraMotion`，
  由 Saba 的 `VMDCameraAnimation` 承接。
- 灯光动画：新增 `LightTrack` 数据层 + `LoadLightMotion/ApplyLightMotion`，
  由 Saba 解析的 `VMDLight` 帧承接。
- 明确不做：动作队列、自动切歌、交叉淡化、分层混合。将来需要时再在接口
  之上加一层，不污染运行时。

## 1. 调研：旧实现与 Saba 原生能力

旧链路是“一个 VMD → 一个 `AnimationClip` → Animator 单 clip 循环”：

- `Scene::InstantiateModel` 对模型自动 `Play(model.AnimationClipAt(0))`；
- `Animator::looping` 默认 `true`，demo 里再显式 `SetLooping(true)`；
- 按 `R` 重启时 `animator.Play(clip, true)` + `ResetPhysicsToCurrentPose()`；
- 没有队列、没有切换、没有交叉淡化。

结论：用户观察到的“一个动作结束循环播放”是对的，而且这个循环是
**按时间取模**的硬循环，不做物理过渡。

## 2. Saba 原生能力

`saba::VMDAnimation` 提供三个关键接口：

| 接口 | 行为 |
|---|---|
| `Add(const VMDFile&)` | 可多次调用，把多个 VMD 的键**合并**进同一套 controller：按骨骼/IK/morph 名字归组、按键时间排序。 |
| `Evaluate(t, weight)` | 求值；`weight < 1` 时相对 base pose 混合（Saba 内部 `SyncPhysics` 用它做过渡）。 |
| `SyncPhysics(t, frameCount)` | 从初始姿势逐步过渡到目标帧：默认 30 步，边 Evaluate 边跑物理，避免“脚穿裙”类瞬间爆裂。 |
| `GetMaxKeyTime()` | 返回所有 controller 的最大关键帧时间（可当动作总时长）。 |

注意两点：

1. `Add` 是“合并时间轴”，不是“加权叠加层”。如果两个 VMD 动同一根骨骼，
   结果是同一根骨头的键按时间排进一个列表；如果两个 VMD 动的骨骼不相交
   （例如身体 VMD + 表情 VMD），合并后相当于天然分层。
2. 多次 `Evaluate(t, weight)` 不会累积混合——每次都以 base pose 为基准，
   后一次覆盖前一次。真正的“两段动作 50/50 融合”需要自己做采样/双模型。

Saba viewer 本身只做单 VMD 播放：按 `GetMaxKeyTime()` 取模循环，没有
播放列表概念。

## 3. 落地：薄接口（当前实现）

接口定义在 `include/wisteria/runtime/mmd_runtime_model.hpp`，实现全部在
`SabaMmdRuntimeModel`：

```cpp
// 动作（单 VMD，无播放列表）
bool LoadMotion(const std::filesystem::path& vmdPath);
bool HasMotion() const noexcept;
void SetMotionLooping(bool looping);
bool IsMotionLooping() const noexcept;
void PauseMotion();
void ResumeMotion();
bool IsMotionPaused() const noexcept;
void RestartMotion(bool resetPhysics = true);
double MotionFrame() const noexcept;
void SetMotionFrame(double frame);
double MotionMaxFrame() const noexcept;

// 相机
bool LoadCameraMotion(const std::filesystem::path& vmdPath);
void ApplyCameraMotion(float frame, Camera& camera);
void ApplyCameraTrack(const CameraTrack& track, float time, Camera& camera);

// 灯光
bool LoadLightMotion(const std::filesystem::path& vmdPath);
void ApplyLightMotion(float frame, DirectionalLight& light);
void ApplyLightTrack(const LightTrack& track, float time, DirectionalLight& light);
```

数据层：

- `CameraTrack`（已有）现在实现了 `Sample`：interest 用 4 条贝塞尔曲线
  （X/Y/Z + distance），rotation/viewAngle 线性，越界钳制到首/末键。
- 新增 `LightTrack`（`LightKeyframe`：time/color/position + 6 条插值曲线），
  与 `CameraTrack` 同构的 `Sample` 语义。

Saba 承接关系：

- `LoadMotion` → `saba::VMDAnimation::Create + Add`，替换当前动作；
- 循环/暂停/帧位置由 runtime 管理，`Update` 在循环模式下用
  `GetMaxKeyTime()` 取模；
- `LoadCameraMotion/ApplyCameraMotion` → `saba::VMDCameraAnimation`
  → `saba::MMDLookAtCamera` → `CameraParam`；
- `LoadLightMotion` → `VMDFile::m_lights` → `LightTrack`，
  `ApplyLightTrack` 把 color 写入 `DirectionalLight`，position 反向后
  归一化为方向（MMD 光源位置 → 光线方向）。

## 4. 自动验收（已实现）

- `TestCameraLightTrackSampling`：CameraTrack/LightTrack 中值插值、越界钳制、
  空轨道拒绝采样；
- `TestSabaMotionCameraLightInterfaceWhenAvailable`：真实 VMD 的加载/循环/
  暂停/恢复/重开/帧接口，真实相机 VMD（越南鼓卡点舞 镜头.vmd）的相机参数
  有限性，以及 LightTrack 应用到 DirectionalLight。

## 5. 手动验收

- demo 暂未接这些接口（保持默认单动作循环）；可以用测试资产验证：

```powershell
.\build\RelWithDebInfo\wisteria_tests.exe
```

- 后续把 `LoadCameraMotion/ApplyCameraMotion` 接到 demo 相机即可实现
  “VMD 镜头跟随”，不需要编排系统。

## 6. 将来若要做编排

在薄接口之上加 `MmdMotionPlaylist`（队列 + 模式 + 切换时 `SyncPhysics` 过渡），
不影响运行时接口；接口层保持现状即可。
