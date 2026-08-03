教程：https://nicolbolas.github.io/oldtut/Basics/Tut01%20Dissecting%20Display.html

最适合的下一步是：把 `Window + Shader + VAO + VBO` 组合成一个完整、稳定的“三角形渲染流程”，并让三角形带颜色。


5. 纹理和材质

加入：

```text
纹理坐标
Texture Object
采样器 sampler2D
材质参数
```

6. 光照

按照这个顺序实现：

```text
法线 + 环境光
漫反射 Lambert
镜面反射 Blinn-Phong
点光源距离衰减
平行光、聚光灯
```

你当前的类设计建议保持这样的职责：

```text
Window       → 创建窗口、处理事件、循环
VBO          → 创建和上传缓冲区数据
VAO          → 保存顶点属性布局
Shader       → 编译着色器
Program      → 链接和使用 Shader Program
Renderer      → 组织绑定和绘制流程
```

实现 Scene：添加／移除／遍历 Entity 和三类光源。
将 Window 当前直接持有的 entity、光源、camera 逐步迁移到 Scene。
实现 Renderer::Render(const Scene&)，把 Window::Run() 中的 uniform 上传和 Draw 逻辑迁移进去。
最后实现 ResourceManager，解决 Mesh、Material、Texture 的共享与加载。

建议接下来的实现顺序是：
Behaviour 抽象类。
RotateBehaviour。
Entity 管理多个 Behaviour。
Scene::Update(deltaTime)。
把 Window 的旋转逻辑迁走。
后续增加 InputState 和 Event，Behaviour 接口无需再次重构。

我建议不要二选一，而是按这个顺序推进：
先实现一个很小的“更新/行为系统”，把旋转代码移出 Window
再实现静态外部模型导入
最后基于导入模型实现骨骼动画系统
原因是“动画”其实有两类：
Transform 动画：整体平移、旋转、缩放，例如当前旋转立方体。
骨骼动画：模型内部骨骼、权重、关键帧和蒙皮计算。
Transform 动画现在就能实现；但骨骼动画依赖外部模型中的节点、骨骼、权重和动画轨道，必须等模型导入结构稳定后再做。


第一版的功能范围
建议第一版只实现：
OBJ 和外部纹理。
position、normal、texCoord。
多 Mesh。
多 Material。
节点局部矩阵。
ResourceManager 路径缓存。
Scene 自动实例化。
暂时不处理：
骨骼和动画。
GLB 内嵌纹理。
PBR 金属度、粗糙度。
法线贴图和切线。
异步加载。


对，不过其中有一项已经完成了一半以上：GLB 内嵌纹理已经可以读取和渲染，目前缺的是支持更多纹理类型，而不是重新实现内嵌纹理解码。

当前状态可以这样划分：

| 功能 | 当前状态 |
|---|---|
| 静态 Mesh、多 Mesh、多 Material | 已完成 |
| OBJ 外部纹理 | 已完成 |
| GLB 内嵌基础颜色纹理 | 已完成 |
| 节点局部变换 | 已完成 |
| 骨骼蒙皮 | 未实现 |
| 动画播放 | 未实现 |
| PBR 金属度/粗糙度 | 未实现，目前是 Blinn-Phong |
| 法线贴图/切线 | 未实现 |
| 输入与事件交互 | 未实现 |
| 异步加载 | 未实现 |

推荐接下来的顺序：

1. 事件与输入系统
   封装 GLFW 键盘、鼠标、窗口事件，先实现可操作相机。

2. 法线贴图与切线
   扩展顶点格式，导入 `tangent/bitangent`，建立 TBN 矩阵。这也是完善材质系统的重要基础。

3. PBR 材质
   支持：

   - Base Color
   - Metallic
   - Roughness
   - Normal
   - Emissive
   - Ambient Occlusion

   GLB 通常会把 metallic 和 roughness 打包在同一张纹理的不同通道中。

4. 骨骼蒙皮
   顶点增加骨骼 ID 和权重，由顶点着色器执行：

   ```glsl
   skinnedPosition =
       boneMatrices[boneID0] * position * weight0 +
       boneMatrices[boneID1] * position * weight1 +
       ...;
   ```

5. 动画系统
   导入动画片段和关键帧，插值位置、旋转、缩放，计算：

   ```text
   动画关键帧
       → 节点局部矩阵
       → 骨骼全局矩阵
       → 最终蒙皮矩阵
       → 上传 Shader
   ```

6. 异步加载
   最后实现，因为它涉及线程边界：

   - 后台线程：读取文件、Assimp 解析、图片解码。
   - 渲染线程：创建 VAO/VBO/EBO/Texture 等 OpenGL 对象。

   OpenGL 资源通常不能直接在普通后台线程中创建，除非额外管理共享 OpenGL Context。

如果目标是先让人物“动起来”，最合适的下一步是实现骨骼数据结构和蒙皮顶点格式；如果目标是先提升画面质量，则先实现切线、法线贴图和 PBR。事件系统相对独立，可以优先做，以便加入自由相机来观察后续效果。


| 动作 | 默认输入 | 类型 |
|---|---|---|
| 向前/后移动 | `W / S` | 持续 |
| 向左/右移动 | `A / D` | 持续 |
| 向上/下移动 | `E / Q` | 持续 |
| 加速移动 | `Left Shift` | 持续 |
| 相机转向 | 鼠标移动 | 持续 |
| 调整视野角 | 鼠标滚轮 | 事件 |
| 捕获/释放鼠标 | 鼠标右键 | 状态切换 |
| 释放鼠标 | `Esc` | 单次 |
| 重置相机 | `R` | 单次 |
| 关闭窗口 | 窗口关闭按钮 | 事件 |


基础的通用 PBR 材质系统已经足够完整，可以作为后续功能的基础；但它还不等于“完整支持 MMD 材质”。

你观察到的“人物各部位光学质感相同”和骨骼无关。

当前模型实际上已经按部位拆分：

- 21 个 Mesh
- 21 个 Material
- 21 个 RenderPart
- 共用约 6 张纹理

骨骼只负责让顶点随关节变形，例如手臂弯曲、头部转动、头发摆动。它不会决定皮肤、衣服、金属饰品分别使用什么材质。

现在质感接近的主要原因是：

- MMD 材质不是标准 PBR 材质。
- 转换到 OBJ/GLB 时，一部分 MMD 参数丢失了。
- `.spa/.sph` 球面贴图尚未支持。
- Toon 阴影纹理尚未支持。
- 多数材质最终使用相似的 `metallic`、`roughness` 默认值。
- 皮肤、布料、头发、金属因此都进入了同一套 PBR 光学模型。

