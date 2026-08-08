# R1.6 Phase 0C — Renderer-Facing Visual State Completeness 契约

> 状态：**CONTRACT FROZEN（2026-08-08，契约级审查闭合）**。
> 基线：R1.6 Phase 0A/0B CLOSED（`9d21bb0`，四矩阵全绿）。
> 一句话：把 Saba 的动态 UV 与求值后 material 以 backend-neutral 的
> transient render state 汇入统一 Renderer，消除 `if (Saba)` 的最后
> 一处视觉断链；Renderer 仍然 0 个 backend 类型。

## 1. 背景与目标

```text
现状：
Saba  → positions ✅ / normals ✅ / UV ❌ / material morph ❌
Generic → Pose ✅ / MorphState ✅ / static UV ✅ / MorphState 材质 ✅

目标（0C 完成后）：
                    ModelInstance
                         ↓
                ModelRenderFrameView
                  ↙             ↘
              Generic           Saba
           Pose/Morph       Pos/Normal/UV/
                            MaterialOverride
                  \             /
                   ↓           ↓
                      Renderer
```

0C 第一件事**不是改 Renderer**，而是基于实际源码冻结：

```text
1. ModelRenderFrameView 的 ownership / lifetime / channel validity
2. MaterialRuntimeOverride 的稳定映射
   （Saba material index ↔ ModelAsset RenderPart）
```

## 2. 源码审计（基于 `9d21bb0`）

### 2.1 Saba importer 的构造映射（1:1:1:1）

`src/assets/saba_mmd_importer.cpp`：

```text
每个 PMX material 一个循环：
  material.name = pmx.m_materials[i].m_name
  mesh.name = "sabaMesh" + i
  mesh.materialIndex = i
  mesh.morphMaterialIndex = i
  part = { "part" + i, meshIndex=i, identity }
```

因此 WISTERIA 导入的 Saba PMX 资产在**构造上**成立：

```text
RenderPart index == Mesh index == Material index == MorphMaterialIndex
（1:1:1:1）
```

这是导入器保证的，不是运行时假设。契约保留这条为 Saba 资产的
**构造不变式**，但映射机制不依赖“全局 material index == part index”。

### 2.2 Saba 求值后的材质终态

`third-party/saba/src/Saba/Model/MMD/PMXModel.cpp`：

```text
BeginMorphMaterial()  ：m_mulMaterialFactors / m_addMaterialFactors 复位
MorphMaterial(...)    ：按 weight 累加 Mul/Add factor
EndMorphMaterial()    ：写回 m_materials[i]：
  m_diffuse / m_alpha / m_specular / m_specularPower / m_ambient
  m_textureMulFactor / m_textureAddFactor
  m_spTextureMulFactor / m_spTextureAddFactor
  m_toonTextureMulFactor / m_toonTextureAddFactor
```

即 `PMXModel::GetMaterials()[i]` 在 Update 后就是**该帧逐材质求值
终态**，按 material index 排列，与 part 序一致。

### 2.3 动态 UV 终态

```text
PMXModel::GetUpdateUVs() / m_updateUVs
  每顶点一个 glm::vec2，与 GetUpdatePositions/Normals 同长度同序
```

**UV 坐标约定（冻结，2026-08-09 post-closure fix）**：

```text
WISTERIA render-view UV 约定 = raw PMX V-down
  （匹配 unflipped stb_image 上传路径；Saba 导入器静态 UV 即此约定）
Saba 内部是 V-up（PMXModel::Load 执行 uv.y = 1.0f - raw.y）
Saba adapter 发布动态 UV 时必须翻回 V-down：
  uv.y = 1.0f - GetUpdateUVs().y
否则 0C 动态上传会把整模型贴图垂直翻转
```

WISTERIA 的 `ModelVertexFrame` 只携带 positions/normals，UV 通道
未接（0B 审计已确认）。

### 2.4 WISTERIA 现有材质求值形状（与 Saba 不完全一致）

`MaterialMorphValues`（`include/wisteria/animation/morph.hpp`）：

```text
diffuse(vec4) / specular(vec3) / shininess / ambient(vec3)
edgeColor(vec4) / edgeSize
textureFactor(vec4) / sphereTextureFactor(vec4) / toonTextureFactor(vec4)
```

Saba 最终 `MMDMaterial` 保存的是**三组独立的 multiply + add**：

```text
textureMulFactor / textureAddFactor
spTextureMulFactor / spTextureAddFactor
toonTextureMulFactor / toonTextureAddFactor
```

