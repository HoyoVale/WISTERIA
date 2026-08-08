# R1.6 — Deterministic Offline Output Pipeline 契约（Phase 0A）

> 状态：**CONTRACT FROZEN（2026-08-08，契约级审查闭合）**。
> 基线：R1.5 CLOSED（`e346414`，四矩阵全绿）。
> 一句话：把统一边界从 **Renderer 入口**推进到**权威离线 SceneColor
> 像素出口**——
> `Runtime → Scene → Renderer → RenderTarget → RGBA pixels`，且
> Saba / WisteriaGeneric / Static 共用同一条输出链，不造第二套
> `MmdOfflineRenderer`。

## 1. 冻结的基本概念（先写死）

```text
Exact State
  = Runtime 到达目标状态是确定的
  = R1.2–R1.4 对 Saba 已经证明（deterministic timeline / checkpoint）

Reproducible Render
  = 相同 Runtime state + asset + presentation + renderer/backend 环境
    → 稳定可复现输出

Pixel-Exact Cross-Platform Render
  = Windows/Linux、NVIDIA/AMD/Intel/Mesa 间逐像素完全一致
  = R1.6 不承诺
```

**禁止把 deterministic Runtime 误读成“不同 GPU 的 PNG hash 必须一致”。**

```text
Offscreen ≠ Headless
  Offscreen = 离屏 FBO → RGBA8 像素（R1.6 Phase 0B 先做）
  Headless   = 不依赖任何 Window System 的 context provider
               （EGL/OSMesa/surfaceless → R1.7 Headless Context Provider）
  hidden GLFW context 是 R1.6 的合法环境前提

Exact State ≠ Pixel-Exact

R1.6 v1 authoritative offline output
  = SceneColor output
  = Renderer::Render 写入 SceneFramebuffer 后的颜色
  = pre-Present / pre-FXAA
Renderer::Present / FXAA 是 Window presentation policy，
不属于 R1.6 v1 offline pixel contract。
```

## 2. 输出最小契约（v1）

R1.6 第一版只支持：

```text
RGBA8
width / height（显式）
explicit Camera
explicit Projection
Scene lights（渲染前已是该 frame 的权威 presentation state）
alpha policy = **OpaqueOnly**：
  clear alpha 固定 1.0；readback 保留实际 A byte；
  v1 不承诺作为透明合成输入（transparent background 暂缓，
  避免拖入 Blend/OIT/材质 alpha/预乘语义）
frame identity（frame index + runtime/build/asset 标识，供 manifest）
```

### RGBA8 内存布局（冻结）

```text
Rgba8Frame v1
  channels        = R, G, B, A
  component       = uint8
  stride          = width * 4 bytes
  storage         = tightly packed
  origin          = top-left
  row order       = top → bottom
  color conversion= 仅 GL float→UNORM8（glReadPixels 原生转换）
  transfer/gamma  = 无额外 sRGB/gamma/tone-map 变换
  = renderer-native RGBA8_UNORM

glReadPixels 原生返回 bottom-left → ReadbackRgba8() 内部垂直翻转，
返回 canonical top-left CPU frame。
分配前检查 width * height * 4 整数溢出。
```

第一版**不做**：

```text
HDR / EXR / GBuffer / motion vector / video encoding / FFmpeg / audio
Vulkan / RenderGraph / IRenderTarget 新抽象 / OffscreenRenderer
MmdRenderer / RenderDevice / HeadlessContext / VideoEncoder
Stable Render C API 冻结
```

### 最重要的输入原则

**不得**定义成：

```text
ModelFrameSnapshot → Renderer
```

Snapshot 是**持久观察/保存/校验接口**，不是 Renderer 的新数据源。

Renderer 的输入是：

```text
Runtime-backed Scene state
+ ModelAsset
+ explicit Presentation state（Camera + Projection + Scene lights）
→ Renderer
```

## 3. 源码审计（基于 R1.5 CLOSED 源码）

### 3.1 `Renderer::Render` 到 `SceneFramebuffer` 的 GL 状态契约

`Renderer::Render(Scene&, const Camera&, const glm::mat4&, SceneFramebuffer&)`
（`src/rendering/renderer.cpp`）：

