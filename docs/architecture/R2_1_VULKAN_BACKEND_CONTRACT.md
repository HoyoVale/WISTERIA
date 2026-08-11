# R2.1 — Vulkan Device / Resource / Pipeline Backend（契约草案 Phase 0A）

> 状态：**DRAFT — 待用户/ChatGPT 复审后 FROZEN（R2.0 已 FINAL CLOSED，
> baseline `5ccf17e`；R2.1 AUTHORIZED）**
> 前置：R2.0 Backend-neutral Render Architecture CLOSED ✅
> 输入：R2_0_RENDER_ARCHITECTURE_CONTRACT.md（冻结）、
> R2_0_FINAL_ARCHITECTURE_CLOSURE_20260812.md（基线）

## 1. 一句话

在 R2.0 已经建立的 neutral 边界（RenderDevice / RenderFramePacket /
RenderGraph / RenderTarget / PresentSurface）上，新增一个 Vulkan backend：

```text
RenderDevice
├─ OpenGlRenderDevice（已有，R2.0 CLOSED）
└─ VulkanRenderDevice（R2.1 新增）
        ↓
    Vulkan 实现 RenderGraph 解释执行
        ↓
    RenderTarget / PresentationTarget（VkSurfaceKHR + swapchain）
```

R2.1 的目标是让 Vulkan 成为**同构 backend**，而不是第二套 engine
architecture。

## 2. 范围

```text
R2.1 做：
  Vulkan instance/device/queue（single graphics queue）
  VulkanRenderDevice + RenderDeviceCapabilities（engine semantic）
  buffer / texture / sampler / pipeline handles → VkBuffer/VkImage/
  VkSampler/VkPipeline（device-scoped，provenance 复用 R2.0 模式）
  SPIR-V 编译/加载（glslc/glslang 或预编译 SPIR-V asset）
  VulkanGraphExecutor：解释 RenderGraph 数据（OrderedPasses /
    AccessesForPass / Load/Clear/Store / aspects / TransientLifetime）
  Vulkan RenderTarget 实现（offline / headless）
  Vulkan PresentationTarget（VkSurfaceKHR + swapchain + queue present）
  Renderer 正式路径只经 RenderDevice（消除 OpenGL 分支）
  OfflineFrameSequence 身份：RenderBackendId +
    RendererPipelineCompatibilityId（caller-selectable 之前落实）

R2.1 不做（R2.2）：
  Vulkan feature parity（全部 pass/像素回归与 OpenGL 逐项对齐）
  多窗口/多 surface 生产验证
  cross-backend OfflineFrameSequence 混跑
  async / multi-queue / resource aliasing / bindless
  stable C ABI 修改
```

## 3. Entry gates（R2.0 复审带过来的三个 watchpoint）

```text
Gate E1 — Renderer 正式路径只经 RenderDevice：
  当前 Renderer facade 仍持有 OpenGlRenderDevice* / ResolveExecutor()
  （OpenGL compatibility knowledge）。R2.1 启用 Vulkan 前，device-backed
  路径必须只通过 RenderDevice 接口：
    RenderDevice::ExecuteGraph(graph, context)
    RenderDevice::CreatePresentationTarget(surface)
  Renderer 内不得出现 if (OpenGL) / if (Vulkan) 的第二套调度。
  Renderer(nullptr) legacy OpenGL 兼容路径保留为显式 legacy 例外。

Gate E2 — PresentSurface native surface 创建只进 platform bridge：
  neutral PresentSurface 只保留尺寸等 platform-neutral 信息；
  Vulkan 的 VkSurfaceKHR 创建能力放在 platform-specific bridge
  （如 GlfwVulkanPresentSurface），绝不把 HWND/GLFWwindow*/VkSurfaceKHR
  塞回 neutral header（Gate A0 继续 0 GL / 0 Vk）。

Gate E3 — OfflineFrameSequence 身份（caller-selectable 前落实）：
  在 Vulkan 第一次可被调用者选择之前，OfflineFrameSequence/session
  身份加入：
    RenderBackendId
    RendererPipelineCompatibilityId
  防止 OpenGL sequence 与 Vulkan sequence 混跑；
  不进入 ModelAsset / runtime checkpoint fingerprints（R2.0 契约 §13）。
```

## 4. 阶段计划

