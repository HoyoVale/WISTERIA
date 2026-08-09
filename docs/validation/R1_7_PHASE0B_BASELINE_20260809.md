# R1.7 Phase 0B — HeadlessContext + EGL Provider 实现基线（2026-08-09）

> 状态：**COMPLETED（含 2026-08-09 Final Fix 修订）**。
> 契约：`docs/architecture/R1_7_HEADLESS_CONTEXT_CONTRACT.md`
> （FROZEN v1.0，四项决策已拍板）。

## 1. 一句话

WISTERIA 现在可以在**完全不创建 GLFW window、不依赖
DISPLAY / WAYLAND_DISPLAY** 的前提下，由引擎自己创建一个 EGL
OpenGL 3.3 Core context，完成 FBO clear + glReadPixels 像素验证，
并满足 Create → Current → Release → Current → Destroy 生命周期。

## 2. 代码改动

```text
include/wisteria/platform/headless_context.hpp
  GraphicsShareGroupToken（const void*，share group 身份，类型冻结）
  HeadlessContextOptions（major/minor、forceSoftware 严格语义）
  IHeadlessContext（MakeCurrent/ReleaseCurrent/ShareGroupToken/
    ProviderName/PlatformName/EglVersion/EglVendor/Vendor/Renderer/
    Version/IsSoftware）
  CreateHeadlessContext（factory）

src/platform/egl_headless_context.cpp
  WISTERIA_ENABLE_EGL 守卫；无 EGL 时编译为 stub（返回 nullptr）
  EGL platform discovery：
    EGL_MESA_platform_surfaceless → EGL_EXT_platform_device
    （hardware → software）
  forceSoftware：只找 EGL_MESA_device_software，找不到明确失败
  EGL_NO_CONFIG_KHR 优先，eglChooseConfig 回退，1x1 pbuffer 兜底
  GL loader：eglGetProcAddress + dlsym fallback → gladLoadGL
  diagnostics：EGL version/vendor、GL vendor/renderer/version、
    provider/platform、IsSoftware
  ShareGroupToken v1 = 单 context handle（0C 定义多 context 映射）

tests/headless_smoke.cpp → wisteria_headless_smoke
  无 GLFW、无 DISPLAY/WAYLAND_DISPLAY
  生命周期 Create → Current → Release → Current → Destroy
  最小 FBO（RGBA8 4x4）→ clear → glReadPixels 逐字节验证

CMakeLists.txt
  Linux（UNIX && !APPLE）find EGL，定义 WISTERIA_ENABLE_EGL
  wisteria_platform 链接 EGL + dl
  wisteria_headless_smoke 目标 + ctest（wisteria.headless-smoke）
```

## 3. 实测结果（2026-08-09，WSL Ubuntu 22.04）

### 3.1 自动模式（env -u DISPLAY -u WAYLAND_DISPLAY）

```text
provider=EGL platform=surfaceless egl=1.5 (Mesa Project)
gl=Microsoft Corporation renderer=D3D12 (Intel(R) UHD Graphics)
version=4.1 (Core Profile) Mesa 23.2.1 software=no

PASS: EGL lifecycle + FBO readback
```

说明：WSL surfaceless 平台默认拿到 Mesa D3D12 设备；R0.4 记录的
"首帧后默认 back buffer 读回全黑" 问题不影响本 0B 的 FBO readback
（引擎渲染链路本来就走 SceneFramebuffer，不走默认缓冲）。

### 3.2 强制软件（--software）

Final Fix 前实测（D3D12 设备被 Mesa 标了 `EGL_MESA_device_software`）：

```text
provider=EGL platform=device-software egl=1.5 (Mesa Project)
renderer=D3D12 (Intel(R) UHD Graphics) software=yes

PASS（但 software=yes 来自设备分类，不是实际 renderer —— 验收错误）
```

**Final Fix 后（严格 gate）**：

```text
--software（WSL，无 LIBGL_ALWAYS_SOFTWARE）
  → FAIL: forceSoftware: GL_RENDERER is not a software renderer
          (renderer="D3D12 (Intel(R) UHD Graphics)")

--software + LIBGL_ALWAYS_SOFTWARE=1
  → platform=device-software renderer=llvmpipe (LLVM 15.0.7, 256 bits)
    version=4.5 (Core Profile) software=yes PASS
```

现在 `IsSoftware()` 只看 GL_RENDERER（llvmpipe/softpipe/swrast），
设备扩展分类不再作为成功证据；D3D12 一律拒绝。这正好匹配
"WSL 只能软件回退、真实 Linux 硬件正常" 的验收口径。

注：`forceSoftware == true` 无条件走 `EGL_EXT_platform_device` +
`EGL_MESA_device_software`，因此即使配合 `LIBGL_ALWAYS_SOFTWARE=1`，
platform 名也始终是 `device-software`（surfaceless 路径只在自动模式下使用）。

Final Micro Fix：`CreateHeadlessContext` 返回前主动 unbind
（eglMakeCurrent(NO_CONTEXT) + 清空两个 tracker），factory 不保持
native current；smoke 对 Create/MakeCurrent/ReleaseCurrent 后的
tracker 状态新增断言，全部通过。

### 3.3 记录在案的 llvmpipe 回退（LIBGL_ALWAYS_SOFTWARE=1）

```text
provider=EGL platform=surfaceless egl=1.5 (Mesa Project)
gl=Mesa renderer=llvmpipe (LLVM 15.0.7, 256 bits)
version=4.5 (Core Profile) Mesa 23.2.1 software=yes

PASS: EGL lifecycle + FBO readback
```

### 3.4 ctest

```text
wisteria.headless-smoke  Passed（0.34s）
```

## 4. Windows 回归

```text
CMake 重新配置：成功
wisteria_platform（MSVC RelWithDebInfo）：编译成功
egl_headless_context.cpp 在无 EGL 时编译为 stub，Windows 保持
GLFW hidden-window 路径不变
```

## 5. Phase 0B 边界确认

```text
未做：HeadlessRenderSession / OfflineFrameSequence migration /
      GraphicsDevice 全面改造 / Windows WGL / OSMesa / Stable C ABI /
      Application 重构 / 渲染级 compatibility probe
```

`GraphicsShareGroupToken` 类型与语义已在 0B 冻结；把
`Window::MakeCurrentContext` 与 `GraphicsDevice` 从
"native context token" 迁到 "share-group token" 是 Phase 0C。

## 6. 下一步

Phase 0C：GraphicsDevice share-group identity + FlushPendingDeletes
生命周期事务（MakeCurrent → 注册 current share-group → Flush）。