```text
入口要求：target.IsValid()，否则 throw std::logic_error
RenderStateScope 保存/恢复：active texture、draw/read FBO、draw buffer、
  blend、depth、scissor、stencil、rasterizer discard、cull face、
  color mask、受管纹理单元（8–15）的 GL_TEXTURE_2D 绑定
   （render_state.cpp：CaptureRenderState / RestoreRenderState）
Render 只写 SceneFramebuffer（GL_COLOR_ATTACHMENT0），
  不触碰 default framebuffer
阴影/OIT/地面阴影等中间 pass 使用内部 FBO/纹理
```

结论：**除 viewport 外**，当前 `RenderStateScope` 已覆盖上述受管
GL state；`SceneFramebuffer::Bind()` 会设置 `glViewport`，而
`RenderState` 尚未保存/恢复 viewport（P1-4）。Phase 0B 将 viewport
纳入 guard 后，冻结为 self-contained。调用者只需保证 owning GL
context current + 合法 target。

### 3.2 `SceneFramebuffer` 能否作为 v1 offscreen target

`src/rendering/framebuffer.cpp`：

```text
colorTexture   = RGBA16F（GL_RGBA / GL_HALF_FLOAT）
depthRenderbuffer = GL_DEPTH24_STENCIL8
Resize(width,height) / Bind / Clear / BindColorTexture / IsValid
WindowManager 每帧已经把它当离屏 target 用：
  Resize → Clear → Renderer.Render → Present → SwapBuffers
render_fbo_tests 已直接从 SceneFramebuffer glReadPixels（RGBA8）
```

结论：**可以**。它就是 v1 offscreen target；唯一注意点：

```text
内部格式是 RGBA16F，readback 时 GL 负责转换到 RGBA8
（既有测试已证明该路径成立）
```

### 3.3 framebuffer → RGBA8 readback 所属层

现状 readback 散落在：

```text
src/platform/window_manager.cpp  ReportScenePixel / SaveWindowScreenshotBmp
src/native/native_window.cpp      wisteria_window_read_pixels
tests/render_fbo_tests.cpp        内联 glReadPixels
```

全部带 Window/截图语义。R1.6 应建立：

```text
Framebuffer → CPU pixel buffer
```

冻结归属：**独立 `frame_readback.hpp/.cpp`**（不放
`SceneFramebuffer` 成员函数，也不放 `GraphicsDevice`）：

```cpp
struct Rgba8Frame
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> pixels;
};

Rgba8Frame ReadbackRgba8(const SceneFramebuffer& target);
```

职责划分：

```text
SceneFramebuffer  → GPU target ownership
GraphicsDevice    → GPU resource / context lifetime
frame_readback    → GPU target → CPU frame 转移
```

GL state 保存/恢复（比现有截图代码更强）：

```text
GL_READ_FRAMEBUFFER_BINDING
GL_READ_BUFFER
GL_PACK_ALIGNMENT
GL_PIXEL_PACK_BUFFER_BINDING
GL_PACK_ROW_LENGTH
GL_PACK_SKIP_PIXELS
GL_PACK_SKIP_ROWS
```

执行时：bind PBO=0、alignment=1、rowLength/skip=0，从
`GL_COLOR_ATTACHMENT0` 读整帧后垂直翻转。**不得放进 WindowManager。**

### 3.4 Render 与 Present 是否可分离

`src/rendering/render_present.cpp`：

```text
Render：只写 SceneFramebuffer，不碰 default FBO
Present：只绑定 default draw framebuffer，全屏 quad + FXAA，
  自己带 RenderStateScope，不改 scene target
WindowManager 顺序：Render → (capture) → Present → SwapBuffers
```

结论：**完全可分离**。Offscreen = Render 后**不调用 Present、不
SwapBuffers**，直接 readback SceneFramebuffer。已有 WSLg/Mesa 经验证明
scene FBO readback 比 default back buffer 稳定（R0.2 记录）。

### 3.5 GL context current 的调用/生命周期边界

```text
Application 拥有唯一 GraphicsDevice；第一个窗口注册 context token
（Application::CreateWindow → SetContextToken）
Window::MakeContextCurrent → glfwMakeContextCurrent + SetCurrentContext
所有 GPU 工作（Render/readback/资源创建）要求 owning context current
ReleaseAll 在 Shutdown 用 shared resource context 执行
```

