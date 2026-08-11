# R2.0 Phase 0D Stage 2C — Pass Execution Extraction 基线（2026-08-12）

> 状态：**IMPLEMENTED / VALIDATED（8 个 OpenGL pass executor 已从
> RenderPacket callback 抽出；等待 ChatGPT 复审）**
> 前置：Stage 2B CLOSED ✅

## 1. 目标

```text
把 Renderer::RenderPacket 里的 callback body 整理成显式 OpenGL pass
executor，彻底切分：
  Graph scheduling（RenderPacket / RenderGraph）
  与
  OpenGL implementation of each pass（Execute* executor）

不改变 DAG / 资源语义 / shader / 算法 / stable ABI，不引入 Vulkan。
```

## 2. 改动

```text
renderer.hpp：
  新增 8 个私有 executor 声明：
    ExecuteShadowDepth
    ExecuteGroundReceivers
    ExecuteMmdGroundShadow
    ExecuteOpaque
    ExecuteSkybox
    ExecuteTransparent
    ExecuteOitComposite
    ExecutePhysicsDebug

renderer.cpp（RenderPacket）：
  8 个 pass callback 变为薄 wiring，只调用对应 Execute*；
  RenderPacket 内保留的 GL 调用仅剩 frame setup：
    target.Bind / glDrawBuffer / glEnable(GL_DEPTH_TEST) /
    glDepthMask(GL_TRUE)

render_passes.cpp：
  Execute* 实现 = Stage 2B callback body 逐字迁移（无算法改动），
  包括：
    ExecuteShadowDepth：cascade 计算 + RenderShadowPass + shadow
      texture 绑定 + 恢复 target
    ExecuteGroundReceivers：polygon-offset ground/receiver 循环
    ExecuteMmdGroundShadow：RenderGroundShadowPass
    ExecuteOpaque：非地面 opaque 循环
    ExecuteSkybox：environment->DrawSkybox
    ExecuteTransparent：OIT（EnsureOitResources + BeginOitPass +
      independent-blend / 3.3 fallback 两轮 draw）或 alpha fallback
    ExecuteOitComposite：CompositeOit
    ExecutePhysicsDebug：显式 target.Bind + draw buffer + viewport，
      再 DrawPhysicsDebug（保留 Stage 2B micro-closure 的修复）
```

## 3. 验证

```text
Windows CORE   12/12 PASS
Windows FULL   13/13 PASS
WSL CORE       14/14 PASS
WSL FULL       15/15 PASS
ABI            94 legacy + 30 stable

像素回归证据（沿用 Stage 2B 已建立的 gates，迁移后全部保持绿色）：
  R1.9 stable render pixels match engine（逐字节一致）
  R2.0 render packet graph execution（重复帧 byte-identical、
    full graph RGB 内容、OIT fallback RGB 内容）
  R2.0 OIT + debug SceneColor A/B adversarial（debug line 进入
    SceneColor）
  render-fbo / headless-smoke / headless-smoke-glfw-fallback

代码级证据：
  RenderPacket 不再包含任何 pass body GL 调用；
  所有 Execute* 与 Stage 2B callback body 逐字对应
```

## 4. 下一步

```text
提交后给 ChatGPT 做 Stage 2C 复审（pass executor 代码审查 + 像素回归
审查）。通过后 Stage 2C CLOSED → 0D Final Review → R2.0 Phase 0D
CLOSED。
```
