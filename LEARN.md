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