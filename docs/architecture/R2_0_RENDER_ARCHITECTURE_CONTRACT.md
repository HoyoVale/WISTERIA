# R2.0 — Backend-neutral Render Architecture（契约草案 Phase 0A）

> 状态：**APPROVED — FROZEN ✅（2026-08-10 复审通过；0A Final Micro
> Fix 已合入）→ Phase 0B RenderDevice Foundation AUTHORIZED**
> 前置：R1.9 Final Closure ✅（tag `r1.9-final-closure`）
> 输入：`R1_TO_R2_BOUNDARY_AUDIT.md`（2026-08-10，APPROVED）
> 性质：本阶段只写文档与代码 inventory，不写实现。

## 1. 一句话

把已经成熟的 OpenGL 渲染栈压到一个 backend-neutral 的
`RenderDevice` 边界之下，先把现有 Renderer 的隐式 pass DAG
数据化为 `RenderFramePacket + RenderGraph`，再谈 Vulkan。

> **不要从“写 Vulkan backend”开始 R2。**

## 2. 核心契约（15 条，冻结候选）

```text
1. Runtime / Scene / Checkpoint 位于渲染上游。
   No R2 rendering API may introduce OpenGL/Vulkan concepts into
   IModelRuntimeDriver, ModelInstance, Scene, Entity or checkpoint formats.

2. RenderDevice 是 backend-neutral GPU execution/resource authority。
   No public RenderDevice contract may expose GLuint, GLenum, Vk*,
   native context handles or API-specific synchronization objects.

3. Existing GraphicsDevice is classified as OpenGL backend implementation,
   not as the generic RenderDevice contract.

4. R2.0 supports one RenderDevice and one graphics execution domain per
   Application / offscreen session.
   Multi-device, multi-GPU and async queues are deferred.

5. CPU asset semantics and GPU realizations are separate.
   ModelAsset/Mesh data/Material semantics/TextureData remain device-neutral.
   GPU Buffer/Texture/Pipeline objects belong to a per-device render resource
   cache.

6. Renderer is split into:
   Scene/runtime frame extraction
   → RenderFramePacket
   → RenderGraph construction
   → RenderDevice execution.

7. RenderGraph is an internal frame scheduler, not a Scene graph and not a
   public Stable C ABI.
   It owns logical pass/resource dependencies, not runtime/physics updates.

8. RenderGraph execution must never call runtime Update/Reset/ExactStep.
   Runtime evaluation and PublishCurrentRuntimeFrame happen before packet
   construction.

9. Initial RenderGraph reproduces current OpenGL pass semantics exactly.
   No visual feature redesign is permitted during graph migration.

10. Offline Render compatibility is frozen:
    output = canonical top-left RGBA8,
    captured from SceneColor BEFORE Present / FXAA.

11. Stable Runtime/Render C ABI v1 remains unchanged.
    R2 internals may replace implementation only.
    RenderDevice/RenderGraph are never added to ABI v1.

12. Runtime/checkpoint determinism is render-backend independent.
    R2 does not broaden the deterministic guarantees already validated by R1.
    Same validated backend/execution environment: existing guarantees remain
    authoritative.
    Different render backends: no byte-identical pixel guarantee.
    RenderBackendId + RendererPipelineCompatibilityId prevent incompatible
    sequence mixing; they are not proof that arbitrary devices/drivers
    produce identical pixels.

13. Before selectable multi-backend OfflineFrameSequence is enabled,
    session identity must include a render-backend/render-pipeline
    compatibility identity.
    This identity must not enter ModelAsset or runtime checkpoint fingerprints.

14. IHeadlessContext, GL share groups, current-context tracking, VAO/FBO
    ownership and GL state restoration are OpenGL backend implementation
    details.

15. R2.0 does not include:
    async compute,
    resource aliasing,
    multi-queue scheduling,
    multi-GPU,
    bindless rendering,
    universal shader graph/compiler,
    scene serialization,
    editor APIs.
```

## 3. RenderDevice v1 范围

只覆盖 WISTERIA 当前真实使用的东西，不造全功能 RHI。
下列函数列表是 **conceptual minimum surface**，不是已冻结的 C++
signatures（尤其 `Submit frame work` 在 0B 前还需细化）：

