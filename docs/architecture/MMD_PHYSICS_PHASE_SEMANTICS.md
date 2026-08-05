# MMD 物理相位语义（saba 路径）——#5 社区矩阵输入

> 状态：基于 saba 源码逐行核实（`third-party/saba/.../PMXModel.cpp`、
> `MMDPhysics.cpp`、`MMDNode.cpp`、`MMDIkSolver.cpp`）。本文是 #5 社区差异
> 矩阵的“saba 侧真值输入”，不依赖外部参考实现。

## 1. 帧内相位顺序（saba 真值）

WISTERIA `SabaMmdRuntimeModel::Update` 每帧依次调用：

```text
1. VMDAnimation::Evaluate(frame)        动作求值：写每骨骼 motion 状态（rotate/translate），
                                        并写入 VMD 的 IK on/off 状态
2. PMXModel::UpdateMorphAnimation()     Material/Vertex morph
3. PMXModel::UpdateNodeAnimation(false) “变形后”标志为 false 的骨骼：
                                           UpdateLocalTransform（motion + append/IK 贡献）
                                           根节点 UpdateGlobalTransform
                                           append 节点：UpdateAppendTransform + Global
                                           IK 节点：IkSolver->Solve() + Global
                                           根节点再 Global 一次
4. PMXModel::UpdatePhysicsAnimation(dt) 激活全部刚体；
                                           MMDPhysics::Update(dt)
                                             = world->stepSimulation(dt, maxSubSteps, 1/fps)
                                           ReflectGlobalTransform（物理写回骨骼）
                                           CalcLocalTransform（global→local）
                                           根节点 Global
5. PMXModel::UpdateNodeAnimation(true)  “变形后”标志为 true 的骨骼：
                                           与第 3 步相同的局部/append/IK 流程
6. PMXModel::Update()                   蒙皮矩阵 = Global * InverseInit
```

## 2. 关键语义

### 2.1 “变形后”是逐骨骼标志，不是全局模式

`IsDeformAfterPhysics()` 来自 PMX 骨骼标志位 `DeformAfterPhysics = 0x1000`
（`PMXFile.h:213`，`PMXModel.cpp:641`）。没有全局 Mode 开关：每个骨骼按
自己的标志决定 append/IK 是在物理前（第 3 步）还是物理后（第 5 步）求值。

### 2.2 IK 在两侧都求解

`UpdateNodeAnimation` 内对带 IK solver 的节点调用 `Solve()`——物理前的
非变形后骨骼、物理后的变形后骨骼各解一次。IK 的启用状态由 VMD 每帧写入 +
引擎 override（`SetMmdIkEnabled` → `solver->Enable`）共同决定。

### 2.3 物理只吃“物理前姿态”，写回后补“变形后”

物理步（第 4 步）以第 3 步的结果作为 kinematic 刚体的跟随目标，Bullet 步进
后把刚体变换写回骨骼（`ReflectGlobalTransform`），再在第 5 步补变形后骨骼的
append/IK。这对应 MMD 官方“物理演算”在骨骼变形中的位置：物理前骨骼先定型，
物理驱动身体，变形后骨骼（裙摆尾端、发梢等）再叠加。

### 2.4 Bullet 世界参数

`MMDPhysics` 默认 `fps=120`、`maxSubStepCount=10`（`MMDPhysics.cpp:57-58`），
使用 `btSequentialImpulseConstraintSolver`；`stepSimulation(dt, maxSubSteps,
1/fps)` 固定步。重力默认 `-9.8 × 10 = -98`（10:1 世界尺度，见
`MMD_PHYSICS_COMPAT_BASELINE.md`）。碰撞过滤走 saba 内部
`MMDFilterCallback`（按 PMX collision group/mask + 非碰撞代理名单）。

## 3. 与社区 “Mode 0/1/2” 的关系（待矩阵验证）

libmmd / babylon-mmd 讨论中的 Mode 0/1/2 是**全局执行顺序**概念（物理相对
IK/变形的先后）。saba 没有全局开关，而是用逐骨骼 0x1000 标志在物理两侧各
求值一次。因此：

- “Mode 等价于什么”不能直接从 saba 的单个开关读出；需要在 Phase 0.3 对照
  实验中，用同一资产分别跑 libmmd 的 Mode 0/1/2 与本路径，确认逐骨骼标志
  组合与哪个全局 Mode 等价（或不等价）。
- 本路径当前**无法**由调用方改变物理相对 IK 的相位顺序；这是矩阵定义后的
  第一个候选可配置项。

## 4. WISTERIA 接入点与现有旋钮

| 旋钮 | 入口 | 状态 |
|---|---|---|
| 固定步 / 最大子步 | `SabaPhysicsSettings` + C API | 可配置 |
| 重力 | `SabaPhysicsSettings.gravity` + C API | 可配置（默认 -98） |
| 逐骨骼 IK 开关 | `SetMmdIkEnabled` / C API | 可配置 |
| 动作播放/暂停/循环/帧 | motion C API | 可配置 |
| 变形后标志、物理相位顺序 | PMX 骨骼标志，saba 固定 | 不可配置 |
| 语义碰撞过滤 | saba 内部 `MMDFilterCallback` | 不可配置 |
| 阻尼 / CCD / recovery | saba 路径未实现 | 不可配置 |

## 5. 矩阵输入：需要对照实验回答的问题

1. 同一资产在 libmmd Mode 0/1/2 下的刚体/骨骼轨迹，与本路径逐骨骼标志行为
   的偏差（RMS/max，Phase 0.3 指标）。
2. 变形后骨骼在物理写回后是否还有第二段 append/IK 叠加（本路径有；参考实现
   是否一致）。
3. IK 两侧求解与“仅物理前/仅物理后”模式的等效性。
4. Bullet 固定步 120Hz / maxSubSteps=10 是否与参考实现的可复现参数一致。
5. 重力 -98（10:1 尺度）在参考实现中是否同样成立。

回答以上问题后，再按矩阵定义把“物理相位顺序 / 变形后策略”做成
`SabaPhysicsSettings` 的可配置项，并扩展 C ABI。
