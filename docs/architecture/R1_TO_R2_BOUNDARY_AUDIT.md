# R1 → R2 Boundary Audit（2026-08-10）

> 状态：**APPROVED — CLOSED ✅（2026-08-10）**
> 基线：R1.9 Final Closure HEAD `5d62ebd`（tag `r1.9-final-closure`）。
> 审计方：ChatGPT 全栈复审 + Codex 独立复核（本文档合并两轮结论，
> 并附代码 inventory 证据）。
> 性质：只做文档与代码 inventory，**不改实现**。

## 1. 总裁决

```text
R1 → R2 Boundary Audit                    APPROVED ✅
R1 Runtime/Scene/Checkpoint architecture   SUITABLE AS R2 FOUNDATION ✅
Current rendering architecture             OPENGL-SPECIFIC BY DESIGN
                                           MUST BE BACKEND-NEUTRALIZED ⚠️
R2.0 Phase 0A Contract                     AUTHORIZED ▶
R2.0 implementation                        HOLD until contract freeze
```

不需要重开 R1.8/R1.9，也不需要等 R1.7 native-Linux hardware gate。

## 2. 独立复核结论（Codex）

对 ChatGPT 审计中的关键代码声明逐项复核，全部成立：

```text
✅ IModelRuntimeDriver 只产出 geometry/UV/Pose/MorphState/material
   override，backend 不接触 render resources
✅ Scene/Entity 生命周期与图形 API 无关
✅ ModelAsset::DeterministicFingerprint 不含 GPU backend 身份
✅ GraphicsDevice 是 OpenGL resource-lifetime backend（glad/gl.h +
   GLuint + share group + pending delete）
✅ Mesh 混 CPU asset（DefaultModelData/MorphTargets/source indices）
   与 GPU object（VBO/EBO/GraphicsDevice/Attach/Draw）
✅ ModelData::IndexGLType() 把 uint8/16/32 以 GLenum 表达
✅ TextureData 中性但 Texture 持有 GLuint + glTexImage2D
✅ Material 混 engine semantics 与 GLSL uniform/Program/ProgramCache
✅ Renderer 是隐式 hand-written RenderGraph（pass 顺序可观察）
✅ RenderOffline = pre-Present / pre-FXAA SceneColor（R1.6 冻结）
✅ Application/Window 直接持有 GLFW/GL context 权威
✅ IHeadlessContext 是 headless GL context，不应泛化成 IRenderContext
✅ OfflineFrameSequence::SessionIdentity 不含 render backend identity
   （R2 多 backend 前的隐蔽 blocker，见 §6）
✅ CMake wisteria_core PUBLIC glad（任何依赖 core 的目标都依赖 GL loader）
```

## 3. 代码 inventory（数据）

### 3.1 GL 依赖现状

```text
24 个文件直接调用 GL（src/rendering 19 + src/platform 3 + native 1 +
headless_context.hpp 1）
683 处 GL 调用
106 个不同 GL 函数
9 个公共头直接 #include <glad/gl.h>：
  ebo.hpp / environment.hpp / framebuffer.hpp / graphics_device.hpp /
  renderer.hpp / shader.hpp / texture.hpp / vao.hpp / vbo.hpp
GLuint/GLenum 出现在公共 API 签名与成员中
```

### 3.2 关键证据位置

```text
GraphicsDevice（OpenGL backend 事实）
  include/wisteria/rendering/graphics_device.hpp
  src/rendering/graphics_device.cpp

Mesh（CPU/GPU 混合）
  include/wisteria/rendering/mesh.hpp
  src/rendering/mesh.cpp（IndexGLType 在 include/wisteria/rendering/model.hpp:43）

Renderer（隐式 pass DAG + GL state + GLuint 成员）
  include/wisteria/rendering/renderer.hpp
  src/rendering/renderer.cpp / render_passes.cpp

独立 blend 能力查询（上层问 GL 扩展）
  src/rendering/render_passes.cpp:757（GLAD_GL_ARB_draw_buffers_blend）

skinning 能力查询（上层问 GL 常量）
  src/rendering/render_skinning.cpp:14/57（GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS /
  GL_MAX_TEXTURE_BUFFER_SIZE）

morph attribute 硬编码 location 9/10
  src/rendering/render_morphing.cpp:65-66

texture 单元上限（GL 常量进静态缓存）
  src/rendering/texture.cpp:268（GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS）

SessionIdentity（缺 render backend identity）
  src/scene/offline_frame_sequence.cpp:287

CMake 依赖（core 公开依赖 glad）
  CMakeLists.txt:314-315
```

## 4. 分类表（R2 裁决）

| 当前对象 | R2 裁决 |
| --- | --- |
| `IModelRuntimeDriver` | **KEEP / FROZEN** |
| `ModelInstance` | **KEEP** |
| `Scene / Entity` | **KEEP** |
| `ModelAsset` | **KEEP / FROZEN identity** |
| `Camera / Light` | **KEEP** |
| `RenderPart` | **KEEP concept** |
| `OfflineRenderRequest` | **KEEP semantics** |
| `Rgba8Frame` | **KEEP / FROZEN output** |
| `OfflineFrameSequence` | **KEEP orchestration** |
| Stable C ABI v1 | **FROZEN** |
| `GraphicsDevice` | **ABSORB INTO OPENGL BACKEND** |
| `GraphicsContextToken / ShareGroupToken` | **OPENGL INTERNAL** |
| `Mesh` | **SPLIT CPU/GPU** |
| `Texture` | **SPLIT CPU/GPU** |
| `Material` | **SPLIT semantic/pipeline** |
| `EnvironmentMap` | **SPLIT CPU/GPU** |
| `Shader / Program / ProgramCache` | **OPENGL BACKEND / legacy shader layer** |
| `VAO/VBO/EBO/Framebuffer` | **OPENGL BACKEND** |
| `Renderer` | **SPLIT extraction / graph / execution** |
| `RenderStateScope` | **OPENGL BACKEND** |
| `IHeadlessContext` | **OPENGL BACKEND** |
| GLFW share group | **OPENGL BACKEND** |
| `Window` | **KEEP platform concept，移除通用 GL 权威** |
| `Present` | **MOVE TO surface/swapchain abstraction** |

