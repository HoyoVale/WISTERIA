# R1.6 Phase 0C — Renderer-Facing Visual State Completeness 实现基线（2026-08-08）

> 状态：**COMPLETED（四矩阵全绿）**。
> 契约：`docs/architecture/R1_6_PHASE0C_CONTRACT.md`
> （CONTRACT FROZEN，2026-08-08）。

## 1. 一句话

Saba 的动态 UV 与求值后 material（含 texture Mul/Add 两套因子）以
backend-neutral 的 `ModelRenderFrameView` 汇入统一 Renderer；材质解析
在 command 构建阶段统一执行（分桶与 DrawPart 同源），Renderer 仍
0 个 Saba/Generic 类型。

## 2. 代码改动

### 中性类型与接口

```text
include/wisteria/runtime/runtime_model_base.hpp
  MaterialRuntimeOverride：
    diffuse(RGBA)/specular/shininess/ambient/edgeColor/edgeSize
    textureMultiply+textureAdd
    sphereTextureMultiply+sphereTextureAdd
    toonTextureMultiply+toonTextureAdd
  ModelRenderFrameView：
    geometry + optional uvs + optional materials + pose + morphState
  IModelRuntimeDriver::ProduceRenderFrameView() const
    默认 = { ProduceFrameView().geometry, {}, {}, pose, morphState }
```

### ModelInstance（生产/验证/发布 authority）

```text
Update：只调用 ProduceRenderFrameView() 一次
  → ValidateRenderFrameView（generic contract 统一执法）
  → 缓存 LastRenderFrameView()
  → 以同一 production 同步 lastView.geometry/pose
Reset：lastRenderView 清空（mutation 使 transient view 失效）
UploadDynamicVertices：从 lastRenderView 取 positions/normals/uvs，
  与 SourceVertexIndices 同一张 remap，一次 combined upload
```

### Mesh combined upload

```text
Mesh::UploadDynamicFrame(positions, normals, uvs = {})
  RebuildInterleavedVertices 增加可选 uvs：
    empty → 保留静态 texCoord
    非空 → 写 texCoord 槽（要求 layout 含 texCoord）
  一次 rebuild + 一次 glBufferSubData
UploadDynamicVertices 变为 wrapper（uvs 空）
```

### Saba bridge

```text
Impl::materialOverrides（按 PMX material index 的 slot buffer）
Initialize（模型 Load 后，material count 权威）：
  scene-backed asset（PartCount>0）强制验证：
    PartCount == material count
    每个 part MorphMaterialIndex == part 序数
  失败 → Initialize false（R1.4 transaction boundary）
  独立 Runtime（无 asset / 0 part）跳过验证，仍建 buffer
SyncRenderStateFromSaba()：
  挂在 SyncPoseFromSaba 之后 —— Update / exact step / restore /
  replay 全部 publication path 自动同步
  m_materials → MaterialRuntimeOverride（mul/add 独立保留）
ProduceRenderFrameView：
  geometry + GetUpdateUVs()（全局，与 positions 同序）
  + materialOverrides span + pose；morphState = nullptr
```

### Renderer resolved material（分桶与 DrawPart 同源）

```text
ResolveMaterialState(part, frame)：
  frame.materials 非空 → override 按 part.MorphMaterialIndex() → slot
    无效 slot → logic_error
  否则 → EvaluateMaterialMorphs(part, morphState)
RenderCommand.material = resolved result
EffectiveAlphaMode(command.material) 决定 opaque/transparent 分桶
DrawPart 接收同一个 resolved material（不再二次求值）
MaterialMorphValues 增加 textureAdd/sphereTextureAdd/toonTextureAdd
ShaderInterface 增加 3 个 add-factor uniform 名
mmd.frag：baseColor/sphere/toon 应用 * multiply + add
```

## 3. 验证（Windows/Linux 实跑）

### runtime tests（canary 加固）

```text
R1.5 procedural malformed render channels rejected：
  malformed UV（2 UV / 3 vertices）→ Update 抛 logic_error
  malformed materials（2 slots / 0 parts）→ Update 抛 logic_error
```

### integration tests（新增 4 项）

