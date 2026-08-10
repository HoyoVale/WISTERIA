# R2.0 Phase 0C — CPU Asset / GPU Realization Split 基线（2026-08-10）

> 状态：**Step 1..7 IMPLEMENTED / VALIDATED（0C 主体完成，待 ChatGPT 复审）**
> 更新（2026-08-10 复审后 6A/6B/Step 7 重开）：
> **6A Neutral Asset Boundary 大部分完成**，6B/Step 7 剩余项见 §9。
> 前置：R2.0 0B CLOSED（`d05f2a6`）。
> 范围：Mesh / Texture / Material / EnvironmentMap 的 CPU asset 与 GPU
> realization 分离；per-device RenderResourceCache；动态 geometry 实例隔离。

## 1. Step 1 — Mesh CPU/GPU 物理拆分（已完成）

### 改动

```text
src/rendering/backend/opengl/mesh_gpu_resource.hpp/.cpp（新增）
  MeshGpuResource：
  - VBO/EBO 所有权（per-instance realization）
  - Attach / ConfigureVertexArray / Draw / UploadDynamicFrame 的 GL 逻辑
  - device 可空（stable render 组合路径延迟 attach，旧 VBO 行为）

include/wisteria/rendering/mesh.hpp + src/rendering/mesh.cpp
  Mesh = CPU semantic asset：
  - 保留 DefaultModelData / morph targets / source indices / bounds /
    skinning debug 数据 / RebuildInterleavedVertices（纯 CPU）
  - GPU 成员（vbo/ebo/attached）移除；
    持有 unique_ptr<MeshGpuResource>（前向声明，不 include backend 头）
  - Attach/Configure/Draw/Upload 转发到 realization
  - CloneForInstance：每个实例 clone 创建独立 realization
    （runtime-deformed geometry 永不跨实例共享）
  - device 引用保留仅用于 clone 时创建新 realization（0C 过渡）

CMakeLists.txt：+mesh_gpu_resource.cpp
tests/integration_tests.cpp：+TestR2MeshGpuRealizationSplit
```

### 验证

```text
TestR2MeshGpuRealizationSplit：
  - asset 数据构造后不变
  - 两个 clone 有独立 lifetime token（实例隔离）
  - 各自 Attach 成功
  - 各自 UploadDynamicFrame（不同位置）后 CPU asset 数据不变
  - vertex count 正确

四矩阵回归（R1 像素行为不变）：
  Windows CORE 11/11、Windows FULL 12/12
  WSL CORE 13/13、WSL FULL 14/14
  ABI 94 legacy + 30 stable
```

## 2. Step 6 — RenderResourceCache（已完成）

```text
src/rendering/backend/opengl/render_resource_cache.hpp/.cpp（新增）
  RenderResourceCache（per RenderDevice）：
  - AcquireTexture：TextureData 身份（file path / payload FNV-1a）→
    共享 TextureGpuResource
  - AcquireStaticMesh：vertices/indices/layout FNV-1a → 共享
    MeshGpuResource
  - Clear / TextureCount / StaticMeshCount

OpenGlRenderDevice：持有 renderCache + RenderCache()（OpenGL internal）
HeadlessRenderSession：GetRenderCache()（OpenGL backend accessor）

Mesh / Texture：
  - 构造新增 RenderResourceCache* cache = nullptr：
    cache 非空 → 共享 realization（shared_ptr）；
    cache 为空 → instance-local（默认，所有现有调用不变）
  - CloneForInstance 不传 cache → 动态实例永远独立

tests：TestR2RenderResourceCache
  - 相同 TextureData → 1 个 realization，Attach 后两 Texture 均 attached
  - 不同 payload → 2 个 realization
  - 相同静态 Mesh 数据 → 1 个 realization，Attach 后共享
  - CloneForInstance → 不进入 cache（StaticMeshCount 保持 1）
```

生产接入点说明：ResourceManager / ModelAssetBundle / stable entity 创建
资产时还没有 device/session 关联（entity 创建早于 render session），
因此 0C 内共享 cache 只作为能力 + 验证面存在；生产路径接入随 0D
（Renderer 迁移）一起完成。动态隔离在 0C 已由 CloneForInstance 保证。

## 3. Step 7 — ShaderStageDesc 再审（已完成）