缺口：`MakeContextCurrent` 之后**没有自动
`GraphicsDevice::FlushPendingDeletes()`**（目前只在 ReleaseAll 调用）。

### 3.6 四个 GL lifetime P1 的最小修复位置

```text
P1-1  Framebuffer move 丢失 GraphicsDevice*
        src/rendering/framebuffer.cpp
        Framebuffer(Framebuffer&&) 只搬 framebuffer id，
        device 不搬 → moved-to 对象 Release 走直接 glDeleteFramebuffers
        修复：move ctor/assign 同时
        device = std::exchange(other.device, nullptr)

P1-2  FlushPendingDeletes 无自动调用点
        src/platform/window_manager.cpp RenderWindow
        （以及 native_window read_pixels 等 MakeContextCurrent 后）
        修复：每次 MakeContextCurrent 后、GPU 工作前调用
        拥有 GraphicsDevice 的平台调用方调用
        device->FlushPendingDeletes()；
        不给 Window 增加 GraphicsDevice ownership

P1-3  SceneFramebuffer::Release 直接 glDelete*
        src/rendering/framebuffer.cpp
        colorTexture/depthRenderbuffer 直接 glDeleteTextures/
        glDeleteRenderbuffers，不走 GraphicsDevice 队列
        修复：GraphicsDevice::ResourceKind 增加 Renderbuffer；
        SceneFramebuffer 自己保存 GraphicsDevice*（现在只有内部
        Framebuffer 知道）；
        colorTexture → DeleteResource(Texture)
        depthRenderbuffer → DeleteResource(Renderbuffer)
        framebuffer → Framebuffer::Release

P1-4  RenderStateScope 漏 viewport
        src/rendering/render_state.cpp + renderer_internal.hpp
        SceneFramebuffer::Bind() 设置 glViewport，
        RenderState 不保存/恢复 → Render/Present 后泄漏
        修复：RenderState 增加 GLint viewport[4]；
        Capture：glGetIntegerv(GL_VIEWPORT, ...)；
        Restore：glViewport(...)；
        Render / Present 两个入口一起修复
```

原则：**这些是为让 Offscreen target 生命周期成立而修的窄修复，不借机
重构 GraphicsDevice。**

### 3.7 ModelInstance / Renderer 消费 Pose / Morph / geometry 的完整调用链

```text
Renderer::Render
  → per Entity / RenderPart：
      RenderCommand{ part, model, entity.TryGetPose(), entity.TryGetMorphState() }
  → DrawPart：
      UploadSkinning(pose)        —— GPU 蒙皮矩阵（skin texture）
      UploadMorphing(morphState)  —— CPU vertex/UV morph 偏移
      Mesh dynamic provider       —— Saba 每帧
        → ModelInstance::UploadDynamicVertices
        → Mesh::RebuildInterleavedVertices(positions, normals)
      EvaluateMaterialMorphs(part, morphState)
        —— MorphState 驱动（MMD toon + MorphMaterialIndex）
```

现状：

```text
Saba：        Pose ✅  MorphState ❌（nullptr）  positions/normals ✅  UV ❌
Generic：     Pose ✅  MorphState ✅             geometry（GPU 蒙皮，无 CPU frame）✅
Static：      无动态状态，纯 asset mesh
```

### 3.8 Saba UV / material morph 要补的 neutral render-state 字段

Saba 侧已有数据（`third-party/saba/src/Saba/Model/MMD/`）：

```text
PMXModel::GetUpdateUVs() / m_updateUVs        —— 动态 UV（含 UV morph）
PMXModel::BeginMorphMaterial/EndMorphMaterial/
          MorphMaterial / m_materials          —— 求值后的材质 morph 因子
MMDModel::GetMaterials()
```

WISTERIA 当前完全没有消费。修复方向（Phase 0C）：

```cpp
struct ModelRenderFrameView
{
    ModelVertexFrame geometry;
    // optional dynamic presentation/render channels
    std::span<const glm::vec2> uvs;
    std::span<const MaterialRuntimeOverride> materials;
    const Pose* pose = nullptr;
    const MorphState* morphState = nullptr;
};
```

