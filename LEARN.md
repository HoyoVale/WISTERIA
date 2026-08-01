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

