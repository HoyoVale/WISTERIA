# WISTERIA RPC 桥接契约（wisteria_rpc + Node SDK）（v1）

> 状态：**DRAFT CONTRACT v1（待用户/ChatGPT 复审后 FROZEN）**。
> 规范源：`docs/architecture/RPC_BRIDGE_DESIGN.md`（§0–12，五轮审查已
> 并入设计）。本契约只冻结接口语义、映射与验收；实现细节以设计文档为准。

## 0. 决策记录（已拍板）

```text
部署形态      独立进程（引擎 exe + Node 客户端）
画面          引擎自己开窗；v1 不传像素帧
传输          JSON-RPC 2.0 over stdio
窗口          v1 支持多窗口（逐窗口 render / window.closed 带句柄）
Context       v1 支持多 context（默认 context + 显式 create/destroy）
daemon        后置（v1 用 rpc 会话；协议已预留 hello/ping/EOF）
包名          wisteria-cli
最小 ABI 包   二进制 + shaders（模型资产不入包）
```

## 1. 协议（冻结语义）

引用设计 §1（帧格式 / 管道分工 / 握手 / 请求响应错误事件 / 错误码 /
类型映射 / 传输健壮性）。冻结要点：

```text
- JSONL：每行一个 JSON 对象；stdout 只跑协议，stderr 只跑日志
- hello 必须先行：protocolMajor/protocolMinor + abiVersion +
  capabilities + methods；Major 不兼容即拒绝
- 句柄/帧号/physicsTick/hash 一律 JSON 十进制字符串
- 客户端 v1 只发请求（id 必填），通知仅引擎 → 客户端
- id 冻结为数字（客户端单调递增）
- 二进制载荷（v1 仅 checkpoint.serialize/deserialize 字节）
  一律 base64 字符串编码，冻结为：RFC 4648 standard Base64、
  带 = padding、无换行、非 base64url
- 错误码公式：code = -33000 - stableStatus
  （stable 状态码 1..17；OK 不产生错误对象）
- data.status 冻结为下方 17 个字符串名（bridge 自实现该表，
  不依赖未来 helper）；data.detail 来自 last_error，单条 ≤ 4KB
- Windows framing（冻结）：引擎 MUST _setmode(stdin/stdout, O_BINARY)；
  wire 定界符只允许 LF(0x0A)；SDK parser MAY 防御性容忍 CRLF，
  但引擎 MUST 只发 LF
- 单行上限 1MB；引擎写 stdout 同步阻塞即天然背压
- 引擎退出码：0 = 优雅；非 0 = 崩溃/错误
- hello 必须返回 defaultContext 句柄（跨工具传递用）
- capabilities 只含 multiWindow / multiContext；
  defaultContext 是 hello 顶层 params（不放 capabilities）
- defaultContext 创建时序（冻结）：
  process starts → init RPC transport → create default context
  → 成功才发 engine.hello{defaultContext} → ready；
  失败：不发 hello，stderr 记录原因，exit non-zero
  （客户端的 hello 10s timeout 语义以此为准）
- system.shutdown：先回 {"result":true}，flush 后再 exit 0
- stdin EOF：不回响应 → destroy 全部 context → exit 0；
  success 响应发出后的 cleanup 失败只能写 stderr，
  不得再发第二个 RPC error
```

### 1.8 data.status 冻结字符串表（bridge 自实现）

```text
1  INVALID_ARGUMENT        9  UNSUPPORTED_REPLAY_PROFILE
2  NOT_FOUND              10  INVALID_STATE
3  IO                     11  NON_SEQUENTIAL_FRAME
4  PARSE                  12  DETERMINISM_VIOLATION
5  INITIALIZATION         13  SNAPSHOT_MISMATCH
6  ALREADY_EXISTS         14  INVALID_SNAPSHOT
7  INTERNAL               15  POISONED
8  INVALID_CHECKPOINT     16  NO_PHYSICS
                          17  UNSUPPORTED
```

例：code = -33015 永远对应 `"status":"POISONED"`。

