# R1.9 — Stable Runtime / Render C ABI（契约草案 Phase 0A）

> 状态：**FROZEN v1.0；0A CLOSED、0B/0D FINAL MICRO PATCH II APPLIED（待复审）、
> 0E HOLD（2026-08-10）**
> 前置：R1.8 CLOSED（tag `r1.8-final-closure`，四矩阵全绿）。
> R1.7 native-Linux hardware gate 为独立 validation debt，
> 不阻塞 R1.9 开发（真机条件具备时补跑 `script/verify_r17_native_linux.sh`）。
>
> 本阶段只做 **ABI archaeology + 契约**，不写实现。
> 顺序：先审"哪些能力值得公开"，再在 0B–0D 审"公开接口怎么长"。

## 1. 一句话

把已经冻结的 R1 能力以稳定 C ABI 暴露给外部程序：
Runtime / Capability / Deterministic stepping / Checkpoint /
Offline render execution。核心原则：

> **C ABI = stable engine contracts 的 projection，不是第二套 engine architecture。**

## 2. 现状盘点（ABI archaeology，2026-08-09）

### 2.1 两张现有 C 面

```text
wisteria_native.h（v0.6，LEGACY 候选）
  ~113 个导出（C_ABI_SAFETY_MATRIX：INVOKE_ABI 108、RAW_TRY 2、
  PROVEN_NO_THROW_LEAF 3）
  句柄：WisteriaContext / Model / Motion / Window / Scene /
        SceneModel / Entity / Light（uint64 opaque）
  错误：WisteriaStatus + wisteria_last_error_message
  线程：一个 Context 单线程
  路径：UTF-8（Windows 经 PathFromUtf8 转 UTF-16）
  不承诺二进制稳定（v0.x）

wisteria_stable_runtime.h（v1，R1.4 FROZEN，19 个 stable function
  declarations：Context 3 + Entity 9 + Checkpoint 6 + last_error 1）
  context create/destroy/info
  entity create/destroy/capabilities
  entity load/unload motion
  entity prepare_frame_zero / step_exact / replay_exact /
       set_preview_frame
  checkpoint create/restore/destroy/info/serialize/deserialize
  last_error
  struct version/size guards；WISTERIA_CAP_*；checkpoint wire 常量
  当前实体路径硬编码 MmdRuntimeModel（RequireStableMmd）
```

### 2.2 R1.8 之后尚未进入 stable 的能力

```text
Generic backend（checkpoint payload kind 2）
deterministic capability 三比特（authoritative）+ mirror 一致性
persistent morph override（SetMorphOverride / Clear / ClearAll）
ModelAsset::DeterministicFingerprint（资产身份）
backend-neutral OfflineFrameSequence（IModelRuntimeDriver）
HeadlessRenderSession / RenderOffline（零窗口）
EGL display refcount 等 provider 生命周期（C++ 内部，不公开）
```

### 2.3 已有安全基础设施（KEEP，不重造）

```text
InvokeAbi / GuardAbi 异常边界（RAII，防异常穿 C ABI）
gen_abi_safety_matrix.py 自动矩阵（113 项全守卫）
native_abi_c_smoke.c（纯 C 编译 smoke）
checkpoint 跨进程 CLI + Python 驱动（stable + legacy 两套）
Python ctypes / Node N-API 示例（M3/M4 已跑通）
```

## 3. 能力分类（0A 核心交付）

### KEEP（已稳定，只维护）

```text
wisteria_stable_* 现有 19 个导出（R1.4 冻结）
struct version/size 守卫、opaque handle 模型、单线程契约、
UTF-8 路径、Status + last_error 错误模型
checkpoint = opaque bytes 搬运（ABI 永不解释 payload 内部结构）
```

### PROMOTE（内部能力 → 稳定 ABI）

```text
A. backend-neutral entity：
   同一 stable entity 承载 Saba + Generic（capability 驱动）
   backend id 是"查询结果"（identity/diagnostics），不是创建指令；
   不公开 backend selector
   capabilities 复用 V1 既有字段：
     runtime_backend_id（Saba=1 / Generic=2）
     checkpoint_payload_kind（MMD R12C=1 / Generic R18=2）
   新增常量（不建 V2，除非出现真正无法表达的新语义）：
     WISTERIA_BACKEND_ID_WISTERIA_GENERIC 2u
     WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18 2u
     WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_GENERIC_R18 1u
B. deterministic capability 直接映射
   ModelRuntimeCapabilities.deterministic（authoritative）：
   exact stepping + checkpoint capture/restore/replay
   mirror 不一致 = contract violation（与 C++ 同规则）
C. Generic exact step / replay（复用现有 stable step 接口，
   按 payload kind 分派 checkpoint）
D. persistent morph override（SetMorphOverride / Clear /
   ClearAllMorphOverrides）→ 新增 stable 函数
E. asset fingerprint（uint64）暴露，供 session identity / 校验
F. HeadlessRenderSession C 面（0D）：create context/session →
   RenderOffline → RGBA8 buffer
G. OfflineFrameSequence C 面（0D）：RenderRange / Resume /
   LastCommittedFrame / Failed + manifest 目录（opaque session）
   0D 同时暴露单帧 RenderOffline（最小渲染原语，非 RenderDevice）
```