## 推荐顺序

我建议先完善静态 MMD 材质，再开发骨骼。

下一步可以增加 `MmdToon` 材质模型，支持：

- Diffuse Color 和 Alpha
- Specular Color
- Specular Power
- Ambient Color
- `.sph` 乘法球面贴图
- `.spa` 加法球面贴图
- Toon 阴影渐变贴图
- 边缘描边颜色和宽度
- 双面材质
- 半透明材质排序

同时给材质增加类型：

```cpp
enum class ShadingModel
{
    PbrMetallicRoughness,
    MmdToon
};
```

Renderer 根据 `ShadingModel` 使用对应 Shader 和上传规则。这样 PBR 和 MMD 不需要硬塞进同一套计算。

需要注意：如果继续导入转换后的 OBJ/GLB，MMD 特有信息可能已经在转换过程中丢失。要完整支持，最好后面直接读取 PMX，或者启用 Assimp 的 MMD Importer，并检查它保留了多少材质信息。

## 骨骼需要哪些新类

骨骼系统确实需要创建新的结构，但应当分清“模型共享数据”和“实例运行状态”：

```text
ModelAsset
├── Mesh / Material
├── Skeleton          模型共享的骨架定义
└── AnimationClip     模型共享的动画数据

Entity
└── Animator          每个角色自己的播放时间和当前姿势
```

建议的核心类：

- `Bone`：骨骼名称、父骨骼索引、逆绑定矩阵。
- `Skeleton`：管理完整骨骼层级。
- `AnimationClip`：保存位置、旋转、缩放关键帧。
- `Animator`：播放动画并计算当前骨骼矩阵。
- `SkinnedMesh` 或扩展现有 `Mesh`：增加每个顶点的骨骼索引和权重。
- Shader 骨骼矩阵数组：在顶点着色器中完成蒙皮。

因此比较合适的开发路线是：

1. MMD 静态材质区分。
2. 直接 PMX/MMD 材质导入。
3. Skeleton 和顶点权重。
4. GPU 顶点蒙皮。
5. AnimationClip 和 Animator。
6. 动画播放、混合与交互。

这样能先解决你当前看到的材质问题，同时避免误把材质差异归到骨骼系统中。


Scene
  ↓
SceneFramebuffer
  ├─ RGBA16F 场景颜色
  └─ Depth 深度附件
       ↓ 共享深度
OITFramebuffer
  ├─ Accumulation
  └─ Revealage
       ↓
透明合成回 SceneFramebuffer
       ↓
PostProcess
  ├─ FXAA
  ├─ Tone Mapping
  └─ 未来 Bloom
       ↓
Window 默认 framebuffer


多窗口则需要这层结构：
Application / WindowManager
├─ Window A
│  ├─ OpenGL Context A
│  ├─ Renderer A
│  └─ Context-local Framebuffers / VAOs
├─ Window B
│  ├─ OpenGL Context B
│  ├─ Renderer B
│  └─ Context-local Framebuffers / VAOs
└─ Shared ResourceManager
   ├─ Texture
   ├─ Buffer
   ├─ Shader / Program
   └─ ModelAsset
当前需要先拆开的地方有：
Window 不再独自调用 glfwInit() 和 glfwTerminate()，改由 Application 管理 GLFW 生命周期。
将阻塞式 Window::Run() 拆成：BeginFrame()
RenderFrame()
EndFrame()
ShouldClose()

Application 每帧只调用一次 glfwPollEvents()，然后逐个窗口：切换 Context。
更新 viewport。
调用对应 Renderer。
交换缓冲区。

创建第二个窗口时，通过 GLFW 的 share 参数共享 Texture、Buffer、Shader 和 Program。
VAO、FBO 等上下文相关状态仍由各窗口自己的 Renderer 管理。
第一批实现建议包括：
framebuffer.hpp/.cpp创建和销毁 FBO。
添加颜色纹理和深度附件。
完整性检查。
Resize。
Bind/Unbind。

RenderTarget保存宽高、颜色附件和深度附件。

将现有 OIT framebuffer 重构到统一 Framebuffer 抽象。
增加 SceneFramebuffer。
增加最终 Present/FXAA Pass。
然后再实现 Application 与 WindowManager。
---

# Physics 2A：Bullet Foundation

本阶段只建立 WISTERIA 与 Bullet 之间的底层边界，不把 PMX 刚体立即接入 Entity。Physics 1 保存的 `MmdPhysicsAsset` 仍是共享、不可变的模型数据；Physics 2B 才会根据这些定义为每个 Entity 创建独立刚体。

## 为什么使用薄封装

Bullet 已经负责刚体积分、碰撞检测、摩擦、反弹、惯性和约束求解。WISTERIA 不重新实现这些算法，只封装：

- 依赖版本和离线构建；
- GLM 与 Bullet 数学类型转换；
- 物理世界和对象生命周期；
- 安全的代际句柄；
- WISTERIA 风格的形状与刚体描述；
- 固定时间步和输入验证。

公开头文件不会暴露 `btRigidBody`、`btCollisionShape` 或 `btDiscreteDynamicsWorld`。Bullet 类型只存在于 `physics_world.cpp` 和私有转换层，因此 Renderer、Animator、PMX importer 不依赖 Bullet 头文件。

## 项目内置 Bullet

首次接入执行：

```powershell
.\script\setup_bullet.ps1
```

脚本通过 shallow sparse clone 获取官方 Bullet `3.25`，并校验提交：

```text
2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5
```

随后只复制 Bullet 的 `src`、`LICENSE.txt` 与 `VERSION`，把核心源码固定到：

```text
third-party/bullet3/
```

完成后应把该目录提交到 WISTERIA。CMake 不会在配置阶段联网；如果源码缺失，会明确提示执行安装脚本。

WISTERIA 只构建：

```text
LinearMath
BulletCollision
BulletDynamics
```

不构建示例、PyBullet、OpenCL、软体和 Bullet 自带测试。

## WISTERIA 物理接口

### 形状

```cpp
PhysicsShapeDesc::Sphere(radius);
PhysicsShapeDesc::Box(halfExtents);
PhysicsShapeDesc::Capsule(radius, cylinderHeight);
```

Box 参数是半尺寸；Capsule 轴向为 Y，`cylinderHeight` 不包含两端半球。

