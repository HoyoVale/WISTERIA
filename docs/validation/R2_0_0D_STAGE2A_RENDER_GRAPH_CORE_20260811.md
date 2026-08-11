# R2.0 Phase 0D Stage 2A — RenderGraph Core 基线（2026-08-11）

> 状态：**IMPLEMENTED / VALIDATED（待 ChatGPT 复审）**
> 前置：0D Stage 1 CLOSED ✅

## 1. 改动

```text
include/wisteria/rendering/render_graph.hpp（新增，backend-neutral）
  RenderPassId：ShadowDepth / GroundReceivers / MmdGroundShadow /
    Opaque / Skybox / Transparent / OitComposite / PhysicsDebug /
    SceneColor（= 当前 RenderPacket 隐式 pass 顺序，显式化）
  RenderResourceKind（Color/Depth/Transient）
  RenderResourceAccess（Read/Write）
  RenderPassDescriptor（id + name + dependencies）
  RenderGraph：
    AddPass / AddResource / AddAccess
    Validate()（Kahn 拓扑排序 + 确定性顺序 + 循环/未知/重复拒绝）
    OrderedPasses() / HasPass / PassCount / ResourceCount

src/rendering/render_graph.cpp（新增）
  确定性拓扑序：ready 队列按 RenderPassId 枚举值（priority_queue +
  greater），输出稳定不依赖容器迭代顺序
  - cycle / unknown dependency / duplicate pass / unknown resource
    access → std::invalid_argument

src/rendering/renderer.cpp + renderer.hpp
  Stage 1 watchpoint：RenderPacket 参数改 const RenderFramePacket&
  （frame-lifetime view 在 execution 期间不变）
```

## 2. 验证

```text
TestR2RenderGraphCore：
  9 pass + 5 logical resources + 4 accesses 注册
  拓扑序 == 当前隐式顺序（0..8）
  cycle / unknown dependency / duplicate / unknown resource → 拒绝
Windows CORE 12/12、FULL 13/13、WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
```

## 3. Stage 2A 边界

```text
已做：core types + dependency/order validation（纯类型，不执行 GL）
未做（Stage 2B）：把当前 RenderPacket 隐式顺序注册进 graph 并执行
  回调（仍调现有 Renderer code）
未做（Stage 2C）：pass bodies 迁入 explicit graph passes + 像素回归
0A 冻结：RenderGraph 不 Update Scene/Runtime、不进 stable C ABI、
  无 aliasing/async/multi-queue/pass merging
```

## 4. 非阻塞 watchpoint（登记）

```text
TestR2RenderFramePacketExtraction 的 debug lines 断言可能是 0==0
（场景无真实 debug geometry）；runtime publication 不变由代码审查
保证（Build 只调 const LastRenderFrameView）。Stage 2B 后补真实
debug-geometry fixture 或显式 runtime revision 断言。
```
