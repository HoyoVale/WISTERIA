# R1.7 Phase 0D — HeadlessRenderSession 实现基线（2026-08-09）

> 状态：**COMPLETED**。
> 契约：`docs/architecture/R1_7_HEADLESS_CONTEXT_CONTRACT.md`
> （Phase 0D 范围已批准）。

## 1. 一句话

WISTERIA 现在可以在**零窗口 HeadlessRenderSession** 上跑通 R1.6 的整条
离线链路：`Scene → Renderer → SceneFramebuffer → RGBA8`（session probe），
以及 `OfflineFrameSequence RenderRange(0..2)` 的确定性序列输出
（PNG + manifest + A/B checkpoint）。整个过程中没有任何 GLFW window、
没有 Application、没有 DISPLAY / WAYLAND_DISPLAY。

## 2. 代码改动

```text
include/wisteria/rendering/headless_context.hpp（新增）
  IHeadlessContext + HeadlessContextOptions 下沉到 rendering
  （无 EGL/GLFW 类型），供 composition root 引用

include/wisteria/platform/headless_context.hpp（瘦身）
  只保留 CreateHeadlessContext factory

include/wisteria/rendering/headless_render_session.hpp
src/rendering/headless_render_session.cpp（新增）
  HeadlessRenderSession：
    ├─ IHeadlessContext（owned）
    ├─ GraphicsDevice（注册 provider ShareGroupToken）
    ├─ ResourceManager（BindGraphicsDevice）
    └─ Renderer（绑定 device）
  MakeCurrent = provider MakeCurrent → FlushPendingDeletes
  RenderOffline(scene, request) = ::wisteria::RenderOffline
  析构：owning context 下 release renderer → ReleaseAll →
    成员逆序析构（context 最后销毁，资源始终在 current context 释放）

tests/headless_smoke.cpp（扩展）
  session probe：Box.glb + directional light → 64x64 RGBA8
    非黑、不透明（渲染级 compatibility probe）
  sequence probe：pmx_physics.pmx + Box.glb →
    OfflineFrameSequence.RenderRange(0..2) → 验证
    00000000.png / 00000002.png / manifest.jsonl /
    checkpoint-A.bin / checkpoint-B.bin

CMakeLists.txt
  headless_render_session.cpp 加入 WISTERIA_RENDERING_SOURCES
```

## 3. 实测结果（2026-08-09）

### 3.1 WSL Ubuntu 22.04（GCC，无 DISPLAY/WAYLAND_DISPLAY）

```text
auto（surfaceless + D3D12）：
  lifecycle + FBO + session probe + sequence probe 全 PASS

LIBGL_ALWAYS_SOFTWARE=1（surfaceless + llvmpipe）：
  全 PASS

--software + LIBGL_ALWAYS_SOFTWARE=1（device-software + llvmpipe）：
  全 PASS

--software（D3D12）：FAIL（严格 gate，预期行为）
```

### 3.2 Windows（MSVC RelWithDebInfo）

```text
编译：core / platform / unit / render / integration / native / wisteria
      全通过
unit 29/29 PASS
render-fbo PASS（含 two-context ownership matrix）
integration PASS（含 R1.7 window release clears trackers）
```

## 4. 语义验收点

```text
1. HeadlessRenderSession 是独立 composition root：
   没有 Application、没有 Window、没有 input/event loop
2. 双身份 ownership 在零窗口链路同样成立：
   Renderer/FBO 的 context-local 对象 owner =
   provider ContextToken；共享资源按 ShareGroupToken
3. session.MakeCurrent() 是完整生命周期事务：
   provider MakeCurrent → 双身份注册 → FlushPendingDeletes
4. RenderOffline 输出 canonical RGBA8：64x64、非黑、alpha=255
5. OfflineFrameSequence 在零窗口 session 上可运行：
   from-start 0..2、PNG + manifest + A/B checkpoint 全部落盘
6. WSL 兼容性口径不变：D3D12 通过 FBO/SceneFramebuffer 路径；
   软件回退（llvmpipe）完整通过；真实 Linux 硬件路径待 0E 矩阵
```

## 5. Closure Fix（2026-08-09 终审后）

```text
HeadlessRenderSession 析构 fail-stop：
  teardown 时 MakeCurrent() 失败 → 打印 FATAL 并 std::terminate()，
  绝不继续 renderer.Release() / graphicsDevice.ReleaseAll() 的
  glDelete*（Renderer 部分资源直接 glDelete，不走延迟队列）。

真实 Linux 硬件路径：由 script/verify_r17_native_linux.sh 在
真实 Linux 机器执行（无 LIBGL_ALWAYS_SOFTWARE），作为 0E
release gate，结果并入 Final Closure。
```

## 6. Phase 0D 边界确认

```text
未做：四矩阵/0E Final Closure、Stable Render C ABI、
      Windows WGL / OSMesa、Application 零窗口模式、多线程渲染、
      视频编码 / Audio
```

## 7. 下一步

Phase 0E：四矩阵验证（Windows CORE/FULL、Linux CORE/FULL）
+ Final Closure；之后 R1.7 全阶段 CLOSED。