### 运动类型

```text
Static     质量为零，由引擎创建后保持不动
Dynamic    质量必须大于零，由 Bullet 模拟
Kinematic  质量为零，由 WISTERIA 设置变换，但参与碰撞
```

### 代际句柄

`PhysicsBodyHandle` 同时保存槽位和 generation。刚体销毁后，即使槽位被新刚体复用，旧句柄也不能访问新对象。这避免 Physics 2B 中 Entity 销毁、模型重载或场景清理留下悬空指针。

### 世界生命周期

`PhysicsWorld` 使用 PIMPL 管理 Bullet 对象，销毁顺序固定为：

```text
从 btDiscreteDynamicsWorld 移除刚体
→ 销毁 btRigidBody
→ 销毁 btMotionState
→ 销毁 btCollisionShape
→ 销毁 DynamicsWorld
→ Solver
→ Broadphase
→ Dispatcher
→ CollisionConfiguration
```

Bullet 世界不拥有传入的这些组件，因此顺序必须由 WISTERIA 保证。

### 固定时间步

默认设置：

```cpp
PhysicsStepSettings{
    .maxSubSteps = 4,
    .fixedTimeStep = 1.0f / 60.0f,
    .maxDeltaTime = 0.1f
};
```

渲染帧时间会先限制到 `maxDeltaTime`，再交给 Bullet 的 `stepSimulation` 拆分为固定子步。这样 30 FPS 和 60 FPS 下的基础模拟结果应接近，同时防止窗口暂停后单帧补算过多导致物理爆炸。

## 自动化测试范围

Physics 2A 测试覆盖：

- 描述和参数验证；
- 默认重力与自定义重力；
- 动态球体、真实盒体和胶囊落地；
- 碰撞组与掩码；
- Kinematic 变换；
- 固定子步的帧率一致性；
- Central Impulse；
- 两个 PhysicsWorld 的隔离；
- 代际句柄和 stale handle 拒绝；
- 清理与重复销毁安全。

## 下一阶段

Physics 2B 将建立 `MmdPhysicsInstance`：

```text
MmdPhysicsAsset
→ 为每个 Entity 创建独立 Bullet shape/body/constraint
→ Follow Bone 同步到 Kinematic body
→ Physics body 反写 Pose
→ Physics With Bone 混合同步
→ Spring 6DOF
→ Scene 统一推进 PhysicsWorld
→ ResetToPose
```

Impulse Morph 和 after-physics 骨骼仍留到 Physics 3，避免把基础依赖、MMD 同步和物理后求解一次性混在同一个阶段。

---

# Physics 2B：MMD Bullet Runtime

Physics 2B 将 Physics 1 的不可变 PMX 刚体/关节定义与 Physics 2A 的 Bullet Foundation 接起来。本阶段不重新实现碰撞、积分或约束求解；这些底层工作继续由 Bullet 完成。WISTERIA 负责模型实例、骨骼姿态与 Bullet 对象之间的生命周期和数据同步。

## 运行时所有权

物理数据仍遵守共享资源与实例状态分离：

```text
ModelAsset
└─ MmdPhysicsAsset
   ├─ MmdRigidBodyDefinition[]
   └─ MmdJointDefinition[]

Scene
└─ PhysicsWorld
   └─ Bullet dynamics world

Entity
└─ MmdPhysicsInstance
   ├─ PhysicsBodyHandle[]
   └─ PhysicsConstraintHandle[]
```

`ModelAsset` 只保存 PMX 定义，可以被多个 Entity 共享。每个 Entity 都创建独立刚体、速度和关节状态，因此相同模型的两个角色不会共享裙摆或头发物理。

`PhysicsWorld` 由 Scene 统一拥有。Scene 使用堆上 `PhysicsWorld`，使 Scene 移动后世界地址保持不变；自定义移动赋值会先释放旧 Entity，再替换物理世界，避免实例留下悬空指针。

## Scene 更新顺序

```text
所有 Entity 更新 Animator 与 Behaviour
→ FollowBone / PhysicsWithBone 同步到 Bullet
→ Scene 只调用一次 PhysicsWorld::Step
→ Dynamic / PhysicsWithBone 从 Bullet 回写 Pose
→ Renderer 读取最终 Pose
```

物理世界不能由 Renderer 或每个 Entity 单独推进，否则多窗口会重复模拟，同一场景中的角色也无法正确碰撞。

## 三种 PMX 刚体模式

### Follow Bone

映射为 Bullet Kinematic body：

```text
Pose bone global
× boneToBody
→ model body transform
→ Entity transform
→ Bullet world transform
```

它跟随动画并参与碰撞，但不会反向修改骨骼。

### Physics

映射为 Bullet Dynamic body。物理步骤后执行：

```text
Bullet world transform
→ 去除 Entity transform 与统一缩放
→ model body transform
× bodyToBone
→ desired bone global
→ parent inverse × global
→ Pose local matrix
```

### Physics With Bone

动画在物理前校正刚体中心位置，Bullet 保留旋转和角速度；物理后再把完整结果写回 Pose。它不会退化为完全 Kinematic，也不会让重力持续拉走动画指定的位置。

## Spring 6DOF

Physics 2A 的公开接口新增：

```cpp
PhysicsConstraintHandle
PhysicsConstraintFrame
PhysicsSpring6DofDesc
PhysicsWorld::CreateSpring6DofConstraint(...)
```

Bullet 类型仍不出现在公开头文件中。内部使用 `btGeneric6DofSpring2Constraint`，支持：

- 两刚体约束；
- 单刚体连接世界；
- 平移与旋转上下限；
- 三轴平移弹簧；
- 三轴旋转弹簧；
- 连接刚体之间关闭碰撞；
- 约束代际句柄；
- 销毁刚体时自动销毁引用它的约束。

当前 MMD Runtime 只实例化 PMX type 0 `Spring6Dof`。Physics 1 保存的其他 PMX 2.1 关节类型仍保留在资产中，留给后续阶段接入。指向同一刚体两端的无效自约束会被安全跳过。

## Entity 缩放

Physics 2B 只接受正数统一缩放：

```text
scale.x == scale.y == scale.z > 0
```

刚体尺寸、模型空间位置、关节位置与线性限制会乘统一缩放。非均匀缩放会改变球体、胶囊与旋转盒体的几何意义，因此在创建任何 Bullet 对象前明确拒绝，并保证失败时不泄漏刚体。

## Reset

```cpp
entity.ResetPhysicsToCurrentPose();
```

