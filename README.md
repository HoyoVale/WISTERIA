# WISTERIA

WISTERIA 是一个以 OpenGL、Assimp、GLM 和 Bullet 构建的 C++20 实时渲染实验引擎。当前主线先完成一条可靠的 MMD 纵向链路：PMX 导入、VMD/程序动画、Morph、IK、刚体物理、物理后骨骼与最终渲染。

## 运行

Windows PowerShell：

```powershell
.\run.ps1 test
.\run.ps1 run
```

默认只打开一个窗口，播放完整 MMD 人物动作。窗口控制：

```text
Space  暂停 / 继续
R      从头播放并重置物理
P      开关 Bullet 调试线框
```

可选 Demo：

```powershell
# Morph 诊断场景，不再默认占用第二个窗口
.\run.ps1 run -ApplicationArguments '--morph-lab'

# 使用备用人物模型
.\run.ps1 run -ApplicationArguments '--alternate-model'

# 为同一个 Scene 增加第二观察视角
.\run.ps1 run -ApplicationArguments '--multi-window'
```

## 完整人物动作

默认 Demo 会优先加载：

```text
assets/motions/demo.vmd
```

文件不存在或与模型不兼容时，自动回退到内置的 8 秒全身动作。回退动作同时驱动：

- 全亲、中心与 Groove；
- 上半身、下半身、颈部和头部；
- 双肩、双臂、双肘和手腕；
- 双足 IK；
- 眨眼与微笑 Morph；
- 角色全部 PMX 刚体和关节。

每帧运行顺序：

```text
动画与 Morph
→ before-physics Append / Grant / IK
→ PhysicsInstance::PrepareSimulation
→ Bullet 固定子步模拟
→ PhysicsInstance::FinishSimulation
→ after-physics Append / Grant / IK
→ OpenGL 渲染
```

## 物理架构边界

```text
PhysicsWorld
└─ 通用 Bullet 世界、刚体、形状、约束和调试绘制

PhysicsInstance
└─ Entity 级通用物理生命周期接口

MmdPhysicsInstance
└─ PMX 骨骼、刚体、关节、Impulse Morph 与 Bullet 的适配器
```

`Entity` 不再以 `MmdPhysicsInstance` 作为唯一物理所有权类型。它持有通用 `PhysicsInstance`，Scene 只调用通用生命周期；MMD 专用访问器暂时保留给刚体索引和调试工具。未来 glTF 角色、车辆或其他模型格式可以提供自己的 PhysicsInstance，而不需要修改 Scene 的模拟循环。

## MMD 刚体模式与调试观察

PMX 三种刚体模式的语义只存在于 `MmdPhysicsInstance` 适配层，通用 `PhysicsWorld` 仍只处理 Static、Dynamic、Kinematic 刚体：

```text
Follow Bone
动画骨骼 → Kinematic 刚体

Physics
Dynamic 刚体的位置和旋转 → 骨骼

Physics With Bone
Dynamic 刚体完整参与重力、碰撞和关节
但回写骨骼时保留动画平移，只采用物理旋转
```

Mode 2 不能通过关闭刚体线性自由度实现。刚体本身必须允许重力和关节推动；“位置对齐”是 MMD 适配器在模拟后写回骨骼时执行的格式语义。

人物 Demo 中按 `P` 开关 Bullet 调试绘制。当前显示内容来自 Bullet：

- 刚体球体、盒体和胶囊的 wireframe；
- 刚体之间的关节连接；
- 关节限制范围。

颜色由 Bullet 调试器根据对象与约束状态提供，不代表 PMX 的三种刚体模式，不能仅凭红色或白色判断 Follow Bone / Physics / Physics With Bone。建议暂停后观察：

```text
Space  暂停动作，便于比较线框与网格
P      开关物理线框
R      重播动作并把刚体重置到当前 Pose
```

判断方法：

