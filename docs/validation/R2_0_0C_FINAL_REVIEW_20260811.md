# R2.0 Phase 0C — Final Review（2026-08-11）

> 状态：**REVIEW COMPLETE（待 ChatGPT 盖章）**
> 前置：6A CLOSED ✅ / P0-2 CLOSED ✅ / Decode Split CLOSED ✅ /
> 6B CLOSED ✅ / Step 7 CLOSED ✅

## 1. Neutral Boundary（六块检查 1）

```text
CPU Mesh / Texture / Material / Environment 公共数据：
  model.hpp / mesh.hpp / texture.hpp / material.hpp / environment.hpp /
  vertex_layout.hpp / pipeline_variant.hpp / shader_path.hpp
  → 零 glad include、零 GL 类型/函数（仅注释提及 GL 名称）
  → render-assets-neutral-compile + render-device-neutral-compile 双 Gate PASS
Material 依赖 pipeline_variant.hpp（neutral），不依赖 RenderDevice
```

## 2. Per-device Realization（六块检查 2）

```text
Mesh      → RenderResourceCache::AcquireStaticMesh（共享）/ instance-local
Texture   → AcquireTexture（共享）/ instance-local placeholder
Environment → AcquireEnvironment（共享，identity = source payload +
             generation params）
Material  → device-local ProgramCache（构造/SetRenderCache 从
             cache->Device().Programs() 重新解析）+ texture cache
统一模型：CPU semantic → validate → cache resolution → backend realization
```

## 3. Identity / Lifetime / Provenance（六块检查 3）

```text
cache identity：hash/descriptor 是 accelerator，exact equality 是权威
  （Mesh/Texture/Environment 全部）
A→B→A：Mesh/Texture/Environment/Material 均验证跨 device 重新解析
wrong-device / wrong-share-group：Environment/Material Attach 前拒绝
share-group vs context-local：R1.7 DeleteResource 分轨
Clear/destruction：cache 唯一 owner 时释放；facade 持有期不清杀
```

## 4. Material Semantic Boundary（六块检查 4）

```text
PipelineVariant 权威：PBR/MMD 内置 shader；Custom = legacy GLSL
shadingModel ↔ variant 一致性 fail-fast；pass-level variants 拒绝；
reserved flags 拒绝
明确冻结到 0D 的 OpenGL coupling：
  ShaderInterface uniform-name contract
  Renderer 的 GetProgram() / Uniform* 调用
  Material::GetProgram()（OpenGL legacy facade）
```

## 5. Static Gates（六块检查 5）

```text
Gate A0  render-assets/device-neutral-compile        PASS
Gate B0  本轮新增 GL 调用全部位于 backend/opengl     PASS
         （native_window.cpp 为 legacy ABI approved facade）
Gate C   Runtime/Scene/Checkpoint 零 RenderDevice    PASS
         assets/manager 仅前向声明 RenderResourceCache
         （asset 装配层允许的 cache resolver 关联）
Gate D   ABI 94 legacy + 30 stable                    PASS
Gate E   wisteria_stable_runtime.h / render.h 零改动  PASS
```

## 6. Known-Debt Ledger（六块检查 6）

```text
0D（RenderFramePacket/RenderGraph）：
  - Renderer 隐式 pass DAG 数据化
  - ShaderInterface / GetProgram / Uniform* 迁移到 backend pipeline
  - RenderResourceCache 生产接入（Renderer 消费 realization）
  - ProgramCount A->B->A 测试可再精确（非阻塞）

R2.1（Vulkan）：
  - Vulkan device/resource/pipeline backend
  - ShaderStageDesc source 语言（SPIR-V）

R2.2：
  - Vulkan feature parity + window/headless

独立债务（不阻塞）：
  - R1.7 native-Linux hardware EGL gate（真机验证）
  - EnvironmentHdrImage mutable payload（0C Final Review watchpoint：
    调用者保留 mutable shared_ptr 可改 rgb → 重复 realization/stale
    identity；exact equality 已防错误共享，长期改为真正不可变）
  - Environment null-cache raw GL fallback（0C Final Review 决定：
    ResourceManager 正式路径已传 cache；fallback 保留待 0D 收口）
```

## 7. Final Review 改动（本阶段）

```text
RenderResourceCache invariant 强制：
  构造改 GraphicsDevice&（引用，null 不可能）；
  Device() 返回 GraphicsDevice&
  （Material/Environment 使用点同步）
测试精度：
  A->B->A ProgramCount 精确 before/after（A +1、B +1、回 A 不变）；
  wrong-share-group 拒绝后 A/B ProgramCount 均不变
```

## 8. 验证

```text
Windows CORE 12/12、FULL 13/13
WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
```

## 9. 状态

```text
R2.0 Phase 0C   REVIEW COMPLETE（待 ChatGPT 盖章）
R2.0 Phase 0D   HOLD
```

## 10. Final Closure（2026-08-11 ChatGPT 复审 `5544d16` 后）

```text
P0-1 Mesh construction transaction：
  全部 CPU validation（skinning/layout/bounds/bone/morph）移到
  AcquireStaticMesh 之前；invalid skinning/morph → StaticMeshCount 不变
P0-2 Mesh/Texture creation provenance：
  MeshGpuResource::Attach / TextureGpuResource::UploadDecodedPixels 在
  任何 GL work 前 RequireShareGroupToken + current context 检查；
  wrong-share-group → logic_error（A/B pending 无副作用）
P1-1 SetRenderCache(nullptr) 统一：
  Mesh/Texture/Material 统一为“facade 不绑定任何 device realization”
  （detach）；A->nullptr->A 重新绑定/重 attach gate
P1-2 CloneForInstance 不再经过 static cache：
  cache-free CPU clone → instanceLocal → CreateInstanceMesh
P1-3 RenderResourceCache 禁 copy/move（one cache ↔ one device lifetime）
P1-4 文档/头注释修正：
  - SetRenderCache 头注释不再称 “No-op once attached”
  - null-cache debt 扩展四类资源（Mesh/Texture/Material/Environment）
  - Mesh::ConfigureVertexArray / EnvironmentMap VAO facade → 0D debt
  - mutable HDR 风险表述（已存在 facade 的 CPU/GPU semantic divergence）
  - Texture MaxUnits process-global static cache 登记（per-device 化
    候选；R1.7 后多 context 假设不成立）

测试：TestR2ConstructionAndProvenanceClosure
  （invalid skinning/morph count unchanged、Mesh/Texture wrong-share-group、
   A->nullptr->A rebind）
render_fbo deferred-delete 测试语义修正：
  owning share-group 创建 → non-owning 销毁 → pending
  （原测试在 non-owning 状态下创建，正是 provenance gate 要禁止的）
```

验证：

```text
Windows CORE 12/12、FULL 13/13、WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
```