Reset 会把所有刚体重新对齐当前 Pose，同时清空线速度、角速度与累计力。后续在动画 Seek、循环回绕、切换动画和模型瞬移时应调用该接口。

## 自动化测试范围

Physics 2B 新增测试覆盖：

- PhysicsWorld Spring 6DOF 创建、约束和代际生命周期；
- FollowBone 骨骼到 Kinematic body；
- Physics 重力下落与 Bullet 到 Pose 回写；
- PhysicsWithBone 位置同步和物理旋转；
- Reset 清除速度；
- 每 Entity 独立刚体和约束；
- Entity 移除后的 Bullet 注销；
- Scene 移动赋值后的世界地址稳定性；
- 非均匀缩放拒绝与失败回滚；
- 真实叶瞬光 PMX 的 495 个刚体、568 条关节定义；其中 565 条有效非自引用 Spring 6DOF 成功实例化并完成一帧模拟。

## Physics 2B 当时尚未进入的范围

以下内容在 Physics 2B 时留给 Physics 3；现已由本文后续章节完成：

```text
Impulse Morph → Bullet impulse
Local / Global impulse conversion
Impulse reset command
after-physics Append / Grant / IK
动画 Seek、循环与切换时的自动 Reset
PMX 2.1 其他关节运行时
物理碰撞形状调试绘制
```

---

# Physics 2B 稳定化：可见效果与性能

真实 PMX 接入 Bullet 后，性能问题不能只看 `stepSimulation()`。一次场景物理更新实际包含：

```text
动画 Pose
→ FollowBone / PhysicsWithBone 同步到 Bullet
→ Bullet 固定子步
→ 动态刚体结果回写 Pose
→ Renderer 上传最终蒙皮矩阵
```

## 本次发现的性能根因

“叶瞬光”模型包含 495 个刚体和 568 个关节定义。初版运行时把每一个动态刚体都设置为永不休眠，并且在物理结果回写时，对每根物理骨骼分别调用 `Pose::SetLocalMatrix()`。

`SetLocalMatrix()` 会使 Pose 变脏；下一根骨骼读取父级全局矩阵时，又会触发整套 Skeleton 重算。真实模型有 464 根物理驱动骨骼，因此该流程接近：

```text
物理骨骼数量 × 完整骨架重算
```

形成二次复杂度。Linux 辅助基准中，修复前三帧约为：

```text
总耗时约 1874 ms
Bullet stepSimulation 约 71 ms
Pose 回写约 1793 ms
```

说明主要瓶颈不在 Bullet，而在 WISTERIA 的 Pose 同步层。

## 批量 Pose 回写

`MmdPhysicsInstance` 现在预分配：

```text
drivenRuntimeBodyByBone
localMatrixScratch
globalMatrixScratch
```

每帧只进行一次骨骼 EvaluationOrder 遍历：

1. 复制当前局部矩阵；
2. 为物理驱动骨骼读取 Bullet 结果；
3. 使用已解析的父级全局矩阵计算局部矩阵；
4. 非物理骨骼继续继承最终父级矩阵；
5. 最后只调用一次 `Pose::SetLocalMatrices()`。

这样 Pose 每个物理帧只增加一次 revision，复杂度恢复为线性。

同一辅助基准修复后三帧约为：

```text
总耗时约 77 ms
Bullet stepSimulation 约 64 ms
Pose 回写约 7 ms
```

测试环境中的完整帧耗时降低约 24 倍。该数字只用于定位相对变化，不能替代 Windows 实机 FPS。

## PhysicsWithBone 的线性锁定

PMX `PhysicsWithBone` 的位置由动画骨骼控制，旋转由物理控制。若仍让 Bullet 对其施加普通线性重力，再每帧把位置传送回骨骼位置，会导致：

- 383 个刚体持续被激活；
- 每帧更新大量 AABB；
- 重力位移与位置校正互相抵消；
- 约束网络无法稳定休眠。

Physics 2B 现在为这类刚体设置：

```cpp
linearFactor = glm::vec3(0.0f);
angularFactor = glm::vec3(1.0f);
```

只有动画目标位置实际变化时才同步位置；旋转仍由 Bullet、碰撞和 Spring 6DOF 求解。普通 `Physics` 刚体不再默认 `DISABLE_DEACTIVATION`，允许 Bullet 在稳定后自行休眠。

## Demo 中明确展示物理

角色 Demo 会周期性对一条头发刚体施加小幅交替扭矩，使头发物理效果容易观察。该激励只存在于 Demo Behaviour，不属于 PMX 或引擎默认行为。

Morph Lab 现在使用两刚体结构：

```text
FollowBone Anchor
        ↓ Spring 6DOF
Dynamic Tip
```

左侧参考实例通过 `ModelInstantiationOptions::enablePhysics = false` 禁用物理；右侧活动实例同时展示 Morph 和动态摆动。Impulse Morph 的刚体索引也改为指向动态 Tip，为 Physics 3 接入真实 Impulse 做准备。

## ModelInstantiationOptions

场景实例化增加：

```cpp
ModelInstantiationOptions{
    .enablePhysics = false
}
```

它适用于：

- 静态对照模型；
- 预览缩略图；
- 编辑器中暂时关闭物理；
- 性能对照测试。

共享 `ModelAsset` 不会因此被修改，只有对应 Entity 不创建 `MmdPhysicsInstance`。

---

# Physics 3：完整 MMD 物理运行顺序

Physics 3 在 Physics 2B 的真实 Bullet 刚体与关节基础上，完成了 MMD 物理管线中剩余的运行时语义：Impulse Morph、物理后骨骼、动画时间不连续重置、PMX 2.1 其他关节，以及可选调试绘制。

最终每帧顺序为：

```text
Animator 采样动画、Bone Morph
→ before-physics Append / Grant / IK
→ FollowBone 与 PhysicsWithBone 同步到 Bullet
→ Impulse Morph：先 Reset，再施加 Global / Local impulse
→ Bullet 固定子步模拟
→ Dynamic body 批量回写 Pose
→ after-physics Append / Grant / IK
→ Renderer 使用最终 Pose
→ 可选 Bullet wireframe / constraint debug draw
```

## Impulse Morph

`MorphState::EvaluateImpulseMorphs()` 继续负责将 Group、Flip 和直接 Impulse 权重汇总为每刚体命令；`MmdPhysicsInstance::ApplyImpulseMorphs()` 负责把命令交给 Bullet。

