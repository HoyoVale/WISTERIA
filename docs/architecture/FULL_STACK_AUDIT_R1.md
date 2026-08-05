# WISTERIA 全栈审查报告（R1 收口后 / C 门户开通前）

> 状态：已完结。结论基于代码级核实；C 门户前置项（R1-01/02/04/06/07/09/10）
> 已全部落地并双平台验证，R1-08 并入渲染配置导出里程碑。
> 范围：底层资源、平台层、资源/场景层、运行时层、渲染层、测试与治理。
> 目标：确认 MMD 渲染链与基础 PBR 材质渲染链的当前支撑状态，并给出
> C 门户（`wisteria_native`）全面导出的前置条件。

## 0. 结论摘要

- **MMD 渲染链（PMX + VMD + IK/Morph/物理 + CPU 蒙皮 + MMD toon/CSM/地面影）**
  已双平台（Windows Intel / WSL llvmpipe）验证打通，0 GL 错误，回归测试覆盖
  绕序、GL 无错误、阴影均匀性。
- **基础 PBR 材质渲染链（程序化 ground/cube，basicTex shader）** 已双平台
  验证打通（像素级回归测试双平台数值一致）。
- **导入模型（glTF/OBJ via assimp）的 PBR 渲染端到端尚无测试/演示**：
  导入链的数据层有集成测试（骨架/动画/morph/物理元数据），但“导入模型 →
  PBR 材质 → 渲染”没有自动化验证，这是当前覆盖面上唯一的明显缺口。
- **C 门户开通存在治理门槛**：`STRUCTURE_STABILIZATION_R0.md` 与
  `NATIVE_ABI_PLAN.md` 明确“C ABI 冻结、不默认构建、不扩张接口；新导出函数
  需引擎级用例 + 回归测试”。全面导出前需要显式解除该冻结并更新文档。

## 1. 底层资源层

### 已核实

- `VBO`/`EBO`/`VAO`/`Texture` 均为构造创建、析构自删（`glDelete*`）；
  `EBO` 有容量跟踪与越界校验，`VBO::Upload` 全量上传。
- `Shader` 编译失败/`Program` 链接失败抛异常；`Program::GetUniformLocation`
  对 -1 抛错（fail-fast），uniform 名缓存避免重复查询。
- `ProgramCache` 按 (vertex, fragment) 路径键控，一个 Application 一个缓存。
- `GraphicsDevice` 持有 `ProgramCache` + context token；`ReleaseAll()` 目前
  仅清程序缓存。**Texture/VAO/VBO/EBO/FBO 未向设备注册，仍自删**。
- `RenderStateScope` 追踪 8 个专用纹理单元与主要 GL 状态；stencil
  func/op/mask 不在其内，由各 pass 自行恢复（地面影 pass 已正确恢复，
  并修复了 fill pass 关闭 depth test 未恢复的回归）。
- `SceneFramebuffer` 现为 color + `DEPTH24_STENCIL8`（模板供地面影掩码），
  OIT 复用其深度附件；`EnvironmentMap::Attach` 惰性初始化，IBL 预计算一次。

### 问题

| 编号 | 严重度 | 位置 | 说明与建议 |
|---|---|---|---|
| R1-01 | Info | graphics_device / 各 GL 对象 | 全量所有权（对象向设备注册、统一销毁）未实现。当前靠 WindowManager 有序释放保证安全；C 门户若出现跨上下文/跨线程销毁，需要 context 校验或统一注册。 |
| R1-02 | Info | renderer `meshVertexArrays` | VAO 缓存按 `Mesh*` 键控、无失效钩子。当前 `ResourceManager` 无卸载路径（生命周期覆盖 Renderer），安全；未来若加模型卸载，必须配套缓存失效。 |
| R1-03 | Info | render_state | stencil 细粒度状态不在 RenderStateScope 内，依赖各 pass 自律；建议后续纳入或保持 pass 内 RAII。 |

## 2. 平台层

### 已核实

- `ManagedWindow` 成员序 `window → renderer → framebuffer`，逆序析构保证
  framebuffer/renderer 先于窗口上下文销毁（关窗 GL 错误已修复）。
- GLFW 进程级引用计数（`GlfwApplicationCount`），多 Application 不会互相
  terminate。
- `Application::Tick` 显式帧管线：animation → pre-physics → physics →
  post-physics → transforms → render；`PollEventsAndRender` 委托于它。
- 窗口创建/销毁在 GLFW owner 线程（`RequireOwnerThread`），frame 边界提交。

### 问题

| 编号 | 严重度 | 位置 | 说明与建议 |
|---|---|---|---|
| R1-04 | Major | native `wisteria_window_destroy` | `WindowManager::DestroyWindow` 只置 `should_close`，实际资源释放延迟到下一帧 `DestroyClosedWindows`；native 层立即擦除 `WindowEntry`，造成“句柄失效但窗口/资源活到下一帧”。若前端 destroy 后不再 poll，资源滞留到 Context 析构。建议：非 running 时同步销毁，或文档化“延迟到下一帧”语义。 |
| R1-05 | Info | application | 多窗口共享一个 ResourceManager/GraphicsDevice，跨窗口 share group 一致。 |

## 3. 资源/场景层

### 已核实

- `Mesh`：静态 VBO/EBO + 动态上传（`glBufferSubData` 同一 VBO，VAO 引用安全）；
  morph target 元数据校验（越界/NaN/重复）；皮肤调试数据。
- `Material`：默认 `MaterialData` 带 chessboard 贴图 + basicTex shader 契约；
  `groundPlane` / `receivesGroundShadow` / cast/receive 标志齐备。
