# R2.0 Phase 0D Stage 2B — Graph Registration + Execution Authority 基线（2026-08-11）

> 状态：**PART 1 ARCHITECTURE CLOSURE IMPLEMENTED / VALIDATED
> （sparse DAG + resource semantics + Execute preflight 已闭合；
> RenderPacket 执行迁移待 PART 2）**
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
      sceneDepth  = Depth+External（GroundReceivers Read+Write /
                    MmdGroundShadow Read / Opaque Read+Write /
                    Skybox Read / Transparent Read / PhysicsDebug Read）
      sceneColor  = Color+External（GroundReceivers/MmdGroundShadow/
                    Opaque/Skybox/PhysicsDebug Write；
                    Transparent Write（fallback）或 OitComposite
                    Write（OIT））
      oitAccum/Reveal = Color+Transient（仅 OIT variant）
  - OIT variant：Transparent → oitAccum/Reveal Write；OitComposite
    Read+Write SceneColor
  - fallback variant：Transparent → SceneColor Write；无 OitComposite
  - 稀疏依赖链：主场景链用 lastScenePass 串联，只依赖“上一实际存在
    的 pass”，sparse frame 不再硬编码 Opaque/ShadowDepth
  - shadowDepth 资源仅在 hasShadow 时注册；Opaque 的 shadowDepth 访问
    同样按 hasShadow 条件注册，无光 opaque-only frame 不产生未知资源

测试：TestR2CurrentRenderGraphVariants
  Variant A（OIT）：8 pass + 5 resources，顺序 0..7
  Variant B（fallback）：7 pass + 3 resources，无 OitComposite
  （注入 1 条 physics debug line 使 PhysicsDebug 注册）
```

## 2.5 Architecture Closure（ChatGPT 复审 blocker 闭合）

```text
render_graph.cpp：
  SetPassCallback 拒绝 empty callback（invalid_argument）
  Execute() 先 preflight 全部 callback，全部合法后才逐个执行；
    任何缺 callback / empty callback 都在第一个 pass 执行前失败
    （不再半执行 frame 后抛异常）

render_graph_builder.cpp：
  主场景链改为稀疏依赖：lastScenePass 记录上一实际注册的 pass，
    GroundReceivers/Opaque/Skybox/Transparent/OitComposite/PhysicsDebug
    只依赖实际存在的上一 pass（无 dangling dependency）
  sceneDepth 进入 DAG：
    GroundReceivers Read+Write / MmdGroundShadow Read /
    Opaque Read+Write / Skybox Read / Transparent Read /
    PhysicsDebug Read
  shadowDepth 进入 DAG：
    ShadowDepth Write / MmdGroundShadow Read / Opaque Read
  真实资源语义覆盖（不再只是 pass 顺序，资源访问参与 hazard 校验）

测试：TestR2RenderGraphSparseAndExecution
  skybox-only → 1 pass
  transparent-only fallback → 1 pass
  transparent-only OIT → Transparent → OitComposite（2 pass）
  debug-only → 1 pass
  opaque-only + shadows disabled → 1 pass（无 shadowDepth 未知资源）
  empty frame → 0 pass
  Execute 成功路径：8 个 callback 各执行一次且严格按拓扑序
  Execute preflight：只接第一个 callback → logic_error，且 executed == 0
  empty callback 注册 → invalid_argument
```

## 2. 验证

```text
Windows CORE 12/12、FULL 13/13、WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
（含上述 closure 补丁后的重新构建与全量回归）
```

## 3. Part 2（下轮）

```text
把 Renderer::RenderPacket 的隐式 pass 顺序注册进 graph：
  每段现有 GL 代码块包成 pass callback（ShadowDepth/GroundReceivers/
  MmdGroundShadow/Opaque/Skybox/Transparent/OitComposite/PhysicsDebug）
  → BuildCurrentRenderGraph → Validate → Execute
  → 像素回归必须与现有 RenderPacket 完全一致
```