```text
Phase 0A  本契约 + R2.1 boundary audit（新增代码 inventory）
          Gate：文档复审通过 → FROZEN

Phase 0B  VulkanRenderDevice foundation
          instance/device/queue + capabilities + buffer/texture/sampler/
          pipeline handles（Vulkan 资源真实创建/销毁）
          Gate：neutral 头零 Vk；wrong-device/destroyed handle clean
          拒绝；R2.0 四矩阵不回归

Phase 0C  SPIR-V pipeline 层
          shader module + pipeline layout/bindings + pipeline cache
          （对接 R2.0 PipelineVariantKey / Material pipeline 语义）
          Gate：invalid shader clean 拒绝；pipeline per-device

Phase 0D  VulkanGraphExecutor
          用 RenderGraph 只读视图解释执行当前 8-pass 语义
          （shadow / ground / opaque / skybox / transparent / OIT /
          composite / debug）
          Gate：与 OpenGL 同一 neutral graph 数据源；不做第二套
          scheduler；逐 pass 对照 OpenGL 行为

Phase 0E  offline + presentation + identity
          Vulkan RenderTarget（headless/offline）+ PresentationTarget
          （surface/swapchain/present）+ OfflineFrameSequence 身份
          Gate：四矩阵重闭合 + 单 backend 全量回归

最终：R2.1 Vulkan backend CLOSED → R2.2 parity 授权
```

## 5. 与 R2.0 边界的对应

```text
RenderGraph（backend-neutral）
  ├─ OrderedPasses / PassName / PassDependencies / AccessesForPass
  ├─ ResourceDescriptor / TransientLifetime
  └─ aspects + Load/Clear/Store（R2.0 已建模，Vulkan 直接消费）
        ↓
VulkanRenderDevice::ExecuteGraph
        ↓
VulkanGraphExecutor（device-owned，per graphics domain）
        ↓
VkCommandBuffer 录制 + 提交（single queue）
        ↓
VulkanRenderTarget / VulkanPresentationTarget

资源所有权复用 R2.0 模式：
  handle device-scoped（deviceUid + id）
  Destroy 后逻辑句柄立即不可用
  物理销毁由 backend 延迟到安全点
```

## 6. 关键技术决策（需在 0A 复审拍板）

```text
1. SPIR-V 来源：
   a) glslc/glslangValidator 构建期编译现有 GLSL 到 SPIR-V
   b) 预编译 SPIR-V asset（.spv 文件入库）
   c) runtime 编译（glslang 库）
   建议 a：构建期生成，runtime 只加载；0A 冻结选项。

2. 内存分配：
   a) 直接 vkAllocateMemory + 手动 sub-allocation（无依赖）
   b) VMA 第三方库
   建议 a（本项目零第三方依赖偏好）；0A 冻结选项。

3. 验证层：
   开发/测试构建启用 VK_LAYER_KHRONOS_validation，
   发布构建禁用；环境变量控制。

4. 队列/同步：
   single graphics queue + 每帧 fence/semaphore；
   swapchain 用 present semaphore；
   R2.1 不做 timeline semaphore / multi-queue。

5. 测试环境：
   Windows 真机 + WSL llvmpipe（lavapipe）双平台；
   lavapipe 支持程度决定 0E 的 surface 测试策略。
```

## 7. 验收 gates（延续 R2.0 静态门）

```text
Gate A0/A  PASS  neutral headers 0 GL / 0 Vk
Gate B0/B  PASS  Vk 类型只存在于 backend/opengl 之外的 vulkan backend
                 + approved platform bridge
Gate C     PASS  Runtime/Scene/ModelAsset/checkpoint 零 RenderDevice
Gate D     PASS  ABI 94 legacy + 30 stable
Gate E     PASS  stable headers 零改动
Gate E1-E3 PASS  R2.1 entry gates（上文 §3）
```

## 8. 已知风险

```text
1. lavapipe/WSL 对 surface/swapchain 支持有限：
   0E 若无法在 WSL 验证窗口呈现，退化为 headless/offscreen 验证 +
   Windows 真机窗口验证。
2. SPIR-V 与现有 GLSL 语义（MMD toon / OIT / shadow）转换是最大
   工作量；0D 逐 pass 对照 OpenGL 像素行为。
3. Vulkan SDK 未安装在当前环境时，0B 无法本地构建：
   需要先确认工具链（vulkaninfo/glslc 可用性）。
4. Renderer facade 去 OpenGL 化（Gate E1）会触碰 HeadlessRenderSession /
   WindowManager 组合根；0A 需明确最小改动面。
```

## 9. 下一步

```text
1. 本契约 + R2.1 boundary audit（R2.0 代码 inventory → Vulkan 需要
   新增哪些 backend 接口）提交给用户/ChatGPT 复审。
2. 复审通过 → 0A FROZEN → 0B AUTHORIZED。
3. 0B 前先做环境探针：vulkaninfo / glslc 是否可用，决定构建集成方式。
```