因此 `MaterialMorphValues` 的单一 factor **不足以无损表达 Saba 该帧
终态**。neutral override 必须保留 Mul/Add 两部分；普通
diffuse/specular/ambient/edge 是求值后终值，直接复制。
Generic 继续走 `MaterialMorphValues` 旧逻辑，并在 Renderer
resolved-state 中解释为 `multiply = textureFactor, add = 0`——
0C 不扩大成 Generic material morph 重构。

### 2.5 Renderer 当前消费链

```text
RenderCommand{ part, model, entity.TryGetPose(), entity.TryGetMorphState() }
  → DrawPart：
      EvaluateMaterialMorphs(part, morphState)
      —— Generic（MorphState 存在）走 MorphSet 材质 morph
      —— Saba（MorphState == nullptr）落 base material
      UploadMorphing(morphState) / UploadSkinning(pose)
      Mesh dynamic provider（Saba positions/normals）
```

## 3. 冻结 1：`ModelRenderFrameView`

```cpp
struct ModelRenderFrameView
{
    ModelVertexFrame geometry;                 // positions/normals/revision
    std::span<const glm::vec2> uvs;            // optional dynamic UV
    std::span<const MaterialRuntimeOverride> materials; // optional
    const Pose* pose = nullptr;
    const MorphState* morphState = nullptr;
};
```

### ownership / lifetime（冻结）

```text
transient、zero-copy：所有 span 指向 Runtime 内部 buffer
valid：直到下一次 runtime state mutation 或 Runtime 销毁
       Update / Reset / deterministic exact step / seek /
       restore / morph override 等均属于 mutation
       （与 ModelFrameView 同一生命周期规则）
禁止存储、禁止跨帧持有、禁止暴露给长生命周期 C handle
Renderer 只在当前帧内消费
```

### channel validity 与执法者（冻结）

```text
IModelRuntimeDriver 负责生产；
ModelInstance 负责 generic contract validation（统一执法）：

geometry：
  positions/normals 双空 → absent
  否则 size 必须相等 → 不等 logic_error

uvs：
  empty → absent（保留 asset 静态 UV）
  非空 → v1 要求 geometry present，且
         uvs.size == geometry.positions.size() → 不等 logic_error

materials：
  empty → absent（走既有 base/morphState 路径）
  非空 → size 必须在 runtime material slot domain 内
         （v1：size == ModelAsset::Parts().size()）→ 不等 logic_error

pose / morphState：沿用 R1.5 optional 语义（可为 nullptr）
Saba 在自身再做强不变式验证
```

### 生产与发布（冻结）

```text
Runtime Update
  ↓
ModelInstance 调用 ProduceRenderFrameView() 一次
  ↓
ModelInstance 验证 channel validity
  ↓
缓存 LastRenderFrameView
  ↓
Renderer 只消费 ModelInstance 的 LastRenderFrameView
（Renderer 不直接调用 runtime）
```

### 生产接口（冻结方向）

```text
IModelRuntimeDriver 增加：
  virtual ModelRenderFrameView ProduceRenderFrameView() const;
  默认实现 = { ProduceFrameView().geometry,
               {}, {}, pose, morphState }

ModelInstance 增加：
  const ModelRenderFrameView& LastRenderFrameView() const noexcept;

不要同时调用 ProduceFrameView + ProduceRenderFrameView 各一遍；
ModelInstance 只生产一次 render view，并以其 geometry/pose
同步既有 lastView。

ModelFrameSnapshot 不扩容（persistent observation 职责不变）
ModelFrameView 保留（pose/geometry 轻量视图）
ModelRenderFrameView = render-facing transient 视图
```

## 4. 冻结 2：`MaterialRuntimeOverride` 与映射

```cpp
struct MaterialRuntimeOverride
{
    glm::vec4 diffuse{1.0f};         // RGBA，含 alpha
    glm::vec3 specular{1.0f};
    float shininess = 32.0f;
    glm::vec3 ambient{0.0f};
    glm::vec4 edgeColor{0.0f, 0.0f, 0.0f, 1.0f};
    float edgeSize = 0.0f;

    glm::vec4 textureMultiply{1.0f};
    glm::vec4 textureAdd{0.0f};

    glm::vec4 sphereTextureMultiply{1.0f};
    glm::vec4 sphereTextureAdd{0.0f};

    glm::vec4 toonTextureMultiply{1.0f};
    glm::vec4 toonTextureAdd{0.0f};
};
```

### 映射规则（冻结）