```text
R1.6 Saba render frame view bridge：
  uvs.size == positions.size；materials.size == PartCount
  SetMorphWeight("uv", 1.0) → uvs 变化
  SetMorphWeight("materialMorph", 1.0) → override diffuse/tex 变化

R1.6 Saba render part mutation mapping（FULL：production 叶瞬光 21 parts）：
  删除 part0 后，幸存 part0 的 MorphMaterialIndex == 1
  按 slot 查 override，不按 vector index 串位

R1.6 generic render frame view absence：
  animated_triangle → uvs/materials 为空、pose 存在

R1.6 Saba deterministic render view publication：
  PrepareFrameZero 后 ProduceRenderFrameView 与 realtime Update 的
  materials/uvs 一致（单条 publication path）
```

### render smoke（render_fbo_tests）

```text
Saba material morph 像素证明：
  extended_morph.pmx → Scene → Renderer → ReadbackRgba8
  SetMorphWeight("materialMorph", 1.0) → override diffuse 1.0→1.2、
  textureAdd 0→0.1 → 离屏像素变化
  （CORE 夹具无纹理，UV 通道的视觉效果无法用像素证明；
    UV 以 render-view 级 bridge 测试证明，纹理依赖的视觉证明
    留待 FULL 资产场景）
```

## 4. 四套矩阵（2026-08-08 实测）

```text
Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）。

## 5. 边界（Phase 0C 不做）

```text
不重写 Renderer / 不建 RenderTarget / 不碰 Present-FXAA
不扩 ModelFrameSnapshot
不做 checkpoint / deterministic exact frame / Stable C API
不修 GraphicsDevice share-group identity（R1.7）
不做 PNG / manifest（0E）
Generic 不伪造动态 UV/material 通道；Generic 材质 morph 继续走
MaterialMorphValues（multiply 语义，add=0），未重构
```

## 5.1 Final Guard（2026-08-08 第二轮审查闭合）

```text
1. deterministic publication 测试遵守 transient lifetime：
   PrepareFrameZero（mutation）前把 uvs/materials/positions 复制为
   vector；比较时不再读取已失效的 span；
   material 全字段（diffuse/specular/shininess/ambient/edge/
   texture-sphere-toon 的 Mul+Add）逐项断言
2. Saba 发布与 Pose 解耦：
   SyncPoseFromSaba 不再隐式调用 SyncRenderStateFromSaba；
   Update / exact step / restore / PublishAfterPhysicsPose 每个
   publication boundary 显式成对调用；
   Initialize 在 materialOverrides.resize 后立即首次同步
   （Initialize 完成后 materials 就是真实 Saba 终态，不是默认值）
3. ResolveMaterialState nullopt fallback：
   MorphMaterialIndex == nullopt（用户 AddRenderPart 的 part）→
   走 MorphState/base 路径，不 throw；
   仅 non-null 且 OOB → logic_error；
   render smoke 增加 Saba entity + nullopt extra part 回归
   （Render 不抛）
```

## 6. 下一步

Phase 0D：Explicit Presentation Authority（Camera/Light 应用，MMD
adapter 在 orchestration 层）+ 离屏输出链消费 `LastRenderFrameView`。

## 7. Post-closure fix（2026-08-09，人工检查发现）

**症状**：Phase 0C 关闭后，demo 渲染的模型贴图垂直翻转。

**根因**：Saba `PMXModel::Load` 把 PMX UV 翻成 V-up
（`uv.y = 1.0f - raw.y`），而 WISTERIA 的 UV 约定是 raw PMX V-down
（配合 unflipped stb_image 上传）。0C 之前动态上传只覆盖
positions/normals，UV 一直用导入器静态值（V-down），所以正确；
0C 之后每帧用 Saba `GetUpdateUVs()`（V-up）覆盖 UV → 整模型贴图
垂直翻转。

**修复**：`SabaMmdRuntimeModel::Impl` 增加 `renderUvs` buffer；
`SyncRenderStateFromSaba()`（所有 publication path）把
`GetUpdateUVs()` 翻回 V-down（`uv.y = 1.0f - uv.y`）；
`ProduceRenderFrameView()` 返回该 buffer 的 span。

**回归**：`TestSabaRenderFrameViewBridge` 新增约定断言——无 UV morph
时 render-view UV 必须逐顶点等于导入器静态 UV。

**验证**：demo frame 1 修复后像素哈希
`0xd232f77654e4a947` == 静态 UV（无动态上传）捕获哈希，逐字节一致。