```text
RenderDevice
├─ Capabilities()
├─ CreateBuffer()
├─ CreateTexture()
├─ CreateSampler()
├─ CreateGraphicsPipeline()
├─ UpdateBuffer()
├─ Destroy / retire resource
└─ Submit frame work
```

句柄与描述：

```text
BufferHandle / TextureHandle / SamplerHandle / PipelineHandle
BufferDesc / TextureDesc / SamplerDesc / GraphicsPipelineDesc
```

禁止：

```text
GLuint / GLenum / VkImage / VkBuffer / native context /
API-specific synchronization objects
```

### RenderDevice resource handles（生命周期与 provenance）

```text
- strongly typed backend-neutral value handles
- scoped to exactly one RenderDevice
- never expose backend-native object identity
- wrong-device handle use is an engine contract violation
- Destroy/retire makes the logical handle unusable immediately
- backend may defer physical GPU destruction until safe
- R2.0 is creator-thread / single graphics execution-domain affine
- no thread-safety guarantee
```

具体是 generational integer、slot-map 还是内部 pointer，**0A 不冻结**。

### RenderDeviceCapabilities（第一版现实案例）

```cpp
struct RenderDeviceCapabilities
{
    bool independentBlend = false;
    std::size_t maxSkinningMatrices = 0;

    // 只有真正出现 engine-level 使用需求时再增加：
    // std::size_t maxSampledTexturesPerMaterial;
};
```

原则：

> capability 描述 WISTERIA 所需的语义能力，不镜像底层 API capability
> table。

`maxTextureBufferSize` / `maxTextureUnits` 是 OpenGL texture buffer /
texture unit 模型的概念，未来 Vulkan 可能不存在等价物，因此不进
RenderDeviceCapabilities。上层不再问 GL 扩展/常量，只问
`RenderDeviceCapabilities`。

## 4. 架构边界

```text
                  Runtime Backends
             Saba / Generic / future
                       │
                       ↓
               ModelRenderFrameView
                       │
                    Scene
                       │
                       ↓
              RenderFramePacket
                       │
                       ↓
                  RenderGraph
                       │
                       ↓
                  RenderDevice
                  /          \
                 /            \
        OpenGL Backend      Vulkan Backend
             │                   │
       GL resources         Vk resources
       GL contexts          Vk queue/device
             │                   │
             └───────┬───────────┘
                     ↓
              PresentSurface
                     │
              Window / Headless

对外：
Python / C# / Rust / Node / Agent
                │
                ↓
        Stable C ABI v1
                │
       RenderSession / Runtime
                │
                ↓
       internal R2 architecture
```

## 5. RenderFramePacket（R2.0 定义）

frame-lifetime stable view，不是深拷贝：

```text
RenderFramePacket
├─ Camera
├─ Lights
├─ Environment
├─ DrawItems[]
│   ├─ Mesh
│   ├─ Material
│   ├─ world transform
│   ├─ pose
│   ├─ morph state
│   └─ runtime material override
└─ DebugDrawData
```

唯一硬要求：

> packet 构建结束后，graph execution 不再调用 Runtime Update / Reset /
> ExactStep。

将来 async rendering 再考虑深拷贝和双缓冲 snapshot。

### RenderFramePacket lifetime contract

```text
- packet is constructed after all runtime evaluation/publication for the frame;
- every pointer/span/reference captured by the packet remains valid and
  immutable until graph execution completes;
- Scene/ModelInstance/runtime mutation is forbidden during that interval;
- R2.0 extraction → graph build → execution is synchronous on the creator
  graphics execution domain;
- async/double-buffered packet ownership is deferred.
```

## 6. RenderGraph 职责

负责：

```text
Pass
Resource
Read/Write dependency
Load/Clear/Store
execution order
resource lifetime
hazard validation
```

不负责：

```text
Scene update / Animation / Physics / Runtime exact step
Asset import / Checkpoint
```

不公开到 stable C ABI。

初始 RenderGraph 复现当前 OpenGL pass 语义：

