# WISTERIA

WISTERIA 是一个以 OpenGL、Assimp、GLM 和 Bullet 构建的 C++20 实时渲染实验引擎。当前主线先完成一条可靠的 MMD 纵向链路：PMX 导入、VMD/程序动画、Morph、IK、刚体物理、物理后骨骼与最终渲染。

## 工程结构与物理策略边界

源码已按领域模块整理到 `include/wisteria/<module>` 与 `src/<module>`。物理层分为：

```text
physics/                 格式无关 Bullet 后端
mmd/physics/             PMX/MMD 运行时适配
MmdPhysicsRuntimePolicy  可注入的 WISTERIA 自适应策略
```

> **状态声明（2026-08-07）**：以下 `MmdPhysicsInstance + 共享
> PhysicsWorld + MmdPhysicsRuntimePolicy` 描述属于 **Legacy
> WISTERIA-owned physics path**，目前不参与 `SabaMmdRuntimeModel`
> 主运行链；保留为历史架构与未来通用 `PhysicsInstance` 参考。
> 当前活动 MMD 物理后端为 `ModelBackendRegistry → SabaMmdBackend →
> SabaMmdRuntimeModel`（Saba 自有 Bullet world）。

详细审计与后续社区兼容路线：

- `docs/architecture/PHYSICS_LAYER_AUDIT.md`
- `docs/architecture/PROJECT_LAYOUT.md`
- `docs/architecture/MMD_PHYSICS_COMMUNITY_ADOPTION_PLAN.md`
- `docs/architecture/REFACTOR_MIGRATION.md`

当前活动人物物理配置为 `MmdPhysicsRuntimeSettings`（fixed step
1/120、max substeps 10、gravity -98）。R1.3 Phase 0A 契约已冻结
（`docs/architecture/R1_3_MMD_COMPAT_CONTRACT.md`），默认档
`MMD_RAW`；社区兼容 preset 将在轨迹对照和单位审计完成后单独实现。

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

### MMD 运行时策略注入

默认创建方式保持不变：

```cpp
entity.SetMmdPhysics(world, physicsAsset);
```

也可以显式注入实验策略：

```cpp
MmdPhysicsRuntimePolicy policy =
    MmdPhysicsRuntimePolicy::WisteriaAdaptiveDefaults();
policy.recovery.enabled = false;
policy.ccd.adaptive = false;
policy.enableChainProfiles = false;

entity.SetMmdPhysics(world, physicsAsset, policy);
```

当前接口先提供代码级配置边界。外部 JSON/TOML model profile 会在 `MMD_COMPAT` 字段和社区差异矩阵稳定后加入，以免过早固化错误配置格式。

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

## P1.2 Collision Topology & Response

`TRANSLATION_DELTA` 暴露出的网格挤压主要来自密集刚体碰撞产生的过度位移。本阶段增加接触对/穿透/冲量诊断，过滤同链关节近邻的无意义内部碰撞，将 CCD 改为按真实单步位移动态启停，并降低密集 Dynamic Box 的局部 margin 与 restitution。接触修正参数也显式限制，避免深穿透后把刚体猛烈推开。

人物演示新增：

```text
C      输出碰撞汇总、链间碰撞矩阵和高冲量刚体对
B      RUNTIME / ALL 时显示接触点和法线
F3     显示 active/candidate CCD、接触对、跨链对和最大穿透
```

完整策略、阈值和 Windows 验收步骤见 `P1_2_COLLISION_TOPOLOGY_RESPONSE.md`。

## P1.2 Gravity & Constraint Balance

人物演示可用 `G` 在 Original、Balanced 1.00/0.75/0.50/0.25G 与 Zero G 间切换，并用 `H` 输出逐链重力、下坠、速度、碰撞冲量和 Mode 2 位移。装饰链支持独立重力和最低阻尼，裙摆内部近邻碰撞可按最多四层关节图过滤。完整说明见 `P1_2_GRAVITY_CONSTRAINT_BALANCE.md`。

## P1.3 Chain Semantics & Anchor Recovery

P1.3 不再把 Zero G 下仍存在的异常归因于重力参数，而是修正物理语义：

- 识别 `Skirt_*_*B` 主/辅助层和同环近邻，过滤超过四跳但 Bind 空间重叠的裙摆内部冲突；
- 将未命名、低分支、单锚点的大型装饰组件归入 `DECORATIVE_FALLBACK`，避免 `GENERAL + 0/0 damping`；
- 通过 Bullet `btJointFeedback` 分离统计碰撞冲量与约束冲量；
- 用 FollowBone 锚点距离、Bind 链长和归一化伸长率判断 runaway，不再仅凭离纯动画目标过远恢复。

`H` 报告新增 `constraintImpulse`、`anchorSpeed`、`anchorDistance`、`extensionMax`；F3/`[PHYSICS STATS]` 新增聚合约束冲量、最大伸长率和语义过滤数量。完整设计与验收步骤见 `P1_3_CHAIN_SEMANTICS_ANCHOR_RECOVERY.md`。

## R0 跨平台渲染人工验收

结构稳定化补丁提供固定帧、自动截图、OpenGL error 检查，以及 C ABI
双 Context 生命周期回归：

```powershell
powershell -ExecutionPolicy Bypass -File .\script\verify_render.ps1
```

```bash
./script/verify_render.sh --backend X11
```

WSLg 渲染验收默认读取引擎离屏 Scene FBO，避免默认 back buffer 读回
干扰交换链。可用 `--capture-source default` 复现平台缓冲读回，或用
`--capture-source none` 做纯肉眼窗口检查。

### Linux 平台支持状态（R0.4 归档）

```text
Windows OpenGL                  Supported
Linux native X11                Supported
Linux native Wayland            Supported / 继续扩大测试
Linux headless NULL             Supported
WSLg llvmpipe                   Supported fallback
WSLg Mesa D3D12                 Known compatibility issue
```

WSLg 的 Mesa D3D12（`vendor=Microsoft` / `renderer=D3D12`）存在已知兼容
问题：首帧后默认 back buffer 读回可能全黑，但动画、蒙皮与 GL 调用持续
更新。原生 Debian 与 llvmpipe 均验证正常，因此这不是引擎渲染链路问题。
程序启动时会打印 `[WSLG COMPATIBILITY WARNING]` 提示。

WSLg 下临时使用软件渲染器（速度较慢，但画面正常）：

```bash
./script/verify_render.sh --backend X11 --software-renderer
# 等价于 LIBGL_ALWAYS_SOFTWARE=1 ./script/verify_render.sh --backend X11
```

或直接运行引擎：

```bash
LIBGL_ALWAYS_SOFTWARE=1 ./build-linux/wisteria
```

无显示环境只做编译与测试：

```bash
./script/verify_render.sh --backend NULL
```

详细步骤、日志判断和回传材料见：

```text
docs/architecture/R0_RENDER_MANUAL_ACCEPTANCE.md
```
