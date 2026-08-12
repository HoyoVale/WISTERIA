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
- 二进制载荷（checkpoint.serialize 字节、captureFrame 像素）
  一律 base64 字符串编码
- 错误码公式：code = -33000 - stableStatus
  （stable 状态码 1..17；OK 不产生错误对象）
- data.status 用稳定字符串名（wisteria_status_name / stable 命名）；
  data.detail 来自 last_error，单条 ≤ 4KB
- Windows stdio 二进制模式（_setmode）或 SDK 容忍 \r\n，契约定一种
- 单行上限 1MB；引擎写 stdout 同步阻塞即天然背压
- 引擎退出码：0 = 优雅；非 0 = 崩溃/错误
- hello 必须返回 defaultContext 句柄（跨工具传递用）
- capabilities 必须含 multiWindow / multiContext / defaultContext
- system.shutdown：先回 {"result":true}，flush 后再 exit 0
```

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
| `system.hello` / `ping` / `listMethods` / `describeMethod` | 桥接自实现 | bridge |
| `system.shutdown` | 回响应 → flush → destroy 全部 context → exit(0) | bridge |
| `context.create` | `wisteria_create_context` | legacy |
| `context.destroy` | `wisteria_destroy_context` | legacy |
| `context.lastError` | `wisteria_last_error_message` | legacy |

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
| `window.captureFrame`（v1 可选） | `wisteria_window_read_pixels` | legacy |
| `camera.set` / `pose` / `setSpeed` | `wisteria_window_set_camera` / `camera_pose` / `set_camera_speed` | legacy |

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
trace / 完整物理配置（stable v2）
C ABI 修改（本契约只翻译，不新增 C 面函数）
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
待用户/ChatGPT 复审后 FROZEN
```