### LEGACY（保留兼容，不继续扩展）

```text
wisteria_native.h v0.6 全部导出
（继续提供，符号与行为不变；新能力只进 stable 面）
```

### INTERNAL（永不公开）

```text
InvokeAbi / GuardAbi / RAW_TRY 机制
native_context / stable_native_context 内部注册表
windows_path / PathFromUtf8 实现
trace JSONL 工具链
GraphicsDevice / EGL provider 内部生命周期
```

### DEFER（R2.x）

```text
RenderDevice / RenderTarget / RenderGraph / Vulkan abstraction
async render / job system
scene serialization
editor API
raw headless provider（EGL 设备选择）C 面
video encoding / FFmpeg / Audio
```

## 4. 架构规则（0A 冻结方向）

```text
1. C ABI = projection：capability 直接映射
   ModelRuntimeCapabilities.deterministic，禁止第二套能力模型
2. checkpoint：ABI 只搬运 opaque bytes（R1.4 envelope），
   不解释 payload；payload kind 通过常量 + capability 暴露，
   未来 Generic kind 3 / 其他 backend kind N 不需要改 ABI 版本
3. 句柄全部 opaque uint64；单线程 per context；
   错误统一 Status + last_error；struct 带 version/size 守卫
4. 新导出必须有引擎级用例 + 回归测试（R1 审查约定）
5. 版本策略：既有 struct 冻结；新字段走 version bump + size 守卫；
   新函数只增不改；payload kinds 只增不改
6. 冻结 struct 绝不原地追加字段：
   struct_size 用于安全读取/互操作，不是逃避版本升级；
   需要新字段 → 新版本 struct（如 WisteriaRuntimeCapabilitiesV2）
7. 既有 stable 函数允许扩展支持的 backend，但不允许改变既有
   Saba 调用的成功/失败语义（新增 glTF → Generic 是 additive）
8. 保留 WisteriaEntity 命名：定义为"拥有/承载一个 model runtime
   instance 的稳定实体句柄"，不引入 WisteriaRuntime /
   WisteriaModelInstance（未来 Scene graph ABI 再说）
9. 头文件按 domain 分层但仍是同一套 ABI：
   wisteria_stable_runtime.h（Context/Entity/capabilities/exact step/
     morph override/fingerprint/checkpoint）
   wisteria_stable_render.h（0D 新增，include runtime 头，
     RenderSession / RenderOffline / OfflineFrameSequence，
     独立 WISTERIA_STABLE_RENDER_ABI_VERSION）
```

## 5. 阶段计划

```text
Phase 0A  契约（本文档）——分类表冻结 + 边界           ✅ CLOSED
Phase 0B  Runtime / capability C ABI                  ⚠️ APPROVED W/ MICRO PATCH II
          backend-neutral entity + capability 映射 +
          morph override + asset fingerprint
Phase 0C  Deterministic stepping + checkpoint C ABI        ✅ CLOSED
          Generic exact step/replay + payload kind 2 +
          跨进程 checkpoint（Generic）
Phase 0D  Render / offline execution C ABI              ⚠️ APPROVED W/ MICRO PATCH II
          wisteria_stable_render.h + RenderSession +
          单帧 RenderOffline + OfflineFrameSequence C 面
Phase 0E  ABI compatibility matrix + Final Closure
          C smoke + 跨进程 + Python/Node 绑定 + 四矩阵
```

## 6. 明确不做（R1.9 边界）

```text
RenderDevice / RenderGraph / Vulkan
async render / job system
scene serialization / editor API
raw headless provider C 面
video / audio
重开 R1.4/R1.6/R1.7/R1.8 已冻结契约
```

## 7. 成功标准（0A）

```text
1. 分类表（KEEP/PROMOTE/LEGACY/INTERNAL/DEFER）逐项可审计
2. 每个 PROMOTE 项都有对应的引擎级冻结契约可投影
3. stable 头保持 C99；无 C++ 类型跨 ABI；struct 守卫齐备
4. capability / payload kind 的扩展路径（additive）明确
5. R1.8 closure baseline 不受 0A 影响（tag 已建）
```

## 8. 已拍板决策（2026-08-09）

