# R2.0 Phase 0D Stage 2B — Graph Registration + Execution Authority 基线（2026-08-11）

> 状态：**PART 1 CLOSED ✅ / PART 2 IMPLEMENTED / VALIDATED
> （RenderPacket → Explicit RenderGraph Execution Migration 已完成；
> 等待 ChatGPT 迁移代码审查 + 像素回归审查）**
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
    ShadowDepth Write / GroundReceivers Read / Opaque Read /
    Transparent Read（均仅 hasShadow 时注册）
    MmdGroundShadow 不 Read shadowDepth（planar projection 不采样
    CSM shadow map，仅保持 pass order 依赖）
  alpha-blending 读目标色：
    MmdGroundShadow / fallback Transparent / OitComposite 对
    SceneColor 均为 Read + Write（blending 需要 destination color）
  真实资源语义覆盖（不再只是 pass 顺序，资源访问参与 hazard 校验）

测试：TestR2RenderGraphSparseAndExecution
  已注册进 main()（此前仅编译未执行，现已真正运行）
  skybox-only → 1 pass
  transparent-only fallback → 1 pass
  transparent-only OIT → Transparent → OitComposite（2 pass）
  debug-only → 1 pass
  opaque-only + shadows disabled → GroundReceivers → Opaque
    （2 pass；无 ShadowDepth / MmdGroundShadow；ground-receiver 循环
    与 shadowsEnabled 无关，与 Renderer::RenderPacket 语义一致）
  empty frame → 0 pass
  Execute 成功路径：8 个 callback 各执行一次且严格按拓扑序
    （场景含 opaque + transparent + debug line + skybox + directional
    light，完整覆盖 8-pass DAG）
  Execute preflight：只接第一个 callback → logic_error，且 executed == 0
  empty callback 注册 → invalid_argument
```

## 2. 验证

```text
Windows CORE 12/12、FULL 13/13、WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
（含上述 closure 补丁后的重新构建与全量回归）
```

## 3. Part 2 — RenderPacket → Explicit RenderGraph Execution Migration

```text
Renderer::RenderPacket 现在的执行链：
  RenderStateScope frameState            ← frame-level RAII 保留
  target.Bind / draw buffer              ← frame setup 保留
  shadowMapSize / PCF / bias 解析        ← CPU config 保留
  ScopedDepthState depthState            ← frame-level depth 边界保留
  BuildCurrentRenderGraph(packet, options)
  → 按 graph.HasPass 条件 SetPassCallback
  → graph.Execute()

options 来自真实运行条件（不再重复手写 if 链）：
  shadowsEnabled =
      config.shadowsEnabled && !WISTERIA_DISABLE_SHADOWS
  groundShadowEnabled =
      config.groundShadowEnabled && !WISTERIA_DISABLE_GROUND_SHADOW
  skyboxEnabled = !WISTERIA_DISABLE_SKYBOX
  oitEnabled    = !WISTERIA_DISABLE_OIT
  builder 再结合 packet 内容裁剪实际 pass

8 个 callback 全部包装“现有 GL 代码块”：
  ShadowDepth      → cascade 计算 + RenderShadowPass + 恢复 target
  GroundReceivers  → polygon-offset ground/receiver 循环
  MmdGroundShadow  → RenderGroundShadowPass
  Opaque           → 非地面 opaque 循环
  Skybox           → environment->DrawSkybox
  Transparent      → OIT 绘制（EnsureOitResources + BeginOitPass +
                     accum/reveal 两轮 draw）或 fallback alpha blend
  OitComposite     → CompositeOit（从 Transparent callback 拆出，
                     对应 DAG 的独立 composite pass）
  PhysicsDebug     → DrawPhysicsDebug

未改动的边界（0A/0D 冻结）：
  - 不重写 pass body / shader / RenderDevice
  - 不提前拆状态生命周期：RenderStateScope、ScopedDepthState、
    target/frame setup 全部保持 frame-level
  - CompositeOit 内部的 RenderStateScope 原样保留
  - 无 GL 泄漏、stable ABI 30 符号不变

新增测试：TestR2RenderPacketGraphExecution
  - 真实 HeadlessRenderSession + RenderOffline（完整帧：directional
    light + opaque + Blend transparent + environment skybox）
  - 同一场景连续两次渲染逐字节一致（graph 路径确定性）
  - 非空帧断言（ShadowDepth/GroundReceivers/MmdGroundShadow/Opaque/
    Skybox/Transparent/OitComposite 真实执行）
  - WISTERIA_DISABLE_OIT=1 fallback（RAII 环境变量守卫）渲染非空
```

## 4. Part 2 验证

```text
Windows CORE   12/12 PASS
Windows FULL   13/13 PASS
WSL CORE       14/14 PASS
WSL FULL       15/15 PASS
ABI            94 legacy + 30 stable

像素回归证据：
  R1.9 stable render pixels match engine（stable ABI vs engine 逐字节一致）
  R2.0 render packet graph execution（graph 路径重复渲染逐字节一致）
  R2.0 render device foundation（RenderDevice session 渲染非空）
  render-fbo / headless-smoke / headless-smoke-glfw-fallback
```
