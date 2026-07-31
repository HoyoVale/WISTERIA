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
