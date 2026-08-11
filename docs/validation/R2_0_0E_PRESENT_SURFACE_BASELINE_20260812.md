# R2.0 Phase 0E — PresentSurface Split 基线（2026-08-12）

> 状态：**IMPLEMENTED / VALIDATED — Phase 0E CLOSED ✅
> （四矩阵重闭合通过；GitHub connector 无可见 CI status）**
> 前置：Phase 0D CLOSED ✅

## 1. 范围

```text
拆分 Window / Renderer / Framebuffer / SwapBuffers：

Window              platform native window + GL context（不变）
SceneFramebuffer    offline scene target（不变）
Renderer::Present   SceneColor → presentation target blit + FXAA（不变）
PresentSurface      呈现端点：拥有 present → swap 顺序（新增）

WindowManager 不再直接调用 Renderer::Present / Window::SwapBuffers；
改为通过 surface：
  managedWindow.presentSurface->Present(renderer, framebuffer)
  [default-framebuffer 诊断读回保持原位]
  managedWindow.presentSurface->Swap()
```

## 2. 交付物

```text
include/wisteria/rendering/present_surface.hpp
  backend-neutral 接口（0 GL / 0 GLFW / 0 Vk）：
    Width() / Height() / Present(renderer, scene) / Swap()
  OffscreenRenderSession 无 PresentSurface 要求（契约 §9）

include/wisteria/platform/glfw_present_surface.hpp
src/platform/glfw_present_surface.cpp
  approved platform bridge（Gate B）：
    Present → renderer.Present(scene, w, h)
    Swap   → window.SwapBuffers()

window_manager.hpp/.cpp
  ManagedWindow 持有 unique_ptr<PresentSurface>（surface 先于
  window 析构）；RenderWindow 走 surface

tests/render_device_neutral_compile.cpp
  Gate A0 扩展：PresentSurface 抽象接口 0-GL 编译
```

## 3. 静态 gates

```text
Gate A0/A  PASS   present_surface.hpp 0 GL（独立编译 target）
Gate B0/B  PASS   GL 调用仅位于 approved platform bridge
                  （GlfwPresentSurface）与既有 renderer 实现
Gate C     PASS   Runtime/Scene/ModelAsset/checkpoint 不受影响
Gate D     PASS   ABI 94 legacy + 30 stable
Gate E     PASS   stable headers 零改动
```

## 4. 验证结果（0E 四矩阵重闭合）

```text
Windows CORE   12/12 PASS
Windows FULL   13/13 PASS（含 native ABI window 路径 → PresentSurface）
WSL CORE       14/14 PASS
WSL FULL       15/15 PASS
ABI            94 legacy + 30 stable
```

## 5. 边界

```text
未做：Vulkan surface/swapchain、backend 选择 API、
stable ABI 修改、Window 结构重写、Present 算法变更。
```
