# R2.0 Phase 0D Stage 1 — RenderFramePacket Extraction 基线（2026-08-11）

> 状态：**IMPLEMENTED / VALIDATED（待 ChatGPT 复审）**
> 前置：R2.0 Phase 0C CLOSED ✅
> 目标：Scene / ModelInstance → RenderFramePacket → 现有 Renderer

## 1. 改动

```text
include/wisteria/rendering/render_frame_packet.hpp（新增）
  RenderCommand（从 renderer_internal.hpp 提升）：
    part / model / pose / morphState / resolved material
  RenderFramePacket：
    camera（值拷贝）+ projection
    opaqueDraws / transparentDraws（frame-lifetime view）
    directionalLights / pointLights / spotLights（span 引用）
    environment（指针）
  BuildRenderFramePacket(Scene&, Camera, projection)

src/rendering/render_frame_packet.cpp（新增）
  EvaluateMaterialMorphs / ResolveMaterialState / EffectiveAlphaMode
    （从 renderer_internal.hpp 迁入，wisteria scope）
  BuildRenderFramePacket：纯 CPU extraction
    - Scene traversal / visibility / world transform
    - ModelRenderFrameView / pose / morph state
    - runtime material override 解析
    - opaque/transparent 分类
    - 不执行 GL、不 Update Runtime、不建 RenderGraph

src/rendering/renderer_internal.hpp
  三个 material-state 函数声明保留（legacy renderer TU 调用）
  RenderCommand 定义移除（由 packet 头提供）

src/rendering/renderer.cpp
  Render() 开头改为：
    RenderFramePacket packet = BuildRenderFramePacket(...)
    使用 packet.opaqueDraws / transparentDraws / directionalLights /
    environment
  其余 pass 顺序与像素路径完全不变
```

## 2. 验证（像素回归 = packet 无行为变化）

```text
Windows CORE 12/12、FULL 13/13
WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
integration 包含：stable==engine 逐字节、frame3!=frame30、
IBL/影子/OIT 像素路径——全部保持绿色
```

## 3. Stage 1 边界

```text
已做：frame data extraction 显式化（纯 CPU）
未做：RenderGraph、pass 顺序变化、Renderer pass 重构、
      RenderResourceCache 生产接入（消费 realization）
下一步 Stage 2：把现有隐式 pass DAG 显式化为 RenderGraph
```