```text
include/wisteria/rendering/render_device.hpp
  - ShaderStageDesc.source 注释强化：
    source 语言由 backend 拥有（OpenGL=GLSL / R2.1 Vulkan=SPIR-V）；
    neutral 层禁止 if (OpenGL) GLSL else SPIR-V 分支
  - 新增 PipelineVariant / PipelineVariantKey：
    PbrMetallicRoughness / MmdToon / ShadowDepth / GroundShadow /
    Skybox / OitComposite / Present
    0D RenderGraph/pipeline realization 据此选择 backend pipeline，
    不再通过 neutral 层运送 GLSL
  - 0B/0C 保持 GraphicsPipelineDesc.stages 为工作面（不破坏现有）
```

裁决记录：shader/pipeline realization 属于 backend；neutral 层只持有
语义 variant key。这是 0A 冻结 watchpoint 的正式关闭条件。

## 9. 复审后重开（2026-08-10）：6A Neutral Asset Boundary

ChatGPT 复审 `f6c29f6`：Steps 1-5 物理拆分 APPROVED，但架构闭合
NOT YET。按 6A → 6B → Step 7 路线执行。

### 6A 已完成（本基线更新）

```text
include/wisteria/rendering/vertex_layout.hpp（新增，0 glad）
  VertexFormat（Float32/Int32/Uint32/Uint8）
  VertexSemantic（Position/Normal/TexCoord/BoneIndices/BoneWeights/
                  MorphPosition/MorphUv0/Custom）
  VertexAttribute / VertexLayout
  legacy 别名：Layout/DataType/FLOAT/INT/UINT/UCHAR（兼容旧代码）

vbo.hpp：Layout/DataType 定义移出（OpenGL wrapper 只留 VBO）
model.hpp / mesh.hpp：脱离 vbo.hpp/ebo.hpp（include vertex_layout.hpp）
shader_path.hpp（新增）：Path（VertexPath/FragmentPath）移出 GL 头
material.hpp：脱离 shader.hpp/program_cache.hpp（前向声明）
texture.hpp：移除 glad/GLuint/GetTexture
environment.hpp：移除 glad

Mesh/Texture/Material 构造：GraphicsDevice* → RenderResourceCache*
  - cache 非空 → 共享 realization（AcquireStatic/Texture）
  - cache 空 → instance-local placeholder（device null，延迟 Attach）
  - SetRenderCache()：首次 GPU touch 前由 Renderer/stable 路径绑定
  - Mesh 显式 instanceLocal 标记（P0-4：不推断自运行状态）
RenderResourceCache：持 GraphicsDevice*；AcquireTexture/AcquireStaticMesh
  （共享）+ CreateInstanceTexture/CreateInstanceMesh（独立不缓存）
ResourceManager：SetRenderCache()；创建资产传 cache
组合根：HeadlessRenderSession/Application 绑定 resources cache；
  Application::Shutdown 在 context 有效窗口内 Clear cache
Renderer：持有 renderCache，DrawPart/shadow 路径在 Attach 前
  SetRenderCache（stable/engine 资产可延迟绑定）
stable render：BindEntityRenderCache() 三条路径绑定 session cache

静态 Gate：
  wisteria.render-assets-neutral-compile（新增，PASS）
  只 include model/mesh/texture/material/environment + 不链接 glad
```

### 验证（6A 更新后）

```text
Windows CORE 12/12、Windows FULL 13/13
WSL CORE 14/14、WSL FULL 15/15
ABI 94 legacy + 30 stable
render-assets-neutral-compile / render-device-neutral-compile PASS
```

### 6A/6B/Step 7 剩余项（下轮）

```text
P0-2 Environment GPU lifetime 接 R1.7：
  EnvironmentMapGpuResource 增加 GraphicsDevice/RenderResourceCache；
  shared 资源（texture/buffer/program）走 DeleteResource；
  context-local（VAO/FBO）按 owner 分离或 cache key 含 context
Environment CPU decode 分离：
  文件 IO + stb_image HDR decode 移出 OpenGL backend
Step 7 pipeline semantic cleanup：
  MaterialData.shaderFilePath/ShaderInterface/uniform 契约 →
  PipelineVariantKey；legacy custom GLSL 关进 OpenGL legacy compatibility
6B adversarial gates：
  同 static Mesh 两个 RenderDevice → realization 不同
  CreateTextureShared → 非 owning context destroy → pending delete
  Environment shared/context-local ownership 分别验证
```

## 10. 6A Final Boundary Fix（2026-08-10 ChatGPT 复审 `eda930e` 后）