- 线框和网格都不动：动作激励小、关节很紧，或该刚体本来是 Follow Bone；
- 线框在动但网格不动：检查刚体到骨骼映射、Pose 回写和蒙皮权重；
- Mode 2 线框受重力产生位移，而骨骼位置仍跟动画：这是正确的 Physics With Bone 行为；
- 大量跨人物长线通常是关节和限制线，不是额外网格。

## MMD Bind、Reset 与运行时诊断

人物 Demo 将 Bullet 自带运行时线框和 MMD 语义 Overlay 分开：

```text
P      开关当前 Bullet 刚体、关节和限制
B      OFF → BIND → RESET → RUNTIME → ALL → OFF
L      在终端输出完整 MMD 对齐、初始化和运行报告
```

Overlay 含义：

```text
BIND
青色      PMX 原始 Bind Pose
蓝色      CreateBody 后、Reset 前的真实 Bullet Bind Pose

RESET
黄色      约束保持算法生成的 Reset 目标
紫红色    Reset 后从 Bullet 反算的实际状态

RUNTIME
绿色      当前帧物理计算前的纯动画目标
白色      当前 Bullet 刚体

红线      同组两套中心存在可测位置差
```

为了让完全重合的线框仍能同时看见，两套 wireframe 只在调试绘制尺寸上采用 `1.03 / 0.97` 的轻微差异；中心、旋转和真实 Bullet 形状都不会被修改。

初始化不再把每个动态刚体互不相关地传送到 VMD 第 0 帧。MMD 适配层会按非宽行程关节建立连接分量，以最近的 FollowBone 刚体作为动画锚点，对整条动态链应用同一个姿态差，从而尽量保持原始约束关系。

若 Reset 后真实的关节限制违规超过阈值，`Scene` 会统一推进最多 30 个隐藏固定步。单个 `MmdPhysicsInstance` 只提交稳定请求和冻结动画锚点，不会私自推进共享 `PhysicsWorld`。预热第 1、10、30 步均会输出阶段日志；若仍不收敛，则进入 `PHYSICS SAFE FREEZE`，停止将异常物理结果写回网格。

日志需要区分两类数值：

```text
maxJointPos / maxJointRotDeg
关节两端局部 Frame 的原始差异；宽行程辅助关节可能很大

maxLinearViolation / maxAngularViolationDeg
扣除 PMX 允许限制范围后的真实违规

stabilization*Violation
排除明确宽行程、无弹簧辅助关节后用于稳定判定的违规
```

因此不能仅凭 `S` 类辅助关节出现十几个单位的原始差异就判定失败。当前叶瞬光模型的 Reset 结果中，虽然部分宽行程关节原始差异很大，但真实初始化违规接近浮点零，所以不会被误预热或冻结。

按 `L` 后的主要字段包括：

```text
skinBindMax
bindPosMax / bindRotMaxDeg
createBulletPosMax / createBulletRotMaxDeg
postResetPosMax / postResetRotMaxDeg
currentVsPrePhysicsPos / currentVsPrePhysicsRotDeg
maxLinearViolation / maxAngularViolationDeg
severeJoints
```

真实 VMD 的 12 秒 Release 回归还揭示了一个独立问题：初始化保持正确，但长时间运行后部分裙摆、飘带链的约束违规会逐渐增大。该结果说明下一阶段应处理固定步调度、Kinematic 子步插值、求解器稳定性、碰撞 margin 与 CCD，而不是继续修改 Bind/Reset 坐标公式。


## Runtime Physics P1.1：恢复降噪与局部化

P1 的自动恢复已改为只在真实 Bullet 固定步后检测，所有持续时间、冷却和熔断均按物理秒计时。有限 `joint_violation` 必须持续约 0.45 秒；恢复范围从具体 joint/body 开始，在约束图半径 4 内局部选择，默认最多 24 个 Dynamic、32 个总刚体。同一链 12 秒内恢复 3 次会熔断 10 秒，避免重复复位风暴。

完整设计、日志字段和验收方式见 `P1_1_RECOVERY_STABILITY.md`。
