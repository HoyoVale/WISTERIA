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