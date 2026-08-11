# R2.0 — Final Architecture Closure（2026-08-12，ChatGPT 5-blocker 修复）

> 状态：**IMPLEMENTED / VALIDATED — 四矩阵 + ABI 全绿；待 ChatGPT
> 定点复查**
> 依据：ChatGPT 对 `360453b` 的横向审查（P0-1..P0-5），R2.0 不能
> CLOSED、R2.1 保持 HOLD，直到 frozen 0A contract 的架构边界真正闭合。

## 1. P0-1 — RenderDevice 成为 graph execution authority

```text
之前：
  RenderGraph::Execute → std::function callback → Renderer::Execute*
  （RenderDevice 完全不参与 graph execution）

现在：
  render_device.hpp 新增
    virtual void ExecuteGraph(RenderGraph& graph) = 0;
  OpenGlRenderDevice::ExecuteGraph → graph.Execute()
    （OpenGL backend 拥有 wired callback 执行；
     未来 Vulkan backend 解释同一 graph data，不依赖 callback）
  Renderer::RenderPacket 改为
    this->renderDevice->ExecuteGraph(graph)
    （Renderer(nullptr) legacy 兼容路径保留直接 graph.Execute）

资源权威边界：
  Renderer 持有的 GLuint/VAO/Program 明确属于 OpenGL pass executor
  layer（contract §14：VAO/FBO ownership 是 OpenGL backend 实现细节），
  不再是“RenderDevice 之外的第二套公开 authority”。
```

## 2. P0-2 — PresentSurface 语义中性化

```text
PresentSurface（platform endpoint only）：
  Width() / Height()；删除 Renderer& / SceneFramebuffer& / Swap()

新增 RenderTarget（backend-neutral）：
  Width() / Height()；SceneFramebuffer 实现该接口

新增 PresentationTarget（backend-created）：
  Present(const RenderTarget&) / Swap()

RenderDevice::CreatePresentationTarget(
    const PresentSurface&,
    PresentBlitFunction,      // composition root 提供 OpenGL blit
    PresentSwapFunction)      // composition root 提供平台 swap
  OpenGlRenderDevice 实现 → OpenGlPresentationTarget

WindowManager 只提供 blit/swap 回调：
  blit = renderer.Present(SceneFramebuffer, w, h)
  swap = window.SwapBuffers()
  present→[诊断读回]→swap 顺序原样保留

Vulkan 未来映射：PresentSurface → VkSurfaceKHR；
PresentationTarget → swapchain + vkQueuePresentKHR（同一接口）。
```

## 3. P0-3 — RenderGraph frozen resource semantics

```text
render_graph.hpp 新增：
  RenderResourceKind::DepthStencil
  RenderResourceAspect { Color, Depth, Stencil }
  RenderLoadOp { NotApplicable, Load, Clear }
  RenderStoreOp { NotApplicable, Store, DontCare }
  RenderAccessDesc（resource + access + aspect + load/store + clearValue）
  RenderResourceLifetimeSpan（firstUse / lastUse）
  RenderGraph::TransientLifetime(name)

Validate() 新增：
  - aspect/kind 一致性检查（Color 资源禁 Depth/Stencil aspect）
  - hazard 按 (resource, aspect) 分组：
      Depth 写与 Stencil 写互不 hazard（对应 Vulkan aspect mask）
  - Transient read-before-initialization gate：
      第一个执行序访问必须 Write 或显式 Load/Clear
  - TransientLifetime 计算 first-use / last-use（执行序）

builder 语义升级：
  sceneDepth = DepthStencil（SceneFramebuffer 是 GL_DEPTH24_STENCIL8）
  shadowDepth Write = Clear + Store（每 cascade 清 depth）
  oitAccum/oitReveal Write = Clear + Store（BeginOitPass 清缓冲）
  MmdGroundShadow = sceneDepth Depth Read +
                    Stencil Write + Stencil Read（mask + fill）
```

## 4. P0-4 — 非 OpenGL RenderDevice 空指针解引用

```text
之前：dynamic_cast<OpenGlRenderDevice*>(device) == nullptr 时仍
      ->RenderCache()（UB），invalid_argument 来不及执行。
现在：先 dynamic_cast 判空，再取 RenderCache；
      non-OpenGL device → clean invalid_argument。
新增 adversarial：FakeNonOpenGlRenderDevice → Renderer(&fake)
      → invalid_argument，绝不 crash。
```

## 5. P0-5 — capabilities 唯一授权

```text
OIT independentBlend：
  device-backed → renderDevice->Capabilities().independentBlend
  （GLAD_GL_ARB_draw_buffers_blend 探测只保留给 Renderer(nullptr)
   legacy OpenGL compatibility path）

Skinning maxSkinningMatrices：
  device-backed → renderDevice->Capabilities().maxSkinningMatrices
  （GL_MAX_* 探测只保留给 legacy path）

OpenGlRenderDevice::RefreshCapabilities 仍是唯一能力来源；
Renderer 不再维护第二套 R2 能力真相源。
```

## 6. 新 adversarial / gates

```text
TestR2NonOpenGlRendererRejection       P0-4（fake device clean reject）
TestR2RenderGraphFrozenSemantics       P0-3：
  - transient read-before-init 拒绝
  - transient lifetime span 正确（并抓到 TransientLifetime lastIndex
    初值 bug：size() → 0，测试先红后绿）
  - 无序 stencil WAW hazard 拒绝
  - Depth/Stencil 不同 aspect 独立（无需序）
  - aspect/kind 不匹配拒绝

Gate A0/A  PASS  render_target/presentation_target/present_surface
                 + render_device 全部 0 GL 编译
Gate B0/B  PASS  OpenGlPresentationTarget 位于 OpenGL backend；
                 WindowManager 提供回调（approved composition bridge）
Gate C     PASS  Runtime/Scene/ModelAsset/checkpoint 零 RenderDevice
Gate D     PASS  ABI 94 + 30
Gate E     PASS  stable headers 零改动
```

## 7. 验证结果

```text
Windows CORE   12/12 PASS
Windows FULL   13/13 PASS（native ABI window → PresentationTarget 真实路径）
WSL CORE       14/14 PASS
WSL FULL       15/15 PASS
ABI            94 legacy + 30 stable
```

## 8. 更新后的 Known-debt ledger

```text
1. Renderer(nullptr) legacy OpenGL compatibility path 保留直接 GL
   capability probing（R2 路径已完全由 RenderDeviceCapabilities 授权）
2. OpenGL pass executor 仍由 Renderer::Execute* 承载（OpenGL
   implementation layer）；Vulkan executor 属于 R2.1
3. 无 parent-commit historical golden byte comparison（证据链不变）
4. GitHub connector 无可见 CI status（本地验证证据）
```

## 9. 裁决

```text
R2.0 0D Final Architecture Review   CLOSED（待 ChatGPT 定点复查）
R2.0 0E backend-neutral boundary    CLOSED（待 ChatGPT 定点复查）
R2.0 FINAL                          待 ChatGPT 最终复审
R2.1 Vulkan                         HOLD（复审通过后授权）
```