```text
ShadowDepth[4]
       │
       ↓
SceneDepthStencil
       │
       ├── Ground receivers
       ├── MMD Ground Shadow
       │
       ↓
SceneColor
       ├── Opaque
       ├── Skybox
       │
       ↓
OITAccum + OITReveal
       │
       ↓
OIT Composite
       │
       ↓
Physics Debug
       │
       ↓
SceneColor
      /          \
     /            \
Offline        Present
Readback         │
 RGBA8           ↓
             FXAA/PresentSurface
```

第一版不做：

```text
Physical memory aliasing ❌
Async compute ❌
Multi queue ❌
Automatic pass merging ❌
```

做：

```text
Transient resource lifetime tracking ✅
Hazard validation ✅
Pass dependency ✅
```

## 7. CPU asset / GPU realization 拆分

```text
Mesh
CPU semantic asset
├─ vertex data
├─ index data
├─ vertex layout
├─ morph target
└─ bounds

RenderResourceCache（per RenderDevice）
└─ MeshGpuResource
    ├─ vertex buffer
    ├─ index buffer
    └─ backend-specific binding state

TextureAsset → RenderResourceCache → OpenGlTexture / VulkanTexture
MaterialDefinition → PipelineVariantKey → backend pipeline
```

### 动态 geometry 归属（冻结）

```text
Immutable/static geometry:
ModelAsset/Mesh identity
→ may share one per-device GPU realization.

Runtime-deformed geometry:
ModelInstance identity
→ owns or addresses instance-local dynamic GPU state.

A RenderResourceCache must never merge runtime-deformed geometry solely
because two instances reference the same ModelAsset Mesh.
```

这条保证 Generic GPU skinning、Saba CPU skinning 以及未来 runtime
都不会因为共享 per-device MeshGpuResource 而互相污染动态顶点状态。

索引格式语义化：

```cpp
enum class IndexFormat { Uint8, Uint16, Uint32 };
// OpenGL backend: Uint16 → GL_UNSIGNED_SHORT
// Vulkan backend: Uint16 → VK_INDEX_TYPE_UINT16
```

顶点布局抽离：

```text
VertexLayout / VertexAttribute / VertexFormat / VertexSemantic
（当前 Layout 定义在 vbo.hpp 且与 GL wrapper 绑定）
VertexSemantic::MorphPosition / VertexSemantic::MorphUv0 ...
（禁止上层知道 location = 9 / 10）
```

## 8. Material 分层

保留 engine semantics：

```text
PBR Metallic/Roughness
MMD Toon
baseColor / normal / metallic / roughness / emissive / alpha
double-sided / sphere / toon / shadow semantics
```

拆分：

```text
MaterialDefinition（backend-neutral）
  PbrMaterial / MmdToonMaterial
        ↓
PipelineVariantKey
        ↓
backend pipeline
```

不再继续：

```text
Material → GLSL uniform string
```

明确 DEFER：

```text
Universal Shader IR / Shader graph / HLSL→SPIRV→GLSL /
reflection framework / custom shader plugin ABI
```

R2.0 只需要 built-in：

```text
PBR / MMD Toon / shadow / OIT / present
（现有 custom GLSL path 标记为 OpenGL legacy compatibility）
```

## 9. Window / Surface 边界

Backend 选择必须早于 Window 创建：

```cpp
enum class RenderBackend { Auto, OpenGL, Vulkan };

ApplicationConfig
      ↓
select RenderBackend
      ↓
configure native window
```

原因：

```text
OpenGL → GLFW OpenGL context
Vulkan → GLFW_NO_API + VkSurface
```

未来结构：

```text
Platform Window
      │
      ↓
PresentSurface / native presentation endpoint
      │
      └────→ Render backend creates PresentationTarget / Swapchain
                    using RenderDevice + PresentSurface
```

并明确：

```text
OffscreenRenderSession has no PresentSurface requirement.
```

避免未来 Vulkan 被迫模拟“window 属于 device”或“headless 也是 surface”。

Stable ABI 不变：

```text
WisteriaRenderSession
  ↓
OpenGL default implementation（R2 初期保持不动）

未来 caller 选择 backend → WisteriaRenderSessionOptionsV2（additive）
绝不原地给 V1 加字段
```