```text
materials[] = runtime material slots

Renderer lookup：
  part.MorphMaterialIndex()
          ↓
  materials[slot]

不得依赖 Entity 当前 RenderPart 顺序
（AddRenderPart / RemoveRenderPart / SetMesh / SetMaterial 都可能
改变实体 part 顺序；asset part 顺序只在初始实例化时成立）

Saba adapter：
  slot == PMX material index
  （导入器构造不变式 §2.1 + Initialize 验证保证）
  Update / exact step / restore 等所有 evaluation publication path
  后从 m_materials[slot] 复制求值终态到内部 buffer

Generic：
  materials 通道为空（MorphState 材质路径继续生效）
```

### 构造期验证（冻结）

位置：**`SabaMmdRuntimeModel::Initialize()`**（模型 Load 成功后
material count 才有权威值；不是 `SabaMmdBackend::CreateRuntime()`）：

```text
asset.PartCount() == Saba material count
且每个 part 的 MorphMaterialIndex == part 序数（Saba 资产）
不一致 → Initialize false / throw（经 backend 保持 R1.4
transaction boundary），不允许静默错位
验证通过后创建 materialOverrides buffer
```

明确：

```text
不得假设全局 "material index == RenderPart index"；
该对齐是 Saba 导入器构造不变式 + 构造期验证的结果，
不是通用模型语义
```

## 5. Renderer 消费方向（0C 实现阶段）

```text
统一 resolved material state：
  ResolveMaterialState(part, ModelRenderFrameView)
  优先级：runtime material override → Generic MorphState → base
  同一个 resolved 结果同时用于：
    EffectiveAlphaMode / command bucketing / DrawPart uniforms /
    edge / texture factors
  RenderCommand 直接保存 resolved result
  （不允许分桶求值一次、DrawPart 再求值一次导致不一致）

command 构建：
  override lookup = part.MorphMaterialIndex() → frame.materials[slot]

UV（global → part remap + combined upload）：
  Saba GetUpdateUVs() 是全局顶点数组，与全局 positions/normals 同序
  → 与 positions/normals 走同一张 Mesh.SourceVertexIndices() remap
  → 一次 interleaved rebuild + 一次 VBO upload：
      Mesh::UploadDynamicFrame(positions, normals, uvs = {})
  uvs.empty() → 保留静态 texCoord（Generic/旧行为完全兼容）
  禁止 UploadDynamicVertices + UploadDynamicUvs 分两次上传
  （第二次从原始 bind data rebuild 会覆盖第一次的动态结果）

Renderer 仍 0 个 Saba / Generic 类型
```

## 6. 0C 测试计划（实现阶段）

```text
1. Saba UV bridge：
   extended_morph.pmx 的 UV morph
   → ProduceRenderFrameView().uvs 随 weight 变化
   → uvs.size == positions.size
2. Material override mapping：
   Saba 资产逐 part override 与 m_materials 求值一致
   （part 序数 == material index 构造不变式）
   + Entity part mutation mapping：
     删除/重排某个 RenderPart 后，幸存 part 仍通过
     MorphMaterialIndex 拿到原 runtime material slot，
     不得按当前 vector index 串位
3. Generic channel absence：
   animated_triangle / procedural morph → uvs/materials 为空
4. validity rejection：
   uvs size 不等 / materials size 不等于 part count → logic_error
5. Renderer smoke：
   Saba UV morph 帧与 base 帧像素不同；Generic 路径回归不变
6. Deterministic render-view publication：
   PrepareFrameZero / StepMotionFrameExact →
   ProduceRenderFrameView → UV/material 对应当前 exact frame
   （证明 exact runtime state 与 renderer-facing state 只有一条
     publication path；不做离屏序列，那是 0E）
7. 四套矩阵全绿
```

## 7. 明确不做（0C 边界）

```text
不重写 Renderer / 不建 RenderTarget / 不碰 Present/FXAA
不扩 ModelFrameSnapshot
不做 checkpoint / deterministic exact frame / Stable C API
不修 GraphicsDevice share-group identity（R1.7）
不做 PNG / manifest（0E）
不为 Generic 伪造动态 UV/material 通道
```

## 8. 开放决策（0A 审查时定）

```text
1. API 名称：ProduceRenderFrameView() / LastRenderFrameView()；
   Renderer 不直接调用 runtime（已拍板）
2. Saba buffer：SabaMmdRuntimeModel::Impl 内部
   std::vector<MaterialRuntimeOverride> materialOverrides；
   集中 helper SyncRenderStateFromSaba() 在 Update / exact step /
   canonical evaluation / restore-replay 后统一同步（已拍板）
3. Mesh 接口：combined UploadDynamicFrame(positions, normals, uvs={})
   —— 一次 rebuild + 一次上传（已拍板）
4. 构造期验证：SabaMmdRuntimeModel::Initialize()（已拍板）
```
