# R2.0 Phase 0C — Final Review（2026-08-11）

> 状态：**REVIEW COMPLETE（待 ChatGPT 盖章）**
> 更新（2026-08-11 ChatGPT 复审 `db86e30`）：**R2.0 Phase 0C
> APPROVED — CLOSED ✅；0D AUTHORIZED ▶**
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
  - ShaderInterface uniform contract / Material::GetProgram() /
    Renderer Uniform* 调用迁移到 backend pipeline realization
  - RenderResourceCache 生产接入（Renderer 消费 realization）
  - Mesh::ConfigureVertexArray(VAO&) / EnvironmentMap::
    ConfigureSkyboxVertexArray(VAO&) / DrawSkybox(..., VAO&)
    VAO facade 迁移（public legacy facade → 0D）
  - TextureGpuResource::MaxUnits process-global static cache
    （R1.7 后多 context 假设不成立；改 per-device capability）
  - legacy unmanaged/null-cache GPU path 收口
    （Mesh / Texture / Material / Environment 四类 raw GL fallback）

R2.1（Vulkan）：
  - Vulkan device/resource/pipeline backend
  - ShaderStageDesc source 语言（SPIR-V）

R2.2：
  - Vulkan feature parity + window/headless

独立债务（不阻塞）：
  - R1.7 native-Linux hardware EGL gate（真机验证）
  - EnvironmentHdrImage mutable payload：调用者保留 mutable shared_ptr
    可在 GPU attach 后修改 rgb → 已存在 facade 的 CPU/GPU semantic
    divergence（cache key 旧 hash、GPU 旧 payload）；exact equality
    防住后续错误共享，但防不住已存在 facade 的分歧；
    长期改为真正不可变 CPU asset
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

## 11. Closure Micro-Fix（2026-08-11 ChatGPT 复审 `cb03921` 后）

```text
1. instance-local Mesh ownership 修复：
   - CloneForInstance 记录 clone->cache（resolver authority 与 instance
     realization 一致；不经过 AcquireStaticMesh）
   - attached instance SetRenderCache：
       同 cache → no-op
       不同 cache / nullptr → fail-fast（logic_error）
     realization 保持 A（6A 冻结：instance fixed to one session/device）
   - 头注释同步
2. Material::SetRenderCache(nullptr) 真正解除 subordinate Texture：
   - gpu.reset() + 遍历 textures SetRenderCache(nullptr) +
     programCache.reset()
   - 防 Material 存活超过 device 时 TextureGpuResource 析构解引用
     stale GraphicsDevice*
3. Final Review §6 debt ledger 同步为最终版本：
   - ProgramCount A->B->A 已精确（移除旧条目）
   - null-cache raw GL fallback 扩展四类资源
   - Mesh/Environment VAO facade、Texture MaxUnits 合并进 0D 条目
   - mutable HDR 风险表述更新（已存在 facade 的 CPU/GPU divergence）

测试：
  TestR2InstanceLocalMeshOwnership（same-cache no-op / device migration
    reject / detach reject）
  TestR2MaterialSubordinateTextureDetach（非空 textureSources：detach 后
    cacheA.Clear() → pending 增加——texture facade 不再持有 A realization）
```

### 12. Shared Texture Facade Isolation（2026-08-11 ChatGPT 复审 `33927ac` 后）

```text
Blocker：Material 共享 canonical Texture facade 的 aliasing
  - BuildModelAssetBundle 中多个 Material 引用同一 texture index →
    同一 shared_ptr<Texture>
  - Material rebind/detach 调用 texture->SetRenderCache → 修改共享对象
  - 一个 Material 的 detach 会让 sibling Material 的 Bind 抛异常；
    A→B rebind 会让 sibling 在 A context 下绑定 B realization
    （cross-share-group GPU object use）
  - 现有测试 textureSources.clear() 或 Material 自有 texture 均未覆盖

修复：per-consumer resolver facade
  - Texture::CloneAsBinding()：共享不可变 TextureData、独立 cache/gpu
    状态的 facade
  - Material 最终构造（validation 后）把 bindings 全部克隆为独立
    facade——同 device GPU 仍由 cache 按数据 dedup
  - 结构：CPU Texture semantic → Material A/B 各自 resolver facade
    → RenderResourceCache → 共享 GPU realization

测试：TestR2MaterialSharedTextureIsolation
  - 同一 canonical Texture + Material A/B
  - Device A：A/B Attach、B.Bind 成功
  - A.SetRenderCache(nullptr) → B.Bind 仍成功
  - A rebind Device B + Attach → A.Bind（B）成功
  - 回 A：B.Bind（realization A）成功；到 B：A.Bind（realization B）成功
  - cacheA/cacheB TextureCount 各 == 1（per-device GPU dedup）
```

### 13. 0C 收尾（2026-08-11 ChatGPT 最终盖章 `db86e30`）

```text
R2.0 Phase 0C  APPROVED — CLOSED ✅
R2.0 Phase 0D  RenderFramePacket + RenderGraph AUTHORIZED ▶

非阻塞 watchpoint（登记 0D/asset ownership optimization）：
  Texture resolver facade 当前复制 immutable-equivalent TextureData
  （encoded/RGBA8 的 vector 会复制）；0D asset/packet 边界稳定后改为
  共享 immutable CPU semantic payload
  文档表述：现阶段为“复制相同的 immutable-equivalent TextureData，
  独立 resolver state”
```

## 14. 0D Stage 1 — RenderFramePacket Extraction（开始）

```text
目标：Scene / ModelInstance → RenderFramePacket → 现有 Renderer
  RenderFramePacket 只描述“这一帧要渲染什么”：
    Scene traversal / Entity visibility / world transform /
    ModelRenderFrameView / pose / morph state / runtime material override /
    opaque-transparent classification inputs / Camera / Lights / Environment
  不执行 GL、不 Update Runtime、不创建 RenderGraph、
  不改变 pass 顺序、不改变像素结果
验证：Packet + 旧 Renderer 像素回归完全一致后，再进 Stage 2（RenderGraph）
```