```text
Blocker A（per-device realization ownership）：
  Mesh/Texture resource realization 的 SetRenderCache 允许跨 device 切换：
  - asset（static）：重新解析当前 cache 的共享 realization，
    即使另一 device 的 realization 已 attach（旧 cache 持有旧
    realization，A/B GPU state 不共享）
  - instance（runtime-deformed）：realization 固定独立
    （instance 生命周期绑定一个 render session/device）
  新增 cross-device Mesh 测试：Device A attach 后 Device B 重新解析，
  两个 cache 各 1 个 realization
  Material 边界：public neutral header 完成；
  Material pipeline/program per-device realization 明确留待 Step 7
  （ProgramCache 目前仍来自首次绑定 device——见 §10 注）

Blocker B（Texture cache identity）：
  TextureKey 加入 TextureColorSpace（file: path+cs /
  payload: hash+cs / RGBA: hash+cs+WxH）
  新增 adversarial：same pixels + Linear / sRGB → 2 realizations

Micro 1：IndexFormat 从 render_device.hpp 下沉到 vertex_layout.hpp；
        model.hpp 不再 include render_device.hpp
Micro 2：mesh.hpp 残留 GraphicsDevice 前向声明删除
```

### 6A Final Fix 验证

```text
Windows CORE 12/12、Windows FULL 13/13
WSL CORE 14/14、WSL FULL 15/15
ABI 94 legacy + 30 stable
render-assets-neutral-compile PASS（model 不再依赖 RenderDevice）
```

### 注：Material per-device 口径（2026-08-10 ChatGPT 复审 `b4a3ad3`）

Material 的 `programCache` 来自 `ResourceManager::BindGraphicsDevice()`，
即首次绑定 device 的 ProgramCache；`SetRenderCache(B)` 会重建
MaterialGpuResource，但 program 仍从原 ProgramCache acquire。
因此 **Material pipeline per-device realization 尚未成立**，属于
Step 7（Material/Pipeline semantic cleanup）范围，不计入 6A。

## 11. 6A Closure Micro Fix（2026-08-10 ChatGPT 复审 `b4a3ad3` 后）

```text
1. cross-device Mesh 测试 current-context 顺序修正：
   A.MakeCurrent → resolve/attach A
   → B.MakeCurrent → resolve/attach B
   → A.MakeCurrent → reacquire A（A->B->A 回环验证）
   （原测试在 B context current 下执行 A attach，GL buffer 可能创建在
     B share group，测试未证明其声称的东西）
2. 文档口径：6A = Mesh/Texture resource realization ownership；
   Material public neutral boundary 完成、pipeline realization → Step 7
3. 非阻塞记录：Texture::SetRenderCache(nullptr) 与 Mesh 行为不一致
   （保留最后 realization vs 清空）；后续统一
   SetRenderCache(nullptr) 语义（detach resolver / 保留 realization）
```

## 12. P0-2 — Environment GPU Lifetime（2026-08-10，6A CLOSED 后）

```text
P0-2.1 shared resource ownership → R1.7：
  EnvironmentMapGpuResource 增加 GraphicsDevice*（来自 RenderResourceCache）
  shared 资源（environment/irradiance/prefilter cubemap、brdfLut、
  cubeVbo/quadVbo、captureRenderbuffer、skyboxProgram）全部经
  GraphicsDevice::DeleteResource：
    owning share-group current → immediate
    not current → pending delete（复用 R1.7 队列，不重造）
  注：managed RenderResourceCache path 一律走 GraphicsDevice；
  legacy/unmanaged null-cache compatibility path 暂保留 raw GL lifetime
  （0C Final Review 再决定去留）
  Program::TakeProgram()：Environment 把 program 所有权交给
  DeleteResource（避免 Program 析构 raw glDelete 双重删除）
  skybox shader 在 link 后立即释放（Attach 时 owning context current）

P0-2.2 context-local ownership：
  cubeVao/quadVao/captureFramebuffer 记录创建时 exact context
  （GraphicsDevice::CurrentContext()），销毁经
  DeleteResource(kind, name, owningContext)；
  sibling context 永不删除（R1.7 ContextLocalIsCurrent 判定）
  注：captureRenderbuffer 按 GL 规范属于 share-group shared
  （GraphicsDevice::IsSharedResource 已含 Renderbuffer），走 shared 队列

P0-2.3 Attach rollback transaction：
  现有 try/catch Release() 保留；Release 按新 ownership 分轨清理

P0-2.4 adversarial tests（TestR2EnvironmentGpuLifetime）：
  1. owning context current 销毁 → immediate，PendingDeleteCount == 0
  2. 另一独立 session current 时析构 → remaining shared resources 进入
     owning device pending queue（成功 Attach 已释放 capture FBO/VAO 等
     临时 context-local 对象；context-local correctness 由 DeleteResource
     contract + partial-construction rollback 路径验证）
  3. 另一 session flush A 队列 → 不释放（非 A share group）
  4. owning flush → 全部清空
  5. corrupted HDR attach 失败 → 临时 GPU allocation（geometry/FBO/RBO/
     cubemap）之后 decode 抛错 → 原子回滚，无 pending 残留
  6. wrong-share-group attach（cache A + session B current）→ logic_error
     → 无 partial realization → A current 后 attach 成功

P0-2.5 四矩阵 + ABI + IBL 回归：
  Windows CORE 12/12、FULL 13/13、WSL CORE 14/14、FULL 15/15
  ABI 94 legacy + 30 stable
```