执行顺序必须为：

1. 遍历全部命令，先处理 `reset`；
2. 清除刚体线速度、角速度和累计力；
3. 再遍历命令施加线性冲量与扭矩冲量；
4. Local 通道使用刚体当前世界旋转转换为世界方向；
5. Global 通道直接作为世界方向使用。

```cpp
world.ClearDynamics(body);
world.ApplyCentralImpulse(
    body,
    globalLinear + bodyRotation * localLinear
);
world.ApplyTorqueImpulse(
    body,
    globalTorque + bodyRotation * localTorque
);
```

FollowBone 刚体不是 Dynamic body，因此 Bullet 封装会安全忽略对它们的冲量。Impulse Morph 保持非零权重时会在每个物理帧持续施加，这是 PMX Morph 作为连续控制量的运行方式。

## before-physics 与 after-physics 骨骼

PMX 骨骼的 `deformAfterPhysics` 标志现在真正参与求解阶段划分。`Skeleton` 在构造时保存三份顺序：

```text
MmdConstraintOrder
MmdBeforePhysicsConstraintOrder
MmdAfterPhysicsConstraintOrder
```

`MmdPoseSolver::Solve()` 接收：

```cpp
enum class MmdPosePhase
{
    BeforePhysics,
    AfterPhysics
};
```

Animator 在动画采样后只执行 BeforePhysics；Scene 在 Bullet 回写之后调用 `Animator::SolveAfterPhysics()`。

after-physics 结果不能成为下一帧的动画输入，否则 Append 会逐帧重复累加。Animator 因此缓存 `beforePhysicsPose`：

```text
上一帧 after-physics 最终 Pose
→ 下一帧 Animator::Update 开始时恢复 beforePhysicsPose
→ 重新采样动画与 before-physics 约束
→ Bullet 回写
→ 重新计算 after-physics 约束
```

Bullet 刚体状态独立存在于 PhysicsWorld，因此恢复动画 Pose 不会清除头发、裙摆等动态状态。

## 动画时间不连续与自动 Reset

Animator 维护单调递增的 `DiscontinuityRevision`。以下操作会增加 revision：

- 播放不同动画；
- 使用 `Play(..., restart=true)` 重播；
- CrossFade 开始；
- Stop；
- `SetTime()` 跳转；
- 循环动画跨越结尾回到开头。

Entity 每帧检测 revision。当发现变化时，在下一次物理同步前执行：

```text
ResetToPose
→ 清空速度和力
→ 将全部刚体对齐当前动画 Pose
→ 再进行正常 PrePhysicsUpdate
```

因此 Seek、循环回绕和动画切换不再保留旧帧的裙摆速度，也不会要求 Demo 或调用方手动补 Reset。显式的 `Entity::ResetPhysicsToCurrentPose()` 仍可用于模型瞬移、编辑器操作等非 Animator 事件。

## PMX 2.1 其他关节

PhysicsWorld 的 WISTERIA 接口新增：

```cpp
CreateSixDofConstraint(...)
CreatePointToPointConstraint(...)
CreateConeTwistConstraint(...)
CreateSliderConstraint(...)
CreateHingeConstraint(...)
```

内部 Bullet 映射为：

```text
PMX Spring 6DOF → btGeneric6DofSpring2Constraint
PMX 6DOF        → btGeneric6DofConstraint
PMX P2P         → btPoint2PointConstraint
PMX Cone Twist  → btConeTwistConstraint
PMX Slider      → btSliderConstraint
PMX Hinge       → btHingeConstraint
```

接口仍只暴露 GLM、描述结构和代际句柄，不向 Entity、Scene 或 PMX importer 泄露 `bt*` 类型。

PMX 对部分非 type-0 关节的六轴字段定义比 Bullet 对应约束更宽，因此运行时采用明确映射：

- Slider 使用关节局部 X 轴的线性与角度限制；
- Hinge 使用关节 frame 的局部 Z 轴，读取 PMX Z 角限制；
- Cone Twist：X 对应 twist，Y/Z 对应两个 swing span，并取上下限绝对值的较大者；
- Point-to-point 只使用两个刚体局部 pivot；
- 指向同一刚体两端的自约束继续安全跳过。

## 物理调试绘制

`PhysicsWorld` 增加默认关闭的调试收集器：

```cpp
world.SetDebugDrawEnabled(true);
std::span<const PhysicsDebugLine> lines = world.DebugLines();
```

内部实现 `btIDebugDraw`，收集：

- 碰撞形状 wireframe；
- 关节 frame；
- 约束限制。

Renderer 使用独立的轻量 `GL_LINES` shader 绘制这些线，不进入材质、阴影或 OIT 管线。关闭时不收集、不上传，也不会影响正常渲染性能。

Morph Lab 中按 `P` 可切换调试绘制。窗口标题会显示 `physics ON/OFF`。左侧仍是关闭物理的参考实例，右侧是带 Bullet 刚体和 Spring 6DOF 的活动实例。

## Physics 3 自动化测试

新增测试覆盖：

- Global Impulse Morph；
- Local Impulse 随刚体旋转转换；
- Reset 与同帧 impulse 的先后顺序；
- before/after-physics Append 阶段隔离；
- after-physics 结果不得跨帧累加；
- `SetTime()` 自动重置；
- 动画 loop wrap 自动重置；
- Bullet 6DOF、P2P、Cone Twist、Slider、Hinge；
- MMD Runtime 六类 PMX 关节全部实例化；
- Bullet debug line 采集与关闭清理；
- 原有 Physics 1、2A、2B、Morph、动画、导入和 Demo 回归。

## 后续边界

Physics 3 完成后，MMD 刚体主链已经完整。后续不再需要继续扩张核心物理层，优先工作应转向：

```text
Windows 实机视觉调参
MMD / MikuMikuDance 行为对比
不同模型的关节轴兼容性
物理参数预设与编辑器 UI
调试绘制颜色、筛选和选中高亮
PMX 2.1 Soft Body（独立阶段）
```

Soft Body 不应混入当前刚体运行时；它需要 BulletSoftBody、不同世界类型、网格顶点同步和单独的性能策略。

# Demo Cleanup：完整 MMD 人物纵向链路与通用 PhysicsInstance

Physics 3 之后，默认 Demo 不再同时打开“头部局部动作”和 Morph Lab 两个窗口。项目当前目标是先证明一个人物从模型资源到最终画面的完整链路，因此默认入口收敛为单窗口全身人物 Demo：