```text
Decision 1 — Header domains
Runtime/deterministic/checkpoint remain in wisteria_stable_runtime.h.
R1.9 0D introduces wisteria_stable_render.h, which reuses the same
Context/error/handle conventions and includes the runtime header.

Decision 2 — Entity naming
Existing WisteriaEntity and wisteria_stable_entity_* symbols remain
authoritative. No WisteriaRuntime or WisteriaModelInstance handle is
introduced in R1.9.

Decision 3 — Backend selection
Stable entity creation is backend-neutral and engine-selected from the
asset/import path. backend_id is observable identity, not a caller-facing
backend selector. Capability discovery governs feature availability.

Decision 4 — Stable render scope
0D exposes: offline/headless render session + single-frame RenderOffline
→ RGBA8 + OfflineFrameSequence RenderRange/Resume.
No RenderDevice/RenderGraph/native EGL surface is exposed.

Decision 5 — Binding gates
Python ctypes is normative R1.9 0E acceptance.
Node N-API is a non-blocking compatibility smoke.

Decision 6 — R1.7 debt
Native-Linux hardware EGL validation remains independent validation debt
and does not block R1.9 implementation or closure.

Additional ABI invariants
- WisteriaRuntimeCapabilitiesV1 is frozen.
- Prefer existing V1 fields for Generic backend/payload kind; do not create
  V2 unless a genuinely new semantic cannot be represented.
- Existing stable symbols are never renamed or behaviorally narrowed.
- Stable structs are never enlarged in place after freeze.
```

### Phase 0B 范围（已批准）

```text
1. stable entity 后端无关化：
   entity_create 由 engine 从 asset/import 结果选择 backend
   （PMX → Saba；skeleton/morph/animation → Generic）
2. capabilities 复用 V1：
   backend id（Saba=1 / Generic=2）、payload kind（1/2）、
   deterministic profile（1/2）、max frame（2^24 / 2^20）
3. 新常量：WISTERIA_BACKEND_ID_WISTERIA_GENERIC /
   WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18 /
   WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_GENERIC_R18 /
   WISTERIA_DETERMINISTIC_PROFILE_GENERIC_V1 /
   WISTERIA_STATUS_UNSUPPORTED
4. exact step / replay 后端无关（IDeterministicFrameStepper）；
   MMD-only（motion/preview）保留 MmdRuntimeModel 门
5. checkpoint variant 分派：
   create/restore/info/serialize/deserialize 按 payload kind
   （1 = FrameCheckpoint，2 = GenericRuntimeCheckpoint）
6. 新函数：persistent morph override ×3 + asset fingerprint
7. StableEntityEntry 生命周期：asset 的 RenderPart 引用的
   mesh/material 由 entry 拥有且晚于 asset 析构
8. ABI safety matrix 重新生成（94 legacy + 23 stable）
```

### Phase 0C 范围（已批准）

```text
1. Generic payload kind 2 走完整 stable checkpoint 面：
   create / info / serialize / deserialize / restore（0B 已通）
2. 跨进程 checkpoint E2E（Generic）：
   stable-checkpoint CLI 以 animated-triangle-gltf（无 VMD）跑
   dump → load，N 与 N+1 wire 字节双进程一致
3. ctest 注册 wisteria.stable-checkpoint-cross-process-generic
   （CORE 层，Windows/Linux/FULL 均执行）
4. Saba kind 1 与 production-full 跨进程回归保持通过
```

### Phase 0D 范围（已批准）

```text
1. 新头 wisteria_stable_render.h（WISTERIA_STABLE_RENDER_ABI_VERSION 1u，
   include runtime 头，同一 Context/error/handle 约定）
2. RenderSession：create/destroy + force_software
3. 单帧 RenderOffline → RGBA8（size query + caller buffer，
   渲染 stable entity 的 EXACT runtime state：borrow ModelInstance
   进临时 Scene → render → 归还）
4. OfflineFrameSequence：sequence_range / sequence_resume /
   last_committed / failed（Reject/Overwrite/VerifySkip 映射）
5. GLFW hidden-window provider：Windows 主路径 + Linux EGL 回退；
   factory 不变量与 forceSoftware 严格语义保持
6. 修复：
   - WisteriaNativeUtf8ToWide 不再把终止 NUL 写进路径
   - Resume 的 checkpoint ifstream 收窄作用域（Windows CRT 无
     FILE_SHARE_DELETE，同次 resume 改写同 slot 会 ACCESS_DENIED）
7. ABI safety matrix：94 legacy + 30 stable
```

### Final Fix（2026-08-09 代码复审后，已实施）

