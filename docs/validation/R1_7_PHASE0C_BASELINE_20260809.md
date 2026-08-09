# R1.7 Phase 0C — GraphicsDevice Share-Group Identity 实现基线（2026-08-09）

> 状态：**COMPLETED（含 2026-08-09 Final Fix 修订）**。
> 契约：`docs/architecture/R1_7_HEADLESS_CONTEXT_CONTRACT.md`
> （Phase 0C 范围已批准）。

## 1. 一句话

`GraphicsDevice` 不再用 "第一个 GLFWwindow*" 近似身份，而是统一持有
**share-group token**；`Window::MakeContextCurrent` 成为
`MakeCurrent → 注册 current share-group → FlushPendingDeletes`
的生命周期事务。多窗口共享 context 下，第二个窗口的
`ContextIsCurrent()` 现在正确成立。

## 2. 代码改动

```text
include/wisteria/rendering/graphics_context.hpp（新增，双身份）
  GraphicsShareGroupToken = const void*（类型从 platform 下沉到 rendering，
  platform/headless_context.hpp 复用；依赖方向 rendering ← platform）

include/wisteria/rendering/graphics_device.hpp + src/rendering/graphics_device.cpp
  SetContextToken → SetShareGroupToken
  ContextToken → ShareGroupToken
  HasContextToken → HasShareGroupToken
  RequireContextToken → RequireShareGroupToken
  SetCurrentContext/CurrentContext → SetCurrentShareGroup/CurrentShareGroup
  ContextIsCurrent：当前 share group == 设备 share group 即成立

include/wisteria/platform/window.hpp + src/platform/window.cpp
  Window 持有 contextToken + shareGroupToken（WindowManager 赋值）+
  非拥有 GraphicsDevice*（仅生命周期事务用）
  MakeContextCurrent = glfwMakeContextCurrent
                     → SetCurrentContext(contextToken)
                     → SetCurrentShareGroup(shareGroupToken)
                     → device->FlushPendingDeletes()
  Release：owning context 释放 context-local 资源；
    glfwDestroyWindow 后清空 CurrentContext 与 CurrentShareGroup

include/wisteria/platform/window_manager.hpp + src/platform/window_manager.cpp
  ShareGroupIdentity 成员 + ShareGroupToken()（所有窗口共享同一身份）
  CreateWindow：窗口赋 token/device；恢复 previous context 时
    注册 share group + flush（catch 与正常路径一致）
  SetGraphicsDevice：向 windows/pendingWindows 传播 device
  RenderWindow 删除散落的手动 FlushPendingDeletes
  DestroyAllWindows 改用 window->MakeContextCurrent()

src/platform/application.cpp
  CreateWindow：首窗注册 device 的 share group
  Shutdown：以 share-group token 校验；校验失败 → fail-stop，
    跳过 GL teardown；结束前清空两个 tracker

src/native/native_window.cpp
  read_pixels 删除手动 FlushPendingDeletes（事务已覆盖）

src/platform/egl_headless_context.cpp
  ShareGroupToken 改为 provider 持有的身份对象（非 native handle）
  新增 ContextToken 身份对象；MakeCurrent 注册双身份；
  ReleaseCurrent 检查 eglMakeCurrent 返回值后再清 tracker；
  构造失败释放已创建 EGL 资源；forceSoftware 校验真实 GL_RENDERER

tests/unit_tests.cpp、tests/render_fbo_tests.cpp
  迁移到新 API（GraphicsDevice ownership、deferred-delete queue）
```

## 3. 验证结果（2026-08-09）

### 3.1 Windows（MSVC RelWithDebInfo）

```text
编译：wisteria_platform / wisteria_core / unit / render / integration / native
     全部成功（唯一 warning 为既有 C4190，与本次改动无关）
unit 29/29 PASS（含 GraphicsDevice ownership 新语义）
render-fbo PASS（含 deferred-delete queue flushed）
integration CORE 全 PASS（Application / 多窗口 / native ABI / R1.6 全链）
```

### 3.2 WSL Ubuntu 22.04（GCC，无 DISPLAY/WAYLAND_DISPLAY）