## 2. 引擎侧桥接（冻结语义）

引用设计 §2。冻结要点：

```text
- 控制类 method 走 native C API；协议类 method 桥接自实现
- 底座优先 stable v1；缺失用 legacy v0.7（标注实验性）
- 显式渲染：window.render({window}) 才推进一帧
- 多窗口：create 可多次；窗口状态隔离；window.closed 携带句柄
- 多 context：context 参数可选（缺省默认 context）；默认禁销毁
- 单线程顺序执行；错误统一 -33000 区映射
```

## 3. method ↔ C 函数映射表

### 3.1 system / context

| RPC method | C 函数 | ABI 域 |
| --- | --- | --- |
| `system.ping` / `listMethods` / `describeMethod` | 桥接自实现 | bridge |
| `system.shutdown` | 回响应 → flush → destroy 全部 context → exit(0) | bridge |
| `context.create` | `wisteria_create_context` | legacy |
| `context.destroy` | `wisteria_destroy_context` | legacy |
| `context.lastError` | `wisteria_last_error_message` | legacy |

注：启动通知方法名为 `engine.hello`（server → client，无 id）；
`system.hello` 不存在，v1 不提供“重新获取 hello payload”的请求方法。

### 3.2 model / motion / physics（headless 面，可选/别名）

| RPC method | C 函数 | ABI 域 |
| --- | --- | --- |
| `model.load` | `wisteria_load_model` | legacy |
| `model.unload` | `wisteria_unload_model` | legacy |
| `motion.load` / `unload` | `wisteria_load_motion` / `unload_motion` | legacy |
| `motion.play` / `pause` / `resume` | `wisteria_play_motion` / `pause_motion` / `resume_motion` | legacy |
| `motion.setLooping` / `setFrame` / `frame` / `maxFrame` | `wisteria_set_motion_looping` / `set_motion_frame` / `motion_frame` / `motion_max_frame` | legacy |
| `physics.setSettings` / `setPreset` / `reset` / `capabilities` | `wisteria_set_physics_settings` / `set_physics_preset` / `physics_reset` / `physics_capabilities` | legacy |
| `model.setMmdIk` / `findBone` / `loadCameraMotion` / `vertexBounds` | 同名 `wisteria_*` | legacy |

### 3.3 scene / entity / light / primitives

| RPC method | C 函数 | ABI 域 |
| --- | --- | --- |
| `scene.create` / `destroy` | `wisteria_scene_create` / `destroy` | legacy |
| `scene.loadModel` / `unloadModel` | `wisteria_scene_load_model` / `unload_model` | legacy |
| `scene.instantiate` | `wisteria_scene_instantiate_model` | legacy |
| `scene.setEnvironment` | `wisteria_scene_set_environment` | legacy |
| `entity.setTransform` / `getTransform` / `setVisible` / `getVisible` / `setPartColor` / `destroy` | 同名 `wisteria_entity_*` | legacy |
| `entity.setMorphWeight` / `getMorphWeight` / `setMmdIk` / `boneCount` / `boneName` / `boneLocalMatrix` / `runtimeBackend` | 同名 `wisteria_entity_*` | legacy |
| `entity.loadMotion` / `unloadMotion` / `pause` / `resume` / `restart` / `setMotionFrame` / `setMotionLooping` / `motionFrame` / `maxFrame` / `physicsReset` / `setPhysicsSettings` / `vertexBounds` | 同名 `wisteria_entity_*` | legacy |
| `light.addDirectional` / `addPoint` / `addSpot` / `set*` / `get*` / `destroy` | 同名 `wisteria_*_light*` | legacy |
| `scene.addCube` / `addGroundPlane` / `addSphere` / `addCylinder` / `addCapsule` / `addCone` / `addTorus` | 同名 `wisteria_scene_add_*` | legacy |

### 3.4 window / camera