## 10. 阶段计划

```text
Phase 0A  Contract（本文档）+ R1_TO_R2_BOUNDARY_AUDIT.md
          只写文档与代码 inventory，不写代码

Phase 0B  RenderDevice foundation                ✅ IMPLEMENTED / VALIDATED
          RenderDevice / RenderDeviceCapabilities /
          backend-neutral resource handles/descriptors
          OpenGlRenderDevice（迁移/包入 GraphicsDevice）
          Gate：现有 OpenGL 行为完全不变 +
                No-GL public-header compile test
          （详见 docs/validation/R2_0_PHASE0B_BASELINE_20260810.md）

Phase 0C  CPU asset / GPU realization split
          Mesh / Texture / Material / EnvironmentMap
          Gate：PBR + MMD Toon + Textures + Morph + Skinning +
                Environment + Shadow + OIT 现有 pixel/golden 不变

Phase 0D  RenderFramePacket + RenderGraph
          只表达现有 Pass，不增加新渲染特性
          Gate：old OpenGL renderer output == RenderGraph OpenGL output
                （deterministic fixtures byte-equal）+ stable==engine

Phase 0E  Platform surface + full regression
          PresentSurface（拆分 Window / Renderer / Framebuffer /
          SwapBuffers）
          Gate：四矩阵重新闭合

最终：R2.0 Backend-neutral Render Architecture CLOSED
```

Vulkan 独立成后续阶段：

```text
R2.1  Vulkan device/resource/pipeline backend
R2.2  Vulkan feature parity + window/headless + cross-backend validation
```

## 11. 静态 Gate（单调收敛）

```text
Gate A0 — from 0B:
所有新增 backend-neutral header 禁止 include glad/gl.h。

Gate B0 — from 0B:
不得新增 GL 类型/调用泄漏到既有 allowlist 之外；
GL allowlist 每 Phase 只能缩小，不能扩大。

Gate A — R2.0 final:
所有 backend-neutral public headers 不含 glad/gl.h。

Gate B — R2.0 final:
GLuint / GLenum / gl* 仅允许存在于 OpenGL backend
及明确批准的 platform bridge / compatibility facade。

Gate C  Runtime / Scene / ModelAsset / checkpoint code has zero
        RenderDevice dependency
Gate D  Stable Runtime/Render ABI exported symbol count remains: 30
Gate E  wisteria_stable_runtime.h unchanged
        wisteria_stable_render.h unchanged
```

Gate A0/B0 让迁移具有**单调收敛**性质：不要求 0B 一步完成 0C/0D 的
工作，只要求新边界不扩大 GL 泄漏，旧 allowlist 每 Phase 只能缩小。

运行验证：

```text
Windows CORE / FULL
Linux CORE / FULL
Python ctypes stable ABI
Generic checkpoint cross-process
MMD checkpoint cross-process
Stable Render
OfflineFrameSequence Resume
multi-window
headless
```

## 12. 依赖方向（冻结候选）

```text
wisteria_engine
├─ core
├─ animation
├─ runtime
├─ assets
├─ scene
└─ render contracts

wisteria_render_opengl
├─ glad
└─ OpenGL implementation

wisteria_render_vulkan
└─ Vulkan implementation

wisteria_platform
├─ GLFW
└─ surface integration
```

Phase 0B 不要求一口气拆成多个二进制 target，但**依赖方向先冻结**。

## 13. 明确不做（R2.0 边界）

```text
async compute / job system
resource aliasing / bindless
multi-queue / multi-GPU
universal shader graph / compiler
scene serialization / editor API
raw headless provider C 面
video encoding / FFmpeg / Audio
```

## 14. 验收定义

```text
Normative gates（blocking）:
- Windows/Linux matrices
- Python ctypes stable ABI
- stable exports/layout
- checkpoint/render/sequence regressions
- byte-equal deterministic fixtures（同 backend）
- stable==engine 逐字节

Compatibility evidence（non-blocking）:
- Node N-API smoke

R2.0 成功 = Normative gates 全绿；
外部调用者不需要知道 R2 发生过一次巨大重构
```