明确不做（P0-2 边界）：HDR decode 拆分（下阶段）、Environment
RenderResourceCache dedup（6B）、PipelineVariant（Step 7）。

### P0-2 Closure Micro Fix（2026-08-10 ChatGPT 复审 `b99573d` 后）

```text
1. EnvironmentMapGpuResource::Attach() 在任何 GL work 前验证：
   device->RequireShareGroupToken(GraphicsDevice::CurrentShareGroup()) +
   current context 非 null（creation provenance == deletion provenance）
2. 新增 adversarial：cache A + session B current → Attach 拒绝
   （logic_error）→ 无 partial / pending → A current 后 Attach 成功
3. 文档口径：
   - 两个独立 headless sessions（不同 share group），不称 sibling
   - corrupted HDR = partial GPU allocation 后 rollback
   - DeleteResource 统一限定 managed cache path；
     null-cache raw GL fallback 保留至 0C Final Review
```

## 13. Environment CPU Decode Split（2026-08-10，P0-2 CLOSED 后）

```text
include/wisteria/rendering/environment.hpp
  - EnvironmentHdrImage（neutral）：width/height + tightly packed RGB floats
  - EnvironmentMapData.equirectangularImage（shared_ptr<const>）：
    CPU source；path 降级为 provenance/diagnostic metadata
  - DecodeEquirectangularHdr() 声明（CPU preparation）

src/rendering/environment_decode.cpp（新增）
  - ReadBinaryFile + stbi_loadf_from_memory 从 OpenGL backend 移出

src/rendering/environment.cpp
  - PrepareEnvironmentData()：构造时 CPU decode（file IO + HDR）
    → 失败在 CPU preparation 阶段（构造抛）

src/rendering/backend/opengl/environment_gpu_resource.cpp
  - CreateEquirectangularCubemap 只接收 width/height/RGB floats
    （glTexImage2D upload）；不再 include <fstream>/stb_image.h

tests（TestR2EnvironmentGpuLifetime 更新）
  - corrupted HDR → CPU 构造失败（GPU work 前）
  - 新增 GPU partial-construction rollback injection：
    procedural sky + 空 WISTERIA_ASSET_ROOT → geometry/FBO/RBO/procedural
    cubemap 已创建后 shader 加载失败 → Release 原子回滚（owning context
    → PendingDeleteCount == 0）——P0-2 rollback gate 不因 decode 搬移丢失
```

验证：

```text
Windows CORE 12/12、FULL 13/13、WSL CORE 14/14、FULL 15/15
ABI 94 legacy + 30 stable
environment_gpu_resource.cpp 零 fstream/stb 引用
```

明确不做：Environment dedup/cache identity（6B）、PipelineVariant（Step 7）。

## 3. 复审注意事项

1. Mesh 公共头不再持有 VBO/EBO（GPU 细节移出 CPU asset）；
   MeshGpuResource 属于 OpenGL backend 路径（Gate B）。
2. 动态 geometry 隔离由设计保证：clone 各自 unique_ptr realization；
   未来 RenderResourceCache 归并 static 时不得合并 dynamic。
3. Mesh 仍保留 device 引用（clone 需要）——0C 后期由
   RenderResourceCache 替代，属已记录的过渡债。
