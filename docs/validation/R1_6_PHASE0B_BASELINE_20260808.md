# R1.6 Phase 0B — Offscreen + RGBA8 Readback 实现基线（2026-08-08）

> 状态：**COMPLETED（四矩阵全绿）**。
> 契约：`docs/architecture/R1_6_OFFLINE_OUTPUT_CONTRACT.md`
> （CONTRACT FROZEN，2026-08-08）。

## 1. 一句话

复用现有 `SceneFramebuffer` 建立第一个正式的
`Scene → RGBA8 CPU Frame` 边界，并完成四个 GL lifetime P1 窄修复；
未新建 IRenderTarget / OffscreenRenderer，未动 Present/FXAA 语义。

## 2. 代码改动

### 新增：frame_readback

```text
include/wisteria/rendering/frame_readback.hpp
src/rendering/frame_readback.cpp
CMakeLists.txt：WISTERIA_RENDERING_SOURCES 加入
```

```cpp
struct Rgba8Frame
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> pixels;
};

Rgba8Frame ReadbackRgba8(const SceneFramebuffer& target);
```

冻结语义：

```text
channels RGBA / uint8 / stride = width*4 / tightly packed
origin = top-left，rows top → bottom（内部做 GL bottom-left 垂直翻转）
color = renderer-native RGBA8_UNORM（仅 GL float→UNORM8）
分配前 width*height*4 溢出检查
GL state 保存/恢复：
  GL_READ_FRAMEBUFFER_BINDING / GL_READ_BUFFER /
  GL_PACK_ALIGNMENT / GL_PIXEL_PACK_BUFFER_BINDING /
  GL_PACK_ROW_LENGTH / GL_PACK_SKIP_PIXELS / GL_PACK_SKIP_ROWS
执行时 PBO=0、alignment=1、rowLength/skip=0
恢复顺序：先绑回原 read FBO，再 glReadBuffer（避免对 FBO 设
GL_BACK 触发 GL_INVALID_OPERATION）
异常安全：CPU 分配（pixels.resize）在触碰 GL state 之前完成；
GL state 修改包在 ReadbackStateScope（RAII）内，任何后续异常
都会恢复全部受管 state
```

### GL lifetime 窄修复

```text
P1-1  Framebuffer move ctor/assign 同步 std::exchange(other.device)
P1-2  WindowManager::RenderWindow 与 native_window read_pixels 在
       MakeContextCurrent 后调用 device->FlushPendingDeletes()
P1-3  GraphicsDevice::ResourceKind 增加 Renderbuffer；
       SceneFramebuffer 自己保存 GraphicsDevice*；
       Release 统一走 DeleteResource(Texture/Renderbuffer)
P1-4  RenderState 增加 GLint viewport[4]；
       Capture glGetIntegerv(GL_VIEWPORT) / Restore glViewport；
       Render 与 Present 共用 guard 一起修复
```

未给 `Window` 增加 GraphicsDevice ownership（按契约最小落地）。

## 3. 测试（render_fbo_tests，Windows/Linux 实跑）

新增四块：

```text
readback state preservation：
  4×4 target + 敌意 GL state（read FBO=0、read buffer=GL_BACK、
  pack alignment=8、PACK_ROW_LENGTH=7、PACK_SKIP_PIXELS=2、
  PACK_SKIP_ROWS=1、PBO、viewport=(1,2,3,4)）
  → ReadbackRgba8 后全部受管 state 原样（7/2/1 保持）
  → 测试块结束恢复 viewport 与 pack row/skip 默认值，避免污染

orientation：
  4×4 target 顶部 red / 底部 green
  → pixels[0] == top-left red、bottom-left green（top-left 钉死）

static box.glb：
  Scene → Render → Readback；尺寸正确、**至少一个 RGB pixel != clear
  （不把 opaque alpha 当渲染成功）**、
  Renderer::Render 后 viewport 保持 (3,4,5,6)（P1-4）、重复 readback 一致

Generic animated_triangle.gltf：
  Scene.Update(0.25) → Render → Readback **RGB 内容证明**；
  Update 到 0.75 再 Render → 像素变化（第二 Runtime 进入同一条
  Output Boundary）
```

## 3.1 Final Guard（2026-08-08 第二轮审查闭合）

```text
1. ReadbackRgba8 异常安全：
   pixels.resize 先于任何 GL state 修改；
   GL 操作包在 ReadbackStateScope（RAII）内
2. Static / Generic 断言改为 RGB 证明：
   不再把 clear 的 alpha=255 误当渲染成功
3. pack rowLength/skip 敌意值真实设入（7/2/1）并验证恢复；
   测试结束恢复默认值
4. 契约 §3.1 措辞修正：
   self-contained = 对 WISTERIA 显式追踪的 frame-boundary state set；
   不承诺任意未列入契约的外部 GL state
5. 记录既有债务：GraphicsDevice share-group identity 用单 context
   token 近似，multi-window 下第二窗口不 flush；
   R1.6 v1（single hidden GLFW context）不受影响，
   列入 R1.7 platform lifetime
```

## 4. 四套矩阵（2026-08-08 实测）

```text
Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）。

## 5. 边界（Phase 0B 不做）

```text
v1 authoritative output = pre-Present / pre-FXAA SceneColor
Present/FXAA 仍是 Window presentation policy
不建 IRenderTarget / OffscreenRenderer / RenderDevice
不碰 EGL / OSMesa / surfaceless（hidden GLFW 是 R1.6 合法环境）
不写 PNG / manifest（Phase 0E）
不冻结 Stable Render C API
```

## 6. 下一步

Phase 0C：`ModelRenderFrameView`（transient renderer-facing state）+
Saba UV / Material Morph bridge；`MaterialRuntimeOverride` 字段在
0C 对照 Saba material 与 WISTERIA Material/RenderPart 后冻结。