| RPC method | C 函数 | ABI 域 |
| --- | --- | --- |
| `window.create` / `createHidden` / `destroy` | `wisteria_window_create` / `create_hidden` / `destroy` | legacy |
| `window.render` | `wisteria_window_poll_and_render` | legacy |
| `window.shouldClose` / `framebufferSize` / `setRenderSettings` | 同名 `wisteria_window_*` | legacy |
| `camera.set` / `pose` / `setSpeed` | `wisteria_window_set_camera` / `camera_pose` / `set_camera_speed` | legacy |

注：`wisteria_window_read_pixels` 保留在 legacy C API，但**不进 RPC v1**
（与“v1 不传像素帧”一致；像素走未来数据通道，设计附录 B）。

### 3.5 timeline / checkpoint（stable 域）

| RPC method | C 函数 | ABI 域 |
| --- | --- | --- |
| `timeline.prepareFrameZero` | `wisteria_stable_entity_prepare_frame_zero` | stable |
| `timeline.stepExact` | `wisteria_stable_entity_step_exact` | stable |
| `timeline.replayExact` | `wisteria_stable_entity_replay_exact` | stable |
| `timeline.setPreviewFrame` | `wisteria_stable_entity_set_preview_frame` | stable |
| `checkpoint.create` / `restore` / `destroy` / `info` | `wisteria_stable_checkpoint_*` | stable |
| `checkpoint.serialize` / `deserialize` | `wisteria_stable_checkpoint_serialize` / `deserialize` | stable |
| `entity.setMorphOverride` / `clearMorphOverride` / `clearAllMorphOverrides` / `capabilities` / `assetFingerprint` | `wisteria_stable_entity_*` | stable |

注：`checkpoint.replay` 没有单一 C 函数，定义为
`checkpoint.restore` + `timeline.replayExact` 的组合（桥接层编排）。

## 4. 类型与 schema 约定

```text
类型映射沿用设计 §1.5（vector3 数组 / transform 对象 / uint64 字符串）
二进制载荷 = base64 字符串（UTF-8 安全，JSON 标准）
完整 schema 由 system.listMethods / describeMethod 运行时提供
实现硬约束：schema 驱动 handler 参数校验器（§5.2.10 防漂移）
```

## 5. Node SDK（冻结语义）

引用设计 §3。冻结要点：

```text
包名 wisteria-cli；src/client（fake transport 可注入）、api、cli、schema
双形态：一次性命令 + rpc 交互会话
client 串行化请求队列；崩溃 reject + stderrTail；超时可配
默认 context：方法省略 context 参数走默认；多 context 显式传
```

## 6. 生命周期（冻结语义）

引用设计 §4。冻结要点：spawn → hello → ready → running → shutdown；
stdin EOF 优雅退出；崩溃时 pending 全部结构化 reject；
失败不承诺回滚；POISONED 原样透传 + 恢复入口。

```text
超时：hello 10s；请求默认 30s（可配）
清理语义：destroy scene → 其下实体句柄 NOT_FOUND；
          destroy window → 该窗口句柄 NOT_FOUND（复用 C ABI 语义）
```

## 7. 最小 ABI 包（冻结语义）

引用设计 §12。包布局：`bin/`（wisteria_rpc + Windows wisteria_native.dll）
+ `assets/shaders/` + README/LICENSE/THIRD_PARTY_NOTICES。
路径解析：`WISTERIA_ENGINE_PATH` → SDK 包内 `../bin/` → PATH；
`WISTERIA_ASSET_ROOT` 由 SDK 显式设为包内 assets 绝对路径。

## 8. stable v2 新增函数清单（不在 v1）

```text
trace.*        引擎侧轨迹导出/差分（当前仅 C++，未进 C 面）
physics 完整配置  compat/adaptive 逐字段 + 配置指纹
窗口/场景提升    常用 window/scene 函数从 legacy 提升到 stable
```

## 9. 测试与验收矩阵