```text
PMX 导入
→ ModelAsset / Skeleton / MorphSet / MmdPhysicsAsset
→ Entity / Pose / Animator / PhysicsInstance
→ 全身动作与面部 Morph
→ before-physics MMD 骨骼求解
→ Bullet 刚体与关节
→ after-physics MMD 骨骼求解
→ Renderer
```

Morph Lab 仍然保留，但改为显式的 `--morph-lab` 诊断模式。它用于检查单项 Morph、Impulse 和 Bullet wireframe，不再代表产品默认效果。

## 全身动作 Demo

默认场景先检查：

```text
assets/motions/demo.vmd
```

存在时通过现有 `VmdImporter` 加载，并把骨骼轨道、Morph 轨道和 IK 开关交给 Animator。加载失败时不会让程序退出，而是打印原因并回退到内置动作。

内置动作持续 8 秒，首尾姿态一致，可循环播放。它不是简单地扩大头部摆动，而是为标准 MMD 骨骼建立多轨道 Clip：

```text
全ての親
センター
グルーブ
下半身
上半身 / 上半身2
首 / 頭
左右肩、腕、肘、手首
左右足 IK
まばたき / 笑い Morph
```

全身中心位移和躯干转动会自然带动 Kinematic 身体刚体，随后通过碰撞和关节把惯性传给刘海、长发、衣物和饰品。默认 Demo 删除了人为定时施加的头发扭矩，因此现在看到的摆动来自人物动作本身，而不是测试脉冲。

镜头从头部特写调整为全身构图，灯光范围也覆盖完整角色。控制键：

```text
Space  Pause / Resume
R      Restart animation + reset physics
P      Toggle Bullet debug draw
```

Animator 在循环回绕和 `R` 重播时仍会触发 discontinuity revision，Entity 会在下一物理步前重置刚体，避免上一轮动作速度污染新一轮。

## 为什么先抽象 PhysicsInstance，而不是重写物理框架

此前 Entity 直接拥有：

```cpp
std::unique_ptr<MmdPhysicsInstance> mmdPhysics;
```

这让 Scene 的通用物理生命周期看起来像只服务于 PMX。现在增加最小接口：

```cpp
class PhysicsInstance
{
public:
    virtual ~PhysicsInstance() = default;
    virtual void PrepareSimulation(float deltaTime) = 0;
    virtual void FinishSimulation() = 0;
    virtual void ResetSimulation() = 0;
};
```

Entity 改为拥有：

```cpp
std::unique_ptr<PhysicsInstance> physicsInstance;
```

Scene 的顺序没有任何 MMD 类型：

```text
所有 Entity 更新动画和行为
→ 所有 PhysicsInstance::PrepareSimulation
→ PhysicsWorld::Step
→ 所有 PhysicsInstance::FinishSimulation
→ Animator after-physics phase
```

`MmdPhysicsInstance` 继承 `PhysicsInstance`，内部继续处理：

- PMX 三种刚体模式；
- 骨骼到刚体与刚体到骨骼同步；
- PMX 关节映射；
- Impulse Morph；
- 动画跳转后的物理重置。

Impulse Morph 也被下沉到 `MmdPhysicsInstance::PrepareSimulation()`，Entity 的通用生命周期不再判断 MorphKind 或调用 MMD 方法。

## 仍然保留的 MMD 专用边界

当前不会为了“看起来通用”而删除所有 MMD 类型。以下能力本来就是格式语义，应继续留在适配器：

```text
MmdPhysicsAsset
MmdPhysicsInstance
MmdPoseSolver
PMX rigid-body modes
PMX joint definitions
Impulse Morph
before / after-physics deform order
```

Entity 暂时保留 `TryGetMmdPhysics()` / `GetMmdPhysics()`，用于调试器、测试和 MMD 编辑工具访问刚体索引；正常 Scene 模拟不依赖这些接口。

## 后续多格式方向

当 MMD 人物表现和兼容性稳定后，可以按真实需求增加：

```text
GltfPhysicsInstance
VehiclePhysicsInstance
RagdollPhysicsInstance
SoftBodyInstance
```

它们共享 PhysicsWorld 和 PhysicsInstance 生命周期，但各自维护模型格式到通用物理资源的映射。动画层也应采用相同策略：先出现第二种真实动画运行时，再从 Animator 中抽取共同接口，而不是预先设计一个无法验证的万能骨骼系统。

## 自动化测试

本阶段新增：

- Generic PhysicsInstance 生命周期与重复挂载保护；
- 全身 Demo Clip 的轨道数量、循环时长和幂等性；
- 中心、头部、手臂和足 IK 必须同时产生动作；
- 眨眼和微笑 Morph 轨道；
- 真实 605 骨骼 / 495 刚体人物连续模拟；
- 全身骨骼动作与至少多组刚体必须同时发生位移；
- 所有 Bullet 状态保持有限值。

# MMD PhysicsWithBone 语义修正：模拟状态与骨骼写回是两件事

这次问题不是 Bullet 没有重力，也不是 PMX 没有刚体，而是把 PMX Mode 2 的“骨骼位置对齐”错误地实现成了“禁止刚体平移”。真实模型有 495 个刚体，其中：

```text
Follow Bone         38
Physics             74
Physics With Bone  383
```

旧实现对 383 个 Mode 2 刚体设置零 `linearFactor`，并在每帧物理前把它们的位置传送回动画骨骼。结果是隐藏锤体和关节仍能带来少量旋转反馈，但重力无法完整拉动长发、衣摆与饰品链。

## 正确分层

通用物理层只表达可复用的物理事实：

```text
PhysicsWorld
├─ Dynamic / Static / Kinematic
├─ shape、mass、damping、friction
├─ collision group / mask
├─ constraints
└─ force、impulse、transform、state
```

它不认识 PMX Mode 0/1/2，也不决定骨骼应该接收位置还是旋转。

格式适配层负责 MMD 语义：

```text
MmdPhysicsInstance::PrepareSimulation
└─ 只把 Follow Bone 的动画变换同步给 Kinematic 刚体

PhysicsWorld::Step
└─ Physics 与 PhysicsWithBone 都作为完整 Dynamic Body 模拟

MmdPhysicsInstance::FinishSimulation
├─ Physics：物理位置 + 物理旋转写回骨骼
└─ PhysicsWithBone：保留动画位置 + 物理旋转写回骨骼
```