```text
1. WisteriaNativeUtf8ToWide：分配 wideLength、写 wideLength、
   成功后 resize(wideLength-1)——修复一元素越界写；
   新增 Unicode 路径测试（中文目录/文件名，双平台）
2. 资产组装单一 pipeline：ImportedModelData → ModelAssetBundle
   （meshes/materials/textures），ResourceManager 与 stable entity
   共同复用；imported materialIndex + texture bindings 完整保留
3. capabilities：backend 身份来自 ModelAsset::BackendKind()；
   Static = STATIC + profile/payload NONE；deterministic ↔ checkpoint
   mirror 不一致 → INTERNAL + contract-violation last_error
4. RenderSession/GPU ownership（v1 最小方案）：
   一个 StableContext 最多一个 active RenderSession；
   entity 首次 render 绑定 owner session；不同 session → INVALID_STATE；
   entity_destroy 在 owner context current 下释放；
   session_destroy 存在 bound entity → INVALID_STATE；
   context 按 GPU-safe 顺序 teardown
5. GLFW 生命周期统一：platform/glfw_lifetime.hpp 单一
   Acquire/Release（Application + GlfwHeadlessContext 共用）；
   新增共存测试
6. provider fallback：EGL 失败 + !forceSoftware → 真正回退
   GLFW-hidden；forceSoftware 严格；WISTERIA_HEADLESS_DISABLE_EGL
   强制失败钩子 + ctest（smoke-glfw-fallback）
7. stable render 一致性：size-query 先验证 session/entity；
   sequence 异常同步 lastCommitted/failed；write_png||write_raw 必选；
   invalid-handle + failure-state 测试
8. 0C 前瞻：ProbeCheckpointEnvelope（engine-owned header parser）；
   malformed → INVALID_CHECKPOINT；unknown kind → UNSUPPORTED
9. 文档：KEEP 14→19；GLFW-hidden 描述为 compatibility provider
```

### Final Micro Fix（2026-08-10 复审后，已实施）

```text
P0-1  Stable Render 真正挂载 RenderParts：
       抽 engine-owned Scene::BindModelInstanceParts（ResolveMesh），
       Scene::InstantiateModel 与 stable borrow 共用
P0-2  单帧 fill 前 ModelInstance::PublishCurrentRuntimeFrame()；
       size query 不 publish、不绑定 owner
P0-3  Static entity 始终创建 ModelInstance（runtime=null）；
       checkpoint restore 对 null runtime 显式
       INVALID_CHECKPOINT（禁止解引用）
4.    ownerRenderSession 只在真实 GPU fill/sequence 前设置；
       size query / too-small / unsupported 无副作用
5.    StableContextState teardown：MakeCurrent 失败 → fail-stop
       （与 HeadlessRenderSession 一致）
6.    ProbeCheckpointEnvelope 验证完整 common envelope
       （header + payload size + build id + checksum）；
       truncated unknown → INVALID_CHECKPOINT
7.    ModelAssetBundle 接收共享 ProgramCache；
       ResourceManager 继续用 GraphicsDevice::Programs()
8.    像素正确性测试：非背景、帧间变化、stable==engine、
       Static render、Static checkpoint restore 拒绝
9.    ctest：三个 tier 测试统一 WORKING_DIRECTORY=source root
       （修复 ctest 下 shader 相对路径解析）
```

### Final Micro Patch II（2026-08-10 ChatGPT 复审 `b865be9` 后，已实施）

```text
P0-1  borrow transaction 异常安全：
       EntityBorrowGuard 在 SetModelInstance 之后、
       BindModelInstanceParts 之前建立；三个路径统一
       （single render / sequence_range / sequence_resume）
P0-2  GPU ownership commit 时机：
       ownerRenderSession 移到所有 CPU-only setup 成功之后、
       紧挨第一次真实 GPU operation 之前（单帧 = RenderOffline 前；
       sequence = MakeCurrent 成功后、RenderRange/Resume 前）
P0-3  status 语义（backend-neutral）：
       NOT_FOUND 只表示 handle 不存在；
       存在 entity 但无对应 backend/runtime → UNSUPPORTED；
       存在 runtime 但无 deterministic profile → UNSUPPORTED_REPLAY_PROFILE；
       checkpoint 与 backend 不兼容 → INVALID_CHECKPOINT
4.    RequireStableRuntime / RequireStableMmd 增加 out_status：
       10 个 runtime 调用点 + 2 个 sequence 路径逐处区分；
       Static + step/replay/checkpoint-create/sequence → UNSUPPORTED
5.    新增 negative-status 集成测试 TestR19StableStatusSemantics：
       Generic+load_motion、Static+morph/prepare/step/replay/
       checkpoint-create/sequence → UNSUPPORTED；
       garbage entity → NOT_FOUND
6.    回归证据：Windows CORE 9/9、Windows FULL 10/10、
       WSL CORE 11/11、WSL FULL 12/12；ABI 94 legacy + 30 stable 不变
```

> 详细验证见 `docs/validation/R1_9_FINAL_MICRO_PATCH_II_20260810.md`。
> 0E HOLD 待 ChatGPT 对 Micro Patch II 复审通过后解除。