- `ResourceManager`：mesh/material/model/texture 缓存，`BindGraphicsDevice`
  共享程序缓存；PMX 导入（Saba + assimp 兼容字节）透传 morph/IK/physics 标志。
- `Scene`/`Entity`：变换、可见性、灯光、环境、Behaviour。

### 问题

| 编号 | 严重度 | 位置 | 说明与建议 |
|---|---|---|---|
| R1-06 | Info | material.cpp | 贴图从 unit 0 顺序绑定，渲染器专用 unit 8–15；MMD 最多 3 贴图无冲突。自定义材质 >8 贴图会撞专用单元，建议文档化上限。 |
| R1-07 | Coverage | tests / demo | 导入模型（glTF/OBJ）的 **PBR 渲染端到端**无自动化验证（只有程序化 PBR 的 render-fbo 测试与导入数据层的集成测试）。建议补：加载测试 glTF/资产 OBJ → PBR 材质 → 渲染 → 像素断言。 |

## 4. 运行时层

### 已核实

- `SabaMmdRuntimeModel::Update` 相位：VMD Evaluate（含 IK override 注入）→
  morph → `UpdateNodeAnimation(false)`（pre-physics append）→
  `UpdatePhysicsAnimation` → `UpdateNodeAnimation(true)`（post-physics append）
  → `model->Update()`（蒙皮）。符合 MMD 语义；after-physics append 由 saba
  处理，引擎侧无缺口。
- `SabaOwnedPhysicsInstance::OwnsSimulationStep()==true`，Scene 共享步进正确
  跳过，避免双步进。
- `SetPhysicsSettings` 支持 fixed timestep/max substeps/gravity/damping/CCD，
  为 #5 社区物理预设提供入口。
- 灯光上传（directional/point/spot + 环境 IBL）与 `ShaderInterface` 契约一致。

### 问题

| 编号 | 严重度 | 位置 | 说明与建议 |
|---|---|---|---|
| R1-08 | Minor | mmd.frag | CSM 采样固定 bias 0.003，极端尺度/角度下可能有 shadow acne / peter-panning。优先级低，建议后续可配置化。 |

## 5. 渲染层

### 已核实

- opaque 三段顺序：groundPlane/接收面（仅 groundPlane 带 polygon offset）→
  地面影（stencil 掩码 + 单次平铺）→ 其余不透明体。角色正确遮挡阴影，
  阴影均匀且不覆盖角色下本身。
- CSM 4 级联（视锥适配光空间）+ PCF（`WISTERIA_SHADOW_MAP_SIZE` /
  `WISTERIA_SHADOW_PCF_RADIUS`）；`CastSelfShadow` 过滤。
- 透明：OIT（独立 blend 检测 + GL3.3 回退），资源按尺寸缓存。
- PBR basicTex 链与 MMD toon 链双平台像素级一致（render-fbo 回归）。

## 6. 测试与治理

### 已核实

- 测试金字塔 4 层：unit / runtime / integration / render-fbo；双平台 CTest
  4/4 绿。render-fbo 回归覆盖：地面绕序可见性、渲染后 GL 无错误、阴影均匀性
  （1 vs 2 重叠投影者最暗像素一致）。
- `WISTERIA_GL_DIAGNOSTICS` / 截图 / `WISTERIA_ASSET_ROOT` 等诊断与部署机制。
- **治理冻结**：`STRUCTURE_STABILIZATION_R0.md` 与 `NATIVE_ABI_PLAN.md`
  声明 C ABI 为冻结的 opt-in 实验层：不默认构建、不扩张接口；新导出函数需
  引擎级用例 + 回归测试；前端不得主导 core 所有权/帧调度。

### 问题

| 编号 | 严重度 | 位置 | 说明与建议 |
|---|---|---|---|
| R1-09 | Major（治理） | docs | 全面导出 C 门户与冻结声明冲突。需显式更新两份文档（解除冻结、约定 v0.x 版本化与新导出门槛），否则后续导出无治理依据。 |
| R1-10 | Info | CMake | 双平台 `WISTERIA_BUILD_NATIVE=OFF`，无产出动态库；现有 v0.3 源码/头文件未在最近重构后编译验证（`wisteria_core`/`wisteria_platform` 拆分、GraphicsDevice 等改动后可能有接口漂移）。 |

## 7. C 门户开通前置条件（建议顺序）

1. **解除治理冻结**：更新 `STRUCTURE_STABILIZATION_R0.md` 与
   `NATIVE_ABI_PLAN.md`，确认“全面导出”为新决策，沿用“每新导出需引擎级
   用例 + 回归测试”的门槛。
2. **打通构建**：开 `WISTERIA_BUILD_NATIVE`，双平台编译现有 v0.3 薄壳，
   修复最近重构造成的接口漂移；补一个绑定冒烟测试（`native_integration`
   或扩展 integration 层）。
3. **修 R1-04**：native 窗口销毁语义（同步销毁或明确文档化延迟语义）。
4. **补 R1-07**：导入 PBR 模型渲染端到端验证。
5. **导出面（按价值排序）**：
   - 物理预设（mode 0/1/2、重力/阻尼/CCD/语义过滤）——与 #5 社区兼容直接衔接；
   - 渲染配置（阴影分辨率/PCF、地面影开关、材质接收标志）；
   - MMD 控制（IK 开关、morph 权重、VMD 相机加载）。

## 8. 与 #5（社区兼容）的关系

`MMD_PHYSICS_COMMUNITY_ADOPTION_PLAN.md` 路线：可复现社区基线 → 移植有来源的
quirks → WISTERIA 自适应增强 → 外部 profile。物理预设的 C API 导出是这条
路线的自然落点：基线矩阵稳定后，外部 JSON/TOML profile 通过 C 门户注入，
引擎侧无需继续堆模型名启发式。