因此 Mode 2 的刚体可以在 Bullet 世界里受重力平移、碰撞和被关节牵引，但网格骨骼不会被这段物理平移拖离动画位置。不同模型格式未来可以在自己的 `PhysicsInstance` 中采用不同写回策略，而不改变 `PhysicsWorld`。

## 为什么不能每帧 teleport Mode 2

每帧传送一个 Dynamic Body 会同时破坏：

- 重力积分；
- 线速度连续性；
- 关节误差的自然求解；
- 碰撞接触缓存；
- Bullet 的休眠判断。

它看起来像在“保持骨骼位置”，实际上修改了物理对象本身。正确方法是在模拟结束后组合骨骼结果，而不是在模拟前削掉刚体自由度。

## 批量 Pose 写回仍然保留

本次修正没有恢复旧的逐骨骼全骨架重算。`PostPhysicsUpdate` 仍然：

```text
复制当前 local/global Pose
→ 按 Skeleton evaluation order 线性遍历
→ 为物理骨骼组合最终 global
→ 计算对应 local
→ Pose::SetLocalMatrices 一次提交
```

所以语义修正不会重新引入 Physics 2B 初版的二次复杂度卡顿。

## Debug Draw 怎么读

按 `P` 后，WISTERIA 收集 Bullet 的 wireframe、constraint 和 constraint limit 线段，再由 Renderer 一次上传为 GL_LINES。颜色是 Bullet 提供的调试颜色，不是 WISTERIA 自定义的 Mode 图例。

实用检查顺序：

1. `Space` 暂停，在固定姿态下观察刚体是否贴合头发、身体和衣物；
2. `P` 开关线框，对比线框与实际网格；
3. `R` 重置，观察刚体是否回到 Pose，而不是保留上一轮速度；
4. 继续播放，观察 Dynamic/Mode 2 线框是否有惯性与重力位移；
5. 线框动而网格不动时，继续检查骨骼映射和蒙皮，不要先怀疑 Bullet。

## 自动化防回归

测试现在同时证明：

- 合成 Mode 2 刚体能在重力下产生线性位移；
- Mode 2 骨骼保留动画指定的全局位置；
- Mode 2 骨骼采用 Bullet 产生的旋转；
- 真实人物仍为 38 / 74 / 383 的模式分布；
- 真实模型的 Mode 2 刚体中至少有对象产生可测平移；
- 57 项原有渲染、动画、Morph、物理和生命周期测试继续通过。

# MMD Bind Pose Alignment：先证明坐标错在哪里

看到碰撞箱离开网格时，直接修改矩阵公式是危险的。PMX 允许刚体相对骨骼存在固定偏移，也允许模型作者放置不直接驱动可见顶点的辅助锤体。一个视觉上远离网格的刚体，不一定是导入错误。

因此本阶段先建立可验证的四层对照：

```text
PMX source bind transform
        ↓
Skeleton bind × boneToBody
        ↓
Bullet initial body transform
        ↓
Bullet current runtime transform
```

## 通用调试接口仍然保持格式无关

`PhysicsInstance` 增加了可选的：

```cpp
virtual void AppendDebugLines(
    std::vector<PhysicsDebugLine>& lines
) const;
```

Renderer 只消费通用的 `PhysicsDebugLine`，不知道提供者是 MMD、glTF、车辆还是未来的 ragdoll。PMX 的颜色语义、Bind 对照和日志全部留在 `MmdPhysicsInstance`。

```text
Renderer
└─ PhysicsInstance::AppendDebugLines
   ├─ MmdPhysicsInstance
   ├─ future GltfPhysicsInstance
   └─ future VehiclePhysicsInstance
```

## 双重 Bind Overlay

`B` 在四种状态间循环：

```text
OFF
BIND
ANIMATED
ALL
```

`BIND`：

```text
青色      PMX 原始 modelBindTransform
紫红色    Bullet 创建并 Reset 后反算回模型空间的初始状态
红色      两者中心误差超过阈值时的连接线
```

重合时后画的线会覆盖先画的线。因此只对调试 wireframe 尺寸采用 `1.03 / 0.97` 的轻微差异，变换中心和旋转不变。这个处理只影响可视化，不影响 Bullet 形状。

`ANIMATED`：

```text
黄色      旧 skeleton-root-space 候选
绿色      WISTERIA model-space 候选
```

如果 Skeleton 的 `InverseRootMatrix` 为单位矩阵，黄色与绿色应该重合。若根空间非单位，两者的差异会直接显示出来。

## 正式对齐日志

人物物理实例较大或检测到对齐误差时，启动阶段输出一行摘要。按 `L` 输出详细报告：

```text
[MMD ALIGN]
[MMD ALIGN REPORT]
[MMD ALIGN MODE]
[MMD ALIGN BODY]
```

报告会计算：

- `InverseRootMatrix` 的位置和旋转；
- 所有骨骼的蒙皮 Bind 恒等误差；
- PMX Bind 与骨骼重建 Bind 的位置/旋转误差；
- PMX Bind 与 Bullet 初始刚体的误差；
- 旧/新动画映射候选的差异；
- 当前刚体相对动画目标的距离与角度；
- 三种 MMD 模式各自最大的运行偏移；
- 当前关节两端锚点的最大分离；
- 按综合误差排序的刚体名称、骨骼、形状、尺寸和位置。

`Physics` 与 `PhysicsWithBone` 相对动画目标存在一定距离可能是正常动态效果；`FollowBone` 的对应误差应接近零。日志的意义是定位异常链条，而不是把所有动态刚体强制拉回动画目标。

## 非单位根空间公式

PMX 刚体数据与渲染顶点位于模型空间，Assimp 的骨骼全局矩阵可能位于 Skeleton root space。通用公式应先把骨骼转换到模型空间：

```text
boneModelBind = inverseRoot × boneBindGlobal
boneToBody    = inverse(boneModelBind) × bodyModelBind
bodyToBone    = inverse(bodyModelBind) × boneModelBind
```

逐帧动画目标同样使用：

```text
bodyModel = inverseRoot × poseGlobal × boneToBody
```

物理结果写回 Pose 时执行逆方向转换。新增测试故意构造非单位根空间，并确认：

- 蒙皮 Bind 仍为单位变换；
- PMX 与 Bullet Bind 完全对齐；
- 动画刚体不会多出一次根平移；
- 青色与紫红色 Overlay 都被输出。

## 对真实皮肤模型的定位结论

对 732 个刚体、1029 个关节的皮肤模型进行离线诊断后得到：