```text
ModelFrameSnapshot     = persistent observation state（不扩容）
ModelRenderFrameView   = transient renderer-facing state（新）

Saba：    GetUpdatePositions/Normals/UVs + 求值后 material → 填 view
Generic： Pose + MorphState + static asset UV/material（不填动态通道）
Renderer：只消费 neutral view，0 个 Saba / Generic 类型
```

`ModelRenderFrameView` 名字冻结；`MaterialRuntimeOverride` 的精确字段
**不在 0A 猜测**。0A 只冻结：

```text
ModelRenderFrameView = transient、backend-neutral、
                       无 Saba/OpenGL 类型
UV / material        = optional dynamic channels
```

Material override 按 RenderPart、material index 还是其他稳定 asset
identity 映射，Phase 0C 对照 Saba material 与 WISTERIA
Material/RenderPart 后冻结。

## 4. Phase 计划

```text
Phase 0A  Output Contract / Final Entry Audit（本文档，不写代码）
Phase 0B  Offscreen SceneFramebuffer + RGBA8 Readback + GL lifetime guards
Phase 0C  Runtime → Renderer Visual State Completeness
          + Saba UV / Material Morph bridge（ModelRenderFrameView）
Phase 0D  Explicit Presentation Authority
          + Camera / Light 应用（MMD adapter 在 orchestration 层）
Phase 0E  Deterministic Frame Sequence
          + PNG/raw output + manifest + resume/checkpoint 集成
（R1.6 = Phase 0A–0E；Phase 0F 拆为 R1.7 — Headless Context Provider）
```

### Phase 0B 测试（不碰复杂 MMD）

```text
box.glb（Static）：
  Scene → Offscreen SceneFramebuffer → Render → Readback
  width/height 正确、RGBA size == w*h*4、非全零、
  FBO 状态恢复、重复读取合法

readback state preservation guard：
  预先设置 read FBO = X、read buffer = Y、pack alignment = 8、
  PBO = 测试对象、viewport = 非 target 尺寸
  → ReadbackRgba8(target) → 所有受管 state 与之前相同

orientation guard：
  2×2/4×4 target，顶部 red、底部 green
  → frame.pixels[0] == top-left red（top-left contract 永久钉死）

animated_triangle.gltf（WisteriaGeneric）：
  Entity.Update → Render → Readback
  证明第二 Runtime 进入同一条 Output Boundary
  不比较 Windows/Linux pixel hash
```

### Phase 0E 流程（orchestration，不进 Renderer）

```text
Prepare exact frame N
→ Runtime state canonical（R1.2–R1.4）
→ Apply presentation frame N（Camera/Light adapter）
→ Render offscreen
→ Readback RGBA8
→ write frame N（raw + PNG + manifest）
```

Renderer 只负责 `current state → pixels`，不知道 MotionFrameIndex /
Checkpoint / Replay / Saba / PNG。

### 第一版文件输出

```text
raw RGBA8
PNG（独立编码器，Renderer 不知道 PNG）
manifest.json：
  WISTERIA build identity / backend / SabaBaseline or profile
  asset hash / motion hash / frame index / resolution
  camera / lights / output format / renderer-backend info

frames/000000.png ...
manifest.json
```

## 5. 明确不做 / 明确顺序

```text
Phase 0B 不建 IRenderTarget / OffscreenRenderer / RenderDevice
Phase 0B 不碰 EGL / OSMesa / surfaceless
  （hidden GLFW context 是 R1.6 合法环境前提；真正 Headless 属 R1.7）
Phase 0B 不写 PNG 编码器（Phase 0E）
Phase 0A/0B 不冻结 Stable Render C API（输出链正确后再议）
Renderer 不负责 stepping / VMD camera-light sampling / PNG
```

## 6. 开放决策（0A 审查已拍板）

```text
1. readback 落点：独立 frame_readback.hpp/.cpp
   （SceneFramebuffer = GPU target ownership；
     GraphicsDevice = GPU resource/context lifetime；
     frame_readback = GPU target → CPU frame）
2. ModelRenderFrameView 命名冻结；MaterialRuntimeOverride 字段
   Phase 0C 对照 Saba/WISTERIA material 后冻结
3. alpha v1 = OpaqueOnly（clear alpha 固定 1.0）
4. Phase 0F → R1.7 Headless Context Provider
   （R1.6 = Phase 0A–0E CLOSED 即完成）
```