```text
CTest：wisteria.rpc-protocol / rpc-golden / 协议 fuzz（设计 §5.2–5.3）
Node：npm test（fake transport 单测 + 包布局集成测试）
环境：Windows + Linux；window.closed 用例挂真实窗口标签；
      fuzz 在 build-asan 下跑
回归：既有四矩阵（CORE/FULL_ASSETS × Windows/Linux）全绿
冻结标准：fuzz 不崩溃、golden 跨进程一致、schema 与实现一致、
         npm test 全绿、四矩阵不回归
```

## 10. 非目标 / 边界

```text
像素帧传输（大数据阶梯，设计附录 B）
daemon 常驻服务（后置）
批量请求 / 取消 / 鉴权（v1 不做）
窗口输入事件（key/mouse/cursor/scroll）不暴露（AI 控制不需要）
window.captureFrame 不在 v1（read_pixels 留 legacy，像素走未来数据通道）
trace / 完整物理配置（stable v2）
C ABI 修改（本契约只翻译，不新增 C 面函数）
```

## 11.5 P0：Entity 双世界（BLOCKER，待拍板）

已核实（`native_scene.cpp` / `stable_native_context.hpp`）：

```text
legacy 场景实体：SceneEntry::entities
  ← wisteria_scene_instantiate_model 产生
stable 运行时实体：StableContextState::entities
  ← wisteria_stable_entity_create 产生；FindStableEntity 只查这张表

契约 §3.3/§3.5 隐含链：
  scene.instantiate → legacy entity → timeline.stepExact
  （stable 函数）→ 真实代码返回 NOT_FOUND
```

方案（二选一，用户拍板）：

```text
A) v1 分裂两个实体域
   legacy sceneEntity：window/scene/transform/visible/legacy motion
   stable runtimeEntity：exact timeline/checkpoint/morph override/fingerprint
   新增 RPC 入口 runtime.create → wisteria_stable_entity_create
   两类句柄明确禁止交叉使用（不改 C ABI）
   代价：窗口里的实体 ≠ deterministic 实体（同一模型要加载两份）

B) 统一实体域（推荐，若目标是“AI 控制真实窗口场景”）
   需先在 stable C 面新增：把 scene/window entity 投影/桥接到 stable
   runtime（或提升 scene/window 到 stable v2），
   使窗口实体可直接 timeline/checkpoint
   代价：v1 需要新增 stable C 函数（§8 提前），本契约 §10
        “不新增 C 面函数”需相应修订
```

## 11. 评审记录

```text
设计五轮审查（2026-08-12）已并入 RPC_BRIDGE_DESIGN.md §6–§11
契约 v1 已做第六轮契约化审查（2026-08-12）：
  P1-1 二进制载荷编码     → base64 字符串（§1/§4）
  P1-2 defaultContext    → hello 返回句柄（§1）
  P1-3 错误码公式        → code = -33000 - stableStatus（§1）
  P1-4 shutdown 顺序     → 先响应 → flush → exit 0（§1）
  P2-1 窗口输入不暴露     → §10 非目标
  P2-2 capabilities 补位 → multiWindow / multiContext / defaultContext（§1）
  P2-3 超时与 detail 上限 → hello 10s / 请求 30s / detail ≤4KB（§6）
  P2-4 id 类型          → 数字单调递增（§1）
  P2-5 清理语义         → destroy 后句柄 NOT_FOUND（§6）
映射表交叉核对：契约中所有具体 C 函数名均存在于真实头文件
第七轮外部复审（2026-08-12）已并入：
  P0   Entity 双世界（stable/legacy 不互用）→ §11.5 BLOCKER 待拍板
  P1-1 system.hello 冲突            → 已删，仅保留 engine.hello
  P1-2 defaultContext 时序          → §1 冻结创建顺序 + 失败退出
  P1-3 captureFrame 矛盾            → 已从 v1 移除
  P1-4 Windows framing 二选一       → §1 冻结 _setmode + LF only
  P1-5 data.status 未机器冻结       → §1.8 冻结 17 字符串表
  minor：modelHandle "42" 示例、base64 RFC4648、EOF/cleanup 语义
待 P0 拍板 + 用户/ChatGPT 复审后 FROZEN
```