```text
InverseRootMatrix        identity
skinBindMax              约 1e-7
PMX ↔ skeleton bind      接近 0
PMX ↔ Bullet initial     接近 0
初始 joint anchor        接近 0
```

因此不能把当前尾巴、飘带偏离归因于根空间公式。该公式修正只保护未来可能具有非单位根空间的模型。

下一步判断应依据画面：

```text
青色和紫红色都偏离网格
→ PMX 原始辅助体、形状方向或欧拉旋转解释

青色正确，紫红色错误
→ Bullet 创建/转换错误

Bind 两套正确，当前 Bullet 红色运行体后来跑偏
→ 约束框架、碰撞、时间步或求解稳定性

黄色与绿色分开
→ Skeleton root-space 映射问题
```

这就是“先定位，再修公式”的工程意义：每一种错误只修改拥有该语义的层，避免用全局补偿矩阵掩盖模型数据或求解器问题。


# MMD Initialization Stability：先区分原始 Frame 差异与真实限制违规

双重 Bind Overlay 解决了“数据在哪一层开始错位”的问题，但它还必须区分捕获时机。Bullet 刚体创建后如果立刻执行 `ResetToPose()`，再记录所谓 initial state，就会把 VMD 第 0 帧误标成 Bullet Bind。正确快照顺序是：

```text
PMX source bind
→ CreateBody bind
→ constraint-preserving reset target
→ post-reset Bullet state
→ pre-physics animation target
→ current Bullet state
```

这些快照分别由 BIND、RESET、RUNTIME 三个 Overlay 显示。

## 为什么原始关节锚点差不能直接作为失败阈值

6DOF 关节允许两端 Frame 在其局部限制区间内存在平移和旋转。诊断需要先计算：

```text
relative = inverse(anchorA) × anchorB
violation = distance(relative, allowed limits)
```

`maxJointPos` 只是两端 Frame 的原始距离；真正用于稳定判定的是扣除 `linearLower/Upper` 和 `angularLower/Upper` 后的 violation。模型中的宽行程、无平移弹簧辅助关节还需要单独标识，避免正常设计被误判。

## 约束保持 Reset

逐个把所有动态刚体传送到各自骨骼目标，会破坏按 Bind Pose 创建的长链约束。现在 MMD 层构建非宽行程关节图，并执行多源 BFS：

```text
FollowBone 刚体 = 动画锚点
动态刚体       = 分配到最近动画锚点
同一分支       = 应用同一 anchor animation delta
```

这不会改变 `PhysicsWorld` 的通用语义。Bullet 仍然只认识刚体和约束；“如何从 PMX 骨骼姿态得到一组约束友好的 Reset 目标”只存在于 `MmdPhysicsInstance`。

## Scene 统一预热

共享 `PhysicsWorld` 不能由某个模型实例私自推进。通用 `PhysicsInstance` 因此只提供可选稳定生命周期：

```cpp
StabilizationRequest()
PrepareStabilizationStep()
ObserveStabilizationStep()
CompleteStabilization()
```

Scene 收集所有请求并统一执行隐藏固定步。MMD 实例在预热时只固定 FollowBone 锚点、记录第 1/10/30 步误差，并在最终不收敛时进入安全冻结。未来其他格式可以使用不同的稳定策略，也可以完全不请求预热。

## 本阶段验证边界

Release 下完整播放真实 VMD 约 12 秒，所有刚体状态保持有限，没有 NaN、生命周期错误或初始化误冻结；但运行末尾仍检测到部分非辅助关节违规和 Mode 2 长链漂移。这是下一阶段的运行时求解问题，不能通过继续篡改 Bind Pose 或把 Mode 2 平移重新锁死来掩盖。

# P1.2 Collision Topology & Response：先治理刚体过度位移，再让蒙皮忠实跟随

Mode 2 A/B 证明 `TRANSLATION_DELTA` 能让骨骼和顶点明显贴近 Bullet，但也让尾巴、头发和裙摆的真实碰撞问题暴露出来。这里不能再次通过丢弃骨骼平移掩盖问题，正确链路是：

```text
密集代理接触
→ 穿透修正和冲量
→ 刚体过度位移
→ 物理骨骼平移
→ LBS 网格挤压、拉伸或穿模
```

工程上必须先知道“哪两个刚体在推、推了多深、冲量多大”。因此 `PhysicsWorld` 在固定步后聚合 manifold，MMD 层再映射为 PMX body、物理链和 collision group/mask。`C` 输出链间矩阵与高冲量 pair，RUNTIME Overlay 则画出接触点和法线。

直接 joint pair 已由 Bullet constraint 禁止碰撞；额外过滤只覆盖同链、两跳、Bind Pose 很近的 Dynamic pair。它避免约束拉近与碰撞推开互相打架，但不会全局删除跨链碰撞。

CCD 也不能再由尺寸一次性决定。尺寸只选候选，真正启用取决于当前线速度与角速度预测的单步位移；低速持续 0.25 秒后关闭。这样可以保护高速小刚体，同时避免数百个低速裙摆代理永久进入连续碰撞路径。

密集 Box 的 margin 和 restitution 只做局部收敛，solver 的错误修正速度也设置上限。顺序始终是：

```text
碰撞拓扑过滤
→ 动态 CCD
→ 局部代理参数
→ 接触修正强度
```

不能用更多 solver iteration 替代错误的碰撞关系。
# P1.2 Gravity & Constraint Balance：重力是持续输入，碰撞冲量是放大器

碰撞矩阵显示，大多数高冲量接触来自同一裙摆分组内部，而非跨链碰撞。只削弱蒙皮平移会再次隐藏问题，因此应先控制 Bullet 的持续下坠输入。WISTERIA 现在为 MMD Dynamic 刚体使用可选的每体重力覆盖，并将大型恢复连通分量拆成更细的 `SKIRT / HAIR / TAIL / ACCESSORY / GENERAL` 重力分组。

平衡模式采用：

```text
world gravity × global A/B scale × chain scale
```

同时只提高到分组最低阻尼，不覆盖更高的 PMX 原值。`G` 用于 1g、0.75g、0.5g、0.25g、0g 对照；`H` 输出每个分组的向下位移、速度、接触冲量和 Mode 2 位移。若 0g 下仍有巨大冲量，问题就不再是重力，而是初始重叠、动画锚点推入或碰撞/约束拓扑。裙摆的三至四跳 Box 近邻会在 Bind Pose 仍重叠时被过滤，避免关节拉近与碰撞推开形成永久反馈。

