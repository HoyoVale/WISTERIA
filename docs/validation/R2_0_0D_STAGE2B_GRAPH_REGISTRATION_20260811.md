# R2.0 Phase 0D Stage 2B — Graph Registration + Execution Authority 基线（2026-08-11）

> 状态：**PART 1 IMPLEMENTED / VALIDATED（builder + variants + execution
> core；RenderPacket 执行迁移待 PART 2）**
> 前置：Stage 2A CLOSED ✅

## 1. Part 1 改动

```text
RenderGraph 执行能力（render_graph.hpp/.cpp）：
  SetPassCallback(pass, std::function<void()>)
  Execute()：按 OrderedPasses() 顺序调用 callback；
    无 callback 的 pass → logic_error

BuildCurrentRenderGraph（render_graph_builder.cpp，新增）：
  RenderFramePacket + RenderGraphBuildOptions（shadows/groundShadow/
  skybox/oit）→ 条件注册“这一帧真正执行的 pass”
  - hasShadow / hasGroundReceivers / hasMmdGroundShadow / hasOpaque /
    hasSkybox / hasTransparent / hasOitComposite / hasPhysicsDebug
    全部来自 packet 内容 + options
  - 不注册不执行的 pass（no-op pass 不存在）
  - 真实资源映射：
      shadowDepth = Depth+Transient（ShadowDepth Write）
      sceneColor  = Color+External（GroundReceivers/MmdGroundShadow/
                    Opaque/Skybox/PhysicsDebug Write；
                    Transparent Write（fallback）或 OitComposite
                    Write（OIT））
      oitAccum/Reveal = Color+Transient（仅 OIT variant）
  - OIT variant：Transparent → oitAccum/Reveal Write；OitComposite
    Read+Write SceneColor
  - fallback variant：Transparent → SceneColor Write；无 OitComposite
  - PhysicsDebug 依赖 {Opaque, OitComposite|Transparent}

测试：TestR2CurrentRenderGraphVariants
  Variant A（OIT）：8 pass + 5 resources，顺序 0..7
  Variant B（fallback）：7 pass + 3 resources，无 OitComposite
  （注入 1 条 physics debug line 使 PhysicsDebug 注册）
```

## 2. 验证

```text
Windows CORE 12/12、FULL 13/13、WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
```

## 3. Part 2（下轮）

```text
把 Renderer::RenderPacket 的隐式 pass 顺序注册进 graph：
  每段现有 GL 代码块包成 pass callback（ShadowDepth/GroundReceivers/
  MmdGroundShadow/Opaque/Skybox/Transparent/OitComposite/PhysicsDebug）
  → BuildCurrentRenderGraph → Validate → Execute
  → 像素回归必须与现有 RenderPacket 完全一致
```
