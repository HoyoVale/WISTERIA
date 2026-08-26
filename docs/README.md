# WISTERIA 文档索引

本目录是 WISTERIA 的工程知识库，分为两类：

- `architecture/`：架构契约、设计与计划。契约文档描述“系统必须怎样”；
  带 `FROZEN` / `CLOSED` 标记的文档不再轻易修改，修改应先更新对应验证报告。
- `validation/`：实现基线与验收报告。每份对应某个阶段（R0–R2）的
  实现结果、测试证据和遗留项。

> 阅读顺序建议：先读仓库根目录 `README.md`，再看
> `architecture/PROJECT_LAYOUT.md`，之后按下面的“核心契约”逐条进入。
> 验证报告是工程过程的留痕，不需要全部阅读。

---

## 1. 快速入口

| 文档 | 说明 |
| --- | --- |
| `FINAL_REPORT.md` | v1.0.0 最终收尾报告，项目全貌与后续路线 |
| `ASSETS.md` | 演示资产准备、目录布局与授权提醒 |
| `SDK.md` | Stable C ABI SDK 的安装、消费方式与版本规则 |
| `architecture/PROJECT_LAYOUT.md` | 工程目录职责、模块依赖方向和 include 规范 |
| `architecture/PHYSICS_LAYER_AUDIT.md` | 物理分层审计，Legacy path 与 Saba path 的边界 |
| `architecture/R1_3_MMD_COMPAT_CONTRACT.md` | 当前 MMD 兼容策略的 Phase 0A 冻结契约 |
| `architecture/R2_0_RENDER_ARCHITECTURE_CONTRACT.md` | 后端中立渲染架构契约 |
| `architecture/R1_TO_R2_BOUNDARY_AUDIT.md` | R1 → R2 边界审计 |
| `architecture/GLTF_ROUTE_AUDIT.md` | glTF / GLB / VRM 第二内容线审计与路线 |
| `architecture/PBR_AUDIT.md` | 通用 PBR 材质渲染审计与改进记录 |

---

## 2. 核心架构契约（按时间线）

| 契约 | 主题 | 状态 |
| --- | --- | --- |
| `R1_2_DETERMINISTIC_TIMELINE_CONTRACT.md` | 确定性时间线与物理回放 | 终版 |
| `R1_2B_RESTORE_STATE_CONTRACT.md` | 物理快照 RestoreState | 已冻结并实现 |
| `R1_2C_FRAME_CHECKPOINT_CONTRACT.md` | FrameCheckpoint 编排与等价性 | 已冻结 |
| `R1_3_MMD_COMPAT_CONTRACT.md` | MMD 物理兼容与后端治理 | Phase 0A 冻结 |
| `R1_3B_MMD_COMMUNITY_COMPARISON_CONTRACT.md` | MMD 社区实现对照 | Phase 0B |
| `R1_4_STABLE_RUNTIME_BOUNDARY_CONTRACT.md` | 稳定运行时边界 | 契约 |
| `R1_5_SECOND_DYNAMIC_RUNTIME_CONTRACT.md` | 第二动态运行时（Generic runtime） | 契约 |
| `R1_6_OFFLINE_OUTPUT_CONTRACT.md` | 确定性离线输出管线 | Phase 0A |
| `R1_6_PHASE0C_CONTRACT.md` | Renderer-facing visual state 完整性 | 契约 |
| `R1_6_PHASE0D_CONTRACT.md` | Explicit presentation authority | 契约 |
| `R1_6_PHASE0E_CONTRACT.md` | 确定性帧序列 | 契约 |
| `R1_7_HEADLESS_CONTEXT_CONTRACT.md` | 真 Headless context | 契约草案 |
| `R1_8_GENERIC_DETERMINISTIC_RUNTIME_CONTRACT.md` | 通用确定性运行时 | 契约草案 |
| `R1_9_STABLE_RUNTIME_RENDER_ABI_CONTRACT.md` | 稳定 Runtime / Render C ABI | Phase 0A 契约草案 |
| `R2_0_RENDER_ARCHITECTURE_CONTRACT.md` | 后端中立渲染架构 | Phase 0A 契约草案 |
| `R2_1_VULKAN_BACKEND_CONTRACT.md` | Vulkan 后端 | Phase 0A 草案，未实现 |

---

## 3. MMD / 物理专线