```text
编译：wisteria_core / wisteria_platform / headless_smoke / unit_tests 成功
smoke 自动（surfaceless + D3D12）      PASS
smoke --software（无 LIBGL_ALWAYS_SOFTWARE） FAIL（严格 gate 拒绝 D3D12）
smoke LIBGL_ALWAYS_SOFTWARE=1（llvmpipe 4.5 Core） PASS
unit 全部 PASS（含 GraphicsDevice ownership）
```

## 4. Final Fix 验证（2026-08-09 二轮审查后）

### 4.1 Windows（MSVC RelWithDebInfo）

```text
编译：core / platform / unit / render / integration / native / wisteria 全通过
unit 29/29 PASS
render-fbo PASS，新增：
  [RENDER FBO] two-context share-group ownership matrix
  （共享 Texture 在兄弟 context 立即删除；
    VAO/FBO 排队且兄弟 context 不 flush；
    切回 owning context 后释放；销毁 context 后 tracker 为空）
integration PASS，新增：
  [PASS] R1.7 window release clears trackers
```

### 4.2 WSL Ubuntu 22.04（GCC）

```text
smoke 自动（surfaceless + D3D12）       PASS
smoke --software（D3D12）              FAIL（严格 gate，预期行为）
smoke --software + LIBGL_ALWAYS_SOFTWARE=1（llvmpipe 4.5） PASS
unit 全部 PASS
render-fbo（llvmpipe）PASS，含 two-context ownership matrix
```

### 4.3 双身份代码落地

```text
include/wisteria/rendering/graphics_context.hpp
  GraphicsContextToken + GraphicsShareGroupToken

GraphicsDevice::DeleteResource(kind, name, owningContext)
  shared（Texture/Buffer/Renderbuffer）：share group 判断
  context-local（VertexArray/Framebuffer）：owning context 判断
  pending 项记录 owningContext；FlushPendingDeletes 按域处理

Framebuffer::Create / VAO 构造：捕获 GraphicsDevice::CurrentContext()
  作为 owning context；Release/析构时传给 DeleteResource

Window::MakeContextCurrent：ContextToken → ShareGroupToken → flush
Window::Release：销毁 native context 后清空两个 tracker
Application::Shutdown：share-group 校验失败 → fail-stop，
  不再继续 GL teardown
EglHeadlessContext：ContextToken 身份对象；构造失败清理；
  ReleaseCurrent 检查返回值；forceSoftware 验证 GL_RENDERER
```

## 5. 语义验收点

```text
1. 多窗口共享 context：第二个窗口 MakeContextCurrent 后注册的是
   与首窗相同的 ShareGroupToken → ContextIsCurrent() == true
   → 该窗口销毁 GPU 对象立即删除，不再排入 pending 队列
2. MakeCurrent 事务：Window::MakeContextCurrent 内部完成
   MakeCurrent → 注册 share group → FlushPendingDeletes；
   RenderWindow / native read_pixels 不再各自手动 flush
3. token 与 native handle 解耦：GLFW window 由 WindowManager 映射到
   ShareGroupIdentity；EGL context 由 provider 身份对象映射；
   不存在"换一种 native handle 继续比指针"的问题
4. legacy 模式保留：未注册 token 的 GraphicsDevice 仍立即删除，
   兼容直接使用 GLFW 的测试路径
5. context-local 排队项记录 owning ContextToken；同 share group 的
   兄弟 context 不得 flush（render-fbo 矩阵已证明）
6. 销毁 current native context 后，CurrentContext 与
   CurrentShareGroup 必须同时为空（integration 已证明）
7. ReleaseAll ownership 校验失败后不执行 GL 删除（fail-stop）
```

## 6. Phase 0C 边界确认

```text
未做：HeadlessRenderSession / Application 零窗口 / OfflineFrameSequence
      migration / Windows WGL / OSMesa / Stable C ABI /
      渲染级 compatibility probe
```

## 7. 下一步

Phase 0D：HeadlessRenderSession（IHeadlessContext + GraphicsDevice +
ResourceManager + Renderer 组合根），让 `RenderOffline` 与
`OfflineFrameSequence` 在零窗口 session 上运行，并补渲染级
compatibility probe（WSL 默认软件回退路径）。