4. 每步都保持 R1 像素回归全绿 + ABI 30 stable。

## 4. Step 2 — IndexFormat 语义化（已完成）

```text
include/wisteria/rendering/model.hpp
  - ModelData::IndexGLType()（GLenum）删除
  - 新增 ModelData::IndexFormatValue()：
    sizeof(IndexType)==1 → IndexFormat::Uint8
    ==2 → Uint16；==4 → Uint32
  - include 清理：移除 ebo.hpp/vao.hpp（CPU asset 不再需要）；
    render_device.hpp 提供 IndexFormat（neutral）
  - model.hpp 零直接 GL 符号（仅注释提及）

include/wisteria/rendering/render_device.hpp
  - IndexFormat 枚举补回（0B Final Fix 重写时意外丢失，
    本轮发现并修复：GCC 编译暴露，Windows 增量构建未触发）

src/rendering/backend/opengl/mesh_gpu_resource.cpp
  - MapIndexFormat()：IndexFormat → GL_UNSIGNED_BYTE/SHORT/INT
  - Draw() 使用 IndexFormatValue() + MapIndexFormat()
```

验证：

```text
IndexGLType 全仓库移除（0 引用）
model.hpp 零直接 GL 类型/函数
四矩阵：Windows CORE 11/11、FULL 12/12、
        WSL CORE 13/13、FULL 14/14（全部重新构建后）
ABI：94 legacy + 30 stable
```

## 5. Step 3 — Texture split（已完成）

```text
src/rendering/backend/opengl/texture_gpu_resource.hpp/.cpp（新增）
  TextureGpuResource：
  - GL texture 对象所有权 + Bind/Unbind/UploadDecodedPixels/
    MaxUnits/ValidateUnit/Configure（GL 全部逻辑）
  - device 可空（与旧 Texture 析构行为一致）

include/wisteria/rendering/texture.hpp + src/rendering/texture.cpp
  Texture = CPU asset + 转发：
  - 保留 TextureData（file/encoded/RGBA8）+ stb 解码（CPU）
  - GL 成员删除；持有 unique_ptr<TextureGpuResource>
  - Upload*/Attach/Bind/Unbind/GetTexture 转发
```

## 6. Step 4 — Material split（已完成）

```text
src/rendering/backend/opengl/material_gpu_resource.hpp/.cpp（新增）
  MaterialGpuResource：
  - programCache/program/texture bindings 所有权
  - Attach（program acquire + texture attach）/Bind/Unbind/HasTexture

include/wisteria/rendering/material.hpp + src/rendering/material.cpp
  Material = semantic MaterialData + 转发：
  - 全部 getter 保留（读 data）
  - GPU 成员（programCache/program/textures）删除；
    持有 unique_ptr<MaterialGpuResource>
  - 4 个构造重载委托到 gpu 创建
  - ProgramCache 共享 ownership 保持（R1.9 修复不变）
```

## 7. Step 5 — EnvironmentMap split（已完成）

```text
src/rendering/backend/opengl/environment_gpu_resource.hpp/.cpp（新增）
  EnvironmentMapGpuResource：
  - IBL cubemap/BRDF LUT/capture FBO/RBO/skybox program/geometry 所有权
  - Attach（含 GL 限制验证 + ScopedOpenGlState 事务）/
    BindIrradiance/BindPrefilter/BindBrdfLut/ConfigureSkyboxVertexArray/
    DrawSkybox（接收 live data：intensity/drawSkybox 实时值）

include/wisteria/rendering/environment.hpp + src/rendering/environment.cpp
  EnvironmentMap = EnvironmentMapData + 转发：
  - 保留分辨率/power-of-two/mip/intensity 验证（CPU）
  - 10 个 GLuint + shader/program 成员删除；
    持有 unique_ptr<EnvironmentMapGpuResource>
  - getter/setter（Intensity/DrawSkybox/Data/MaxReflectionLod）保留
```

## 8. Step 3-5 验证

```text
四矩阵（全部重新构建后）：
  Windows CORE 11/11、Windows FULL 12/12
  WSL CORE 13/13、WSL FULL 14/14
ABI：94 legacy + 30 stable
R1 像素回归：stable render / engine==stable / IBL 路径全绿

Gate B 检查：
  Texture/Material/EnvironmentMap 公共头不再持有 GL 资源成员；
  全部 GPU realization 位于 backend/opengl/
```