| 文档 | 说明 |
| --- | --- |
| `MMD_ADAPTER_REWRITE_PLAN.md` | MMD 适配层重写计划 |
| `MMD_COMPAT_INTERFACE.md` | MMD Compat 接口契约草案 |
| `MMD_MOTION_ORCHESTRATION.md` | 动作 / 相机 / 灯光接口设计 |
| `MMD_PERFORMANCE_ROADMAP.md` | MMD 性能路线图 |
| `MMD_PHYSICS_COMMUNITY_ADOPTION_PLAN.md` | 社区物理实现借鉴计划 |
| `MMD_PHYSICS_COMPAT_BASELINE.md` | 社区兼容基线 |
| `MMD_PHYSICS_PHASE_SEMANTICS.md` | 物理相位语义（Saba 路径） |
| `MMD_PHYSICS_ROBUSTNESS.md` | MMD 链路鲁棒性清单 |
| `MMD_PHYSICS_P0_BULLET275_COMPAT.md` | Bullet 2.75 约束兼容层实验 |
| `SABA_ADAPTER_INTERFACE.md` / `SABA_ADAPTER_PLAN.md` | Saba 承接接口与实现计划 |

---

## 4. Native C ABI / RPC

| 文档 | 说明 |
| --- | --- |
| `NATIVE_ABI_PLAN.md` | Native C ABI 门面计划 |
| `NATIVE_ABI_SURFACE.md` | 原生 C ABI 导出面 |
| `M4_WINDOW_ABI_DRAFT.md` | 窗口 C ABI 草案（实验接口） |
| `C_ABI_SAFETY_MATRIX.md` | C ABI 安全矩阵 |
| `R1_S_ABI_SAFETY.md` | R1.S C ABI Safety |
| `R1_9_STABLE_RUNTIME_RENDER_ABI_CONTRACT.md` | 稳定 Runtime / Render C ABI |
| `RPC_BRIDGE_DESIGN.md` | RPC 桥设计（已 design closed，转契约草案） |
| `RPC_BRIDGE_CONTRACT.md` | RPC 桥契约 v1 草案（未实现，P0 待拍板） |

---

## 5. 迁移、审计与支撑文档

| 文档 | 说明 |
| --- | --- |
| `REFACTOR_MIGRATION.md` | 模块目录重构迁移说明 |
| `STRUCTURE_STABILIZATION_R0.md` | 结构稳定化 R0 |
| `FULL_STACK_AUDIT_R1.md` | 全栈审查报告（R1 收口后） |
| `R0_RENDER_MANUAL_ACCEPTANCE.md` | R0 渲染人工验收 |
| `LINUX_SUPPORT_PLAN.md` | Linux / WSLg 支持计划 |
| `FRONTEND_INTEGRATION.md` | 前端接入与模型格式路线 |

---

## 6. 验证报告（validation/）

验证报告按阶段归档，命名规则为 `R<版本>[_PHASE<阶段>]_<类型>_<日期>.md`。
以下为各阶段的最终收口报告，其余为过程中间基线：

| 阶段 | 最终报告 |
| --- | --- |
| R1.3 | `R1_3_PHASE_0A_BASELINE_20260807.md` |
| R1.3B | `R1_3B_PHASE0B_COMPLETION_20260807.md` |
| R1.5 | `R1_5_FINAL_CLOSURE_20260808.md` |
| R1.6 | `R1_6_FINAL_CLOSURE_OFFICIAL_20260809.md` |
| R1.7 | `R1_7_FINAL_CLOSURE_OFFICIAL_20260809.md` |
| R1.8 | `R1_8_FINAL_CLOSURE_OFFICIAL_20260809.md` |
| R1.9 | `R1_9_PHASE0E_CLOSURE_20260810.md` |
| R2.0 | `R2_0_FINAL_ARCHITECTURE_CLOSURE_20260812.md` |

---

## 7. 文档约定

1. 契约文档只描述接口语义与验收标准；实现细节放入设计文档或源码注释。
2. 标记 `FROZEN` 的契约改动必须同时更新对应 `validation/` 报告并跑完整测试。
3. 新源码文件必须归属 `src/<module>` 与 `include/wisteria/<module>`，
   禁止继续使用无分类长列表。
4. 引入第三方实现需在 `third-party/` 中记录来源、commit、许可证与本地回归证据。
