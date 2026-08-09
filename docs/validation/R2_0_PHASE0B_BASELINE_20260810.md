# R2.0 Phase 0B — RenderDevice Foundation 基线（2026-08-10）

> 状态：**IMPLEMENTED / VALIDATED**；待 ChatGPT 复审后 CLOSED。
> 前置：R2.0 Phase 0A FROZEN（`R2_0_RENDER_ARCHITECTURE_CONTRACT.md`）。
> 范围：只建立 backend-neutral RenderDevice 边界 + OpenGL backend
> 吸收/包装现有 GraphicsDevice。**没有**提前做 0C（Mesh/Texture/
> Material 拆分）、0D（RenderGraph）、0E（PresentSurface）。

## 1. 交付物

```text
include/wisteria/rendering/render_device.hpp        backend-neutral 契约
  RenderDevice（抽象接口）
  RenderDeviceCapabilities（engine semantic）
  BufferHandle / TextureHandle / SamplerHandle / PipelineHandle
  BufferDesc / TextureDesc / SamplerDesc /
  ShaderStageDesc / GraphicsPipelineDesc

src/rendering/backend/opengl/
  open_gl_render_device.hpp/.cpp                    OpenGlRenderDevice
    - 吸收现有 GraphicsDevice（组合，不重造 ownership）
    - 复用 R1.7 context/share-group 删除队列（Buffer/Texture 走
      GraphicsDevice::DeleteResource）
    - 真实 GL 资源创建/更新/销毁（buffer/texture/sampler/pipeline）
    - wrong-device handle 检测（engine contract violation）
    - RefreshCapabilities()：engine semantic 能力（独立 blend、
      maxSkinningMatrices），不镜像 GL 常量表

组合根接入（Renderer → RenderDevice → OpenGlRenderDevice → GL）：
  HeadlessRenderSession：unique_ptr<RenderDevice> + GetRenderDevice()
  Application / WindowManager：SetRenderDevice() + 窗口路径接入
  Renderer 构造签名：GraphicsDevice* → RenderDevice*

tests/render_device_neutral_compile.cpp            Gate A0 编译测试
tests/integration_tests.cpp                        TestR2RenderDeviceFoundation
```

## 2. 实现要点

```text
1. render_device.hpp 完全 backend-neutral：
   0 glad / 0 GL 类型 / 0 Vk 类型（Gate A0 独立 target 证明）
2. handle 是强类型值句柄：
   - scoped to exactly one RenderDevice
   - 不暴露 backend-native 对象身份
   - wrong-device / destroyed handle 使用 → std::invalid_argument
   - Destroy 后逻辑句柄立即不可用
   - 0B 未冻结 generational/slot-map 具体实现（递增 id + 表）
3. OpenGlRenderDevice 组合现有 GraphicsDevice（absorb）：
   - 复用 share-group token / pending delete（R1.7 已验证）
   - LegacyGraphicsDevice() 是 OpenGL-backend-internal 桥
     （不进入 neutral 契约）
4. RenderDeviceCapabilities 只含 engine semantic：
   independentBlend / maxSkinningMatrices；
   maxTextureBufferSize / maxTextureUnits 明确不进（0A 冻结）
5. SubmitFrameWork 未进入 v1 接口：0D 与 RenderGraph 一起定义
6. 0B 没有迁移 Mesh/Texture/Material/Renderer pass（0C/0D 范围）
```

## 3. Gate 验证

```text
Gate A0  PASS：render_device.hpp 零 GL/Vulkan（独立编译 target 不链接
               wisteria_core，仅 std）
Gate B0  PASS：新增 GL 调用（40 处）全部位于
               src/rendering/backend/opengl/open_gl_render_device.cpp；
               既有 allowlist 未扩大
Gate C   PASS：Runtime/Scene/ModelAsset/checkpoint 代码零 RenderDevice 依赖
Gate D   PASS：ABI 94 legacy + 30 stable（导出面不变）
Gate E   PASS：wisteria_stable_runtime.h / wisteria_stable_render.h 零改动
```

## 4. 验证结果（2026-08-10）

```text
Windows CORE：11/11 PASS（+render-device-neutral-compile；integration
  含 TestR2RenderDeviceFoundation）
Windows FULL：12/12 PASS
Linux CORE（WSL，llvmpipe）：13/13 PASS
Linux FULL（WSL，llvmpipe）：14/14 PASS
ABI safety matrix：94 legacy + 30 stable
```

TestR2RenderDeviceFoundation 覆盖：

```text
backend identity（OpenGL）
capabilities 与当前 GL 能力逐项一致（engine semantic）
buffer create/update/destroy
texture / sampler / pipeline create/destroy
invalid shader 显式拒绝
wrong-device handle 检测（两个 session 互用 → invalid_argument）
destroyed handle 立即不可用
R1 离线渲染回归（RenderDevice-backed session 出非零帧）
```

## 5. 复审注意事项

1. RenderDevice 接口没有复制 OpenGL resource model：handle/desc 全部
   engine semantic；OpenGL 细节只存在于 backend 实现。
2. handle 严格 device-scoped：错误 device 使用可检测（测试覆盖）。
3. OpenGlRenderDevice 复用 R1.7 GraphicsDevice（组合 + DeleteResource），
   没有第二套 ownership system。
4. Capabilities 保持 engine semantic（0A 冻结原则）。
5. 没有提前把 Mesh/Material/Renderer 大量迁入 0B（scope creep 检查）。
6. Stable ABI 30 导出、两个 stable header 无变化。
7. 0B 完成状态：
   ```text
   R2.0 Phase 0B  IMPLEMENTED / VALIDATED（待 ChatGPT 复审）
   ```