## 5. 冻结的架构边界

```text
1. Runtime / Scene / Checkpoint 永远位于 RenderDevice 之上；
   R2 渲染 API 不得把 OpenGL/Vulkan 概念引入
   IModelRuntimeDriver / ModelInstance / Scene / Entity / checkpoint 格式

2. RenderDevice 是 backend-neutral GPU execution/resource authority；
   公共契约不得暴露 GLuint / GLenum / Vk* / native context / API 同步对象

3. GraphicsDevice = OpenGL backend 实现，不是通用 RenderDevice 契约
   （禁止 if (backend == OpenGL) ... else if (backend == Vulkan) 风格）

4. RenderGraph 不是 SceneGraph：
   Scene = 世界里有什么
   RenderFramePacket = 这一帧准备画什么
   RenderGraph = 这一帧按什么资源依赖和 Pass 顺序执行

5. RenderFramePacket 是 frame-lifetime stable view；
   graph execution 阶段禁止再调用 Runtime Update/Reset/ExactStep

6. ModelAsset fingerprint / Runtime checkpoint identity 与 RenderDevice
   完全独立；不得因进入 R2 把 GPU backend 身份塞进指纹

7. RenderOffline 语义冻结：canonical top-left RGBA8，
   取自 SceneColor、在 Present/FXAA 之前

8. Stable Runtime/Render C ABI v1 不变；RenderDevice/RenderGraph 永不
   进入 ABI v1；Vulkan 先走 C++ experimental backend，
   未来若需要 caller 选择 backend → WisteriaRenderSessionOptionsV2
   （additive，绝不原地加字段）

9. IHeadlessContext / GL share group / current-context tracking /
   VAO/FBO ownership / GL state restoration 全部是 OpenGL backend
   实现细节；未来通用概念是 OffscreenRenderSession，不是
   IRenderContext / HeadlessContext
```

## 6. R2 多 backend 前的隐蔽 blocker

`OfflineFrameSequence::SessionIdentity()` 现在包含：

```text
build compatibility
runtime BackendName
asset fingerprint
width/height
camera
projection
clear color
scenePresentationIdentity
```

**但没有 render backend identity。** 一旦 OpenGL 与 Vulkan 都能输出到
同一个 sequence directory，OpenGL frame 0..100 + Vulkan Resume 101 会被
误判为同一 session。

R2 contract 裁决：

```text
✅ 在 render backend 第一次 caller-selectable 之前，
   SessionIdentity 必须加入 RenderBackendId + RendererPipelineCompatibilityId
❌ 绝不进入 ModelAsset fingerprint / Runtime checkpoint identity
```

## 7. 跨 backend determinism 定义（提前冻结）

```text
Runtime determinism              与 OpenGL/Vulkan 无关，checkpoint 必须一致
Offline rendering                R2 不扩大 R1 已验证的 deterministic
                                 guarantees
Same validated backend/          已有 deterministic guarantees
execution environment            保持权威
Different render backends        不承诺逐字节 RGBA 相同
                                 （shader compiler / rasterization / FP /
                                  sampling / driver 都可能引入差异）
RenderBackendId +                防止不兼容 sequence 混用；
RendererPipelineCompatibilityId  不是“任意设备/驱动像素相同”的证明
Vulkan acceptance                语义 / tolerance golden，而非 GL 字节 == VK 字节
Stable ABI 语义                  RGBA8 / top-left / pre-Present / pre-FXAA 必须一致
```

## 8. 风险矩阵

```text
R1  Renderer 隐式 pass DAG → RenderGraph 改变 draw 顺序 → 像素漂移
     护栏：byte-equal deterministic fixtures + stable==engine 逐字节
R2  context-local（VAO/FBO）vs share-group（Buffer/Texture）所有权
     护栏：OpenGL backend 必须保留 R1.7 已验证的 context/share-group
           双身份所有权语义；该 token 模型不得上升为通用
           RenderDevice contract
R3  ProgramCache 以文件路径为 key → 多 backend 需 pipeline cache 抽象
R4  SceneFramebuffer 持有 GLuint → RenderTarget 抽象保持 RGBA8 语义
R5  glReadPixels 同步读回 → Vulkan queue transfer 语义等价
R6  EGL/GLFW provider → 多 backend provider 扩展，force_software 不变
R7  OIT independent-blend 能力 → RenderDeviceCapabilities.independentBlend
R8  skinning/morph 能力与 attribute 布局 → RenderDeviceCapabilities +
    VertexSemantic（禁止上层知道 location = 9）
R9  SessionIdentity 缺 render backend identity（§6）
R10 公共头 include glad → Vulkan backend 无法并存（Gate A/B）
```

## 9. 与既有 R2 审计文档的关系

本审计是 R1.9 closure 后对 R1 全栈（Runtime → Scene → Asset → Renderer
→ GPU Resource → Window/Headless → Stable ABI）的正式边界审计，作为
`R2_0_RENDER_ARCHITECTURE_CONTRACT.md` 的输入。后续实现阶段由 contract
与本文档共同约束。
