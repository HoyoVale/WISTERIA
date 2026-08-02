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
