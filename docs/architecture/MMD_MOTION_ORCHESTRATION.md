# MMD 多动作编排设计

## 1. 旧实现是怎么做的（已删除，仅作对照）

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

## 3. 建议设计：MmdMotionPlaylist（v1）

放在 `SabaMmdRuntimeModel` 上（当前唯一 MMD 运行时；未来第二个实现出现时
再提升到 `MmdRuntimeModel`）。

```cpp
struct MmdMotion
{
    std::string name;                 // 播放列表显示名
    std::vector<std::filesystem::path> vmdPaths; // 可多个 VMD，走 Add 合并
    std::filesystem::path cameraVmdPath;        // 阶段 3：相机轨道
    bool loop = false;                // 单个动作是否循环
    SabaPhysicsSettings physics;      // 每个动作可带自己的物理参数
};

class MmdMotionPlaylist
{
public:
    void SetMotions(std::vector<MmdMotion> motions);
    void Play(std::size_t index);            // 立即切换（带物理 warmup）
    void PlayNext();                          // 序列前进
    void PlayPrevious();

    enum class Mode { SequenceLoop, SingleLoop, Once };
    void SetMode(Mode mode);

    bool IsEnded() const;                     // Once 模式播完
    std::size_t CurrentIndex() const;
    double CurrentTime() const;               // Saba frame
};
```

切换语义（对齐 Saba 的 `SyncPhysics`）：

1. 记录当前姿势；
2. 重建/选择目标 `VMDAnimation`，把该动作的全部 VMD `Add` 进去；
3. `vmdFrame = 0`；
4. 调用 `model->SaveBaseAnimation()` + 30 帧
   `BeginAnimation → Evaluate(t, weight) → morph → node → physics → node
   → EndAnimation`（即 Saba 现成的 `SyncPhysics`），让物理从旧姿势过渡；
5. 进入常规 `Update` 循环。

播放模式：

- `SequenceLoop`（默认）：按列表顺序播放，每个动作播完自动 `PlayNext`，
  末尾回到第一个——这就是“多个动作编排”的最小闭环；
- `SingleLoop`：当前动作循环，等价旧实现；
- `Once`：播完停在最后一帧。

## 4. v1 不做、留给后续

- **加权交叉淡化**：两段动作同时在场并 50/50 混合。Saba 单实例做不到；
  可选路线是双 `VMDAnimation` + 采样导出 pose，或双 PMXModel 实例。成本高，
  等 v1 播放列表跑通后再评估。
- **动作分层**：身体 + 表情分别独立控制。先靠“骨骼不相交的多 VMD Add
  合并”满足大部分需求；严格分层需要双 controller 组。
- **相机编排**：VMD 相机是独立 `VMDCameraAnimation`，播放列表只负责
  把 `cameraVmdPath` 与动作同步切换（阶段 3）。

## 5. 自动验收建议

- `TestMmdMotionPlaylist`（无资产，用两个程序化 VMD）：SequenceLoop 在
  末尾自动切回；SingleLoop 不前进；Once 停住；切换后 30 帧物理 finite。
- `TestMmdMotionPlaylistWhenAvailable`（叶瞬光 + 两个真实 VMD）：
  切换 10 次，顶点 finite、无 `[ERROR]`。

## 6. 手动验收

- demo 加键盘：`N` 下一个动作、`B` 上一个、`L` 循环模式切换；
- 重点观察动作切换瞬间裙摆/头发是否有爆裂或穿模（`SyncPhysics` 过渡）；
- 与 Saba viewer 的观感对比。
