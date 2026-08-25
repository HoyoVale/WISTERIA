# WISTERIA RPC 桥接设计（wisteria_rpc + Node SDK）（Draft v1）

> 状态：**DESIGN CLOSED（2026-08-12）**。已转正式契约草案：
> [RPC_BRIDGE_CONTRACT.md](RPC_BRIDGE_CONTRACT.md)。
> 本文档基于 brainstorming 已确认的决策；与现有架构的关系见附录 A。

## 0. 背景与目标

WISTERIA 需要一个对外（尤其面向 AI 工具）的稳定控制入口：Node.js
命令行前端通过它加载模型、控制场景、推进帧、读取状态，画面由引擎
自己开窗显示。已确认的决策：

```text
1. 部署形态：独立进程（引擎 exe + Node 客户端），不做同进程插件
2. 画面：引擎自己开窗显示；v1 不传像素帧
3. 传输：JSON-RPC 2.0 over stdio（方案 A）
4. 控制面/数据面分离：stdio 永远只跑控制命令，大数据走可插拔第二通道
5. 形态：一次性命令 + 交互式 rpc 会话双入口（AI 长会话避免重复加载）
6. 部署：先定义最小 ABI 包（bin + shaders），模型资产不入包（§12）
```

## 1. 协议格式（JSON-RPC 2.0 over stdio）

### 1.1 帧格式与管道分工

```text
stdin  ← 请求（客户端 → 引擎）
stdout → 响应 + 事件（引擎 → 客户端）
stderr → 日志（不参与协议）
```

JSONL：每行一个 JSON 对象；请求/响应/事件/错误都是单行 JSON。

日志纪律（实现硬约束）：RPC 模式下引擎**所有**日志（含 spdlog）必须
只走 stderr；stdout 是协议流，任何日志混入都会污染 hello/响应解析。
hello 是脆弱点——启动期日志最容易漏配 sink。

通知方向：v1 客户端只发请求（id 必填），不发通知；通知仅允许
引擎 → 客户端（window.closed 等）。

### 1.2 握手

引擎以 `--rpc` 启动后先发 `engine.hello`（通知，无 id）：

```json
{"jsonrpc":"2.0","method":"engine.hello","params":{
  "protocolMajor":1,
  "protocolMinor":0,
  "engineVersion":"1.0.0",
  "abiVersion":{"legacy":"0.7","stableRuntime":1,"stableRender":1},
  "defaultContext":"1",
  "capabilities":{"window":true,"headless":true,"checkpoint":true,"trace":false,"multiWindow":true,"multiContext":true,"dataChannel":"none"},
  "methods":["system.ping","system.listMethods","system.describeMethod","system.shutdown",
             "context.create","model.load","motion.setFrame","window.render",
             "timeline.prepareFrameZero","checkpoint.create"]
}}
```

客户端必须等 hello 才能发请求。版本规则：客户端只接受
`protocolMajor == 客户端支持版本`（Major 不兼容即拒绝）；
`protocolMinor` 允许客户端 <= 服务端（additive 兼容）。

### 1.3 请求 / 响应 / 错误 / 事件

```json
{"jsonrpc":"2.0","id":1,"method":"model.load","params":{"path":"C:/x.pmx"}}
{"jsonrpc":"2.0","id":1,"result":{"modelHandle":"42"}}
{"jsonrpc":"2.0","id":1,"error":{"code":-33001,"message":"model load failed","data":{"status":"INVALID_ARGUMENT","detail":"file not found"}}}
{"jsonrpc":"2.0","method":"window.closed","params":{"window":"42"}}
```

### 1.4 错误码约定

```text
-32700 解析错误      -32600 非法请求      -32601 方法不存在
-32602 参数错误      -32603 内部错误
-33000 起           引擎状态码区（WisteriaStatus 一一映射）
```

### 1.5 类型映射

```text
vector3      → [x, y, z]
transform    → {"position":[x,y,z],"rotationBasis":[9 个浮点]}
uint64 字段  → JSON 十进制字符串（句柄 / 帧号 / physicsTick / hash）
               ——JSON 数字是 double，JS Number 超过 2^53 会丢精度
               （v1 强制字符串，不做 <2^53 的隐性限制）
浮点/整数帧   → 确定性帧用整数（MotionFrameIndex）；预览帧用浮点
字符串       → UTF-8 JSON 字符串
```

确定性约定：Pose/Physics/Vertex hash 与 checkpoint 指纹**永远以引擎
返回值为准**；客户端不做浮点重算或自行比较（精度与版本都由引擎侧
R1.2 定义保证）。

### 1.6 v1 明确不做

```text
批量请求（batch）
取消/超时重试语义
二进制帧通道
鉴权（本地单用户）
```

### 1.7 传输层健壮性（新增）

```text
Windows stdio 二进制模式（冻结，外部复审 2026-08-12）：
  引擎 MUST _setmode(_fileno(stdin)/_fileno(stdout), _O_BINARY)；
  wire 定界符只允许 LF(0x0A)；SDK parser MAY 防御性容忍 CRLF，
  但引擎 MUST 只发 LF

id 策略：
  客户端 id 单调递增；未知 id 的响应记录并忽略；
  引擎单线程顺序执行 → 响应按请求顺序返回，SDK 可串行化队列

行大小上限：
  单行上限 1MB（防误传超大路径/恶意输入）；超限 → -32700

背压：
  引擎写 stdout 同步阻塞即天然背压；SDK 必须持续读取 stdout，
  不能只等 promise

崩溃半行：
  进程退出时 stdout 可能残留不完整行——SDK 丢弃尾部残缺行，不得死循环

退出码约定：
  引擎：0 = 优雅退出，非 0 = 崩溃/错误；
  SDK 对外统一 0/1/2（成功/引擎错误/用法错误），内部保留引擎原始码
```

## 2. 引擎侧 `--rpc` 桥接

### 2.1 入口

新增小可执行目标 `wisteria_rpc`，不动 `wisteria.exe`（demo）与核心架构。
启动方式：`wisteria_rpc --rpc`。

### 2.2 桥接原则

控制类 method 背后**直接调用现有 native C API**（`wisteria_native` /
`wisteria_stable_runtime` 导出函数），桥接只是翻译器：

```text
- 零引擎新逻辑
- 每笔调用持续验证 C ABI 本身（桥接即 dogfood）
- C ABI 加函数 → 桥接表加一行
```

例外与缺口（不能假装全部翻译）：

```text
协议类 method（hello / listMethods / describeMethod / shutdown）
  → 由桥接自身实现，不映射 C 函数
底座选择：优先 stable v1（冻结）；stable 没有的用 legacy v0.7
  （标注实验性）；两者都没有的列为“需新增 C 函数（stable v2）或延迟”
```

### 2.8 stable / legacy 覆盖矩阵（新增，决定 v1 底座）

```text
stable v1 覆盖：context / entity（创建、动作、确定性 timeline、
                morph override）/ checkpoint / render session
legacy v0.7 覆盖：window / scene / light / 图元 / headless model /
                 physics settings / camera / readPixels

含义：v1 RPC 的 window.* / scene.* / light.* / primitives.* 只能站
legacy（实验性）。两个选择：
  a) 接受现状，legacy 组在契约里标注“实验性，可能变化”；
  b) 先提升常用窗口/场景函数到 stable v2 再冻结 RPC
建议 a（RPC 先行，stable v2 提升单独排期）。
```

### 2.3 方法分组（与 C API 分组一一对应）

方法名是**扁平字符串**（如 `model.load`、`motion.setFrame`、
`timeline.prepareFrameZero`）；分组只是组织视图，不是命名层级。

```text
system.*      listMethods / describeMethod / shutdown
              + system.ping（健康检查，AI 调试/保活用）
              （启动通知是 engine.hello，不是 system.hello）
context.*     create / destroy / lastError
model.*       headless 模型面（可选/别名；v1 以 scene/entity 为主面）
physics.*     setSettings / reset / capabilities
scene.*       create / destroy / loadModel / instantiate / addLight / addPrimitive
entity.*      transform / visible / morph / destroy
camera.*      set / get
window.*      create / createHidden / render / shouldClose
              （captureFrame 不在 v1：read_pixels 留 legacy，
               像素走未来数据通道）
timeline.*    prepareFrameZero / stepExact / replayExact / setPreviewFrame
              （映射 stable：wisteria_stable_entity_*，确定性回放核心）
checkpoint.*  create / restore / replay（复用 R1.2C）
              + serialize / deserialize（stable 已有，v1 暴露并测）
trace.*       明确延迟到 v2（C ABI 当前无导出；不得在 v1 假装可用）
```

物理配置边界：`physics.*` 目前只能映射 legacy 的
`set_physics_settings / set_physics_preset / physics_capabilities`；
R1.3 的 compat/adaptive 逐字段配置与配置指纹**未进 C ABI**，列为
stable v2 缺口。

### 2.4 窗口与命令循环共存（显式渲染）

```text
主线程单循环：读一行命令 → 分发 → 响应
客户端调 window.render → 引擎执行一次 poll + render
```

无后台渲染线程，跨平台无竞态；窗口只在被要求时刷新，与确定性时间线
哲学一致。持续动画由客户端循环调 `window.render`；后台线程渲染留作后续。

v1 窗口策略（2026-08-12 用户拍板）：**支持多窗口**。

```text
window.create 可多次调用，每次返回独立窗口句柄（可见或 createHidden 离屏）
window.render({window}) 只渲染指定窗口
  ——镜像 legacy wisteria_window_poll_and_render 的逐窗口语义
window.closed 事件携带 window 句柄，客户端可区分哪个窗口关闭
窗口间状态隔离（各自 camera / render settings / input）；
场景可以共享（沿用 BindScene 语义）
```

### 2.5 状态所有权与关闭

```text
引擎进程持有 Context/Window/Scene/Model 句柄，跨命令持久
system.shutdown → 统一销毁并退出 0
stdin 读到 EOF → 同样视为优雅关闭
```

### 2.6 AI 可发现性

```text
system.listMethods    → 全部方法清单（含参数/结果 schema）
system.describeMethod → 单个方法详情（说明 + 示例）
```

### 2.7 错误安全

复用 native 层异常边界（`InvokeAbi` / 状态码）；任何 handler 异常都变成
`-33000` 区错误码返回，绝不把崩溃漏给客户端。

## 3. Node SDK 结构

### 3.1 目录

```text
sdk/node/
  package.json          包名建议 wisteria-cli
  bin/wisteria-cli.js   CLI 入口（one-shot + rpc 会话）
  src/client.js         传输层：spawn + JSONL 解析 + 请求关联
  src/api.js            类型化封装：loadModel / setMotionFrame / ...
  src/schema.js         方法/参数 schema（与引擎 listMethods 对齐）
  src/types.js          transform / vector3 / 句柄 编解码
```

### 3.2 client.js（传输层）

```js
engine = await WisteriaClient.spawn(enginePath, { timeoutMs: 10000 })
//   spawn(engine, ['--rpc'], {stdio: ['pipe','pipe','pipe']})
//   按行解析 stdout；id → pending Map
//   等 hello（版本/能力/方法列表）→ ready
//   request(method, params) → Promise<result>
//   notify(method, params) → 事件（window.closed 等）
//   shutdown() → 优雅退出；exit/close → reject 所有 pending + 报错
```

stderr 一律转发日志，绝不参与协议。

多 Context（2026-08-12 用户拍板）：支持多个 context，语义镜像 C ABI。

```text
进程启动时先创建默认 context，成功后才发 engine.hello{defaultContext}；
失败不发 hello、stderr 记录、exit non-zero
方法参数里 context 可选：省略 → 默认 context；显式 → 指定 context
context.create 返回新句柄；context.destroy 销毁指定 context
默认 context 禁止销毁（INVALID_STATE），显式 context 可销毁
context 间状态隔离：A 加载的模型/窗口在 B 不可见
```

### 3.3 api.js（类型化封装）

```js
await engine.contextCreate();
await engine.loadModel({ path: "C:/x.pmx", motion: "C:/x.vmd" });
await engine.motionSetFrame({ model, frame: 120 });
await engine.windowRender({ window });
await engine.checkpointCreate({ model });
```

错误统一抛成 `{ code, message, data }` 结构化异常。

### 3.4 cli.js（双形态）

```text
形态 A：一次性命令
  wisteria-cli load-model --path x.pmx --json
  → spawn → hello → 发一个请求 → 输出 JSON → shutdown → 退出
  代价：每次冷启动引擎 + 重新加载模型

形态 B：交互式会话
  wisteria-cli rpc
  → 保持连接；stdin 每行一个 JSON 请求，stdout 响应/事件
  → 适合 AI 长会话，模型只加载一次

输出约定：--json 单行 JSON；默认人类可读；退出码 0/1/2
（成功 / 引擎错误 / 用法错误）
```

### 3.5 AI 友好

```text
启动即 hello（协议版本 + capabilities + methods）
system.listMethods / system.describeMethod 按 schema 调，不猜
错误码稳定（WisteriaStatus → -33000 区）
```

## 4. 生命周期与崩溃处理

### 4.1 引擎进程状态机

```text
spawned → hello（版本/能力）→ ready → running → shutting down → exited
                 ↓ 超时/版本不符 → 启动失败（报错并销毁进程）
```

### 4.2 启动

```js
spawn(enginePath, ['--rpc'])
waitHello({ timeoutMs: 10000 })
if (hello.protocolMajor !== clientSupportedMajor)   // Major 不兼容即拒绝
    throw EngineVersionMismatch
ready
```

### 4.3 正常关闭

```text
客户端主动：system.shutdown → 引擎销毁 Context/Window → exit 0
stdin EOF（客户端管道关闭）：引擎同样优雅退出，不留孤儿进程
Linux SIGTERM/SIGINT：v1 可选处理（默认由 OS 终止，SDK 按崩溃路径兜底）
```

SDK `shutdown()`：发 `system.shutdown` → 等退出（超时 3s）→ 超时才 kill。

### 4.4 崩溃检测

```js
engine.on('exit', (code) => {
    // 1. 所有 pending request 以结构化错误 reject：
    //    { code: 'ENGINE_EXITED', exitCode: code, stderrTail: '最后 20 行' }
    // 2. 触发 engine.on('crash') 事件，调用方决定是否重启
})
```

保证：**JSONL 一请求一行，引擎不会返回半截结果**——要么完整响应，
要么进程没了，没有“部分成功”歧义。

### 4.5 请求超时

```js
request(method, params, { timeoutMs })   // 默认 30s；模型加载可调大
```

超时只 reject 客户端等待，不杀引擎。

### 4.6 失败后的会话状态语义

```text
- 单个请求失败：会话保持可用，不自动销毁
- 失败不承诺回滚：调用方可再发查询确认状态，或重启会话
- 引擎创建类操作自带事务边界（R1.4）：失败不留幽灵实体/句柄
- 确定性入口返回 POISONED：SDK 原样透传，
  文档提示用 PrepareFrameZero / EvaluateTick(0, ResetAtTarget) 恢复
```

### 4.7 窗口关闭与重启

```text
引擎发 window.closed 事件，但进程继续活着（AI 可重建窗口或 shutdown）
默认不自动重启（AI 会话自己掌控）；SDK 提供 crash 事件 + 可选 restart()
```

### 4.8 资源上限提示（新增）

```text
v1 不设硬配额，但文档提示：
  会话内可加载多个模型，AI 应显式 unload；
  模型加载是重操作，重复加载同一模型是调用方责任；
  后续可加配额/缓存/路径 allowlist（daemon 化时一并设计，
  AI 工具调用任意文件路径的信任边界也在那时收紧）
```

## 5. 测试方案

### 5.1 分层

```text
引擎侧协议测试     CTest 驱动：Python 脚本 spawn wisteria_rpc --rpc，
                   发送脚本化 JSONL，断言响应/事件/错误
引擎侧确定性 golden  同一命令序列 → 跨会话 hash 一致（复用 R1.2 语义）
Node SDK 测试       node:test，对真实引擎二进制
协议 fuzz           坏 JSON / 超大行 / 未知 id / 乱序 / 客户端通知
```

### 5.2 引擎侧：脚本化会话测试（对齐 checkpoint_wire_cli 模式）

新增 CTest：`wisteria.rpc-protocol`；脚本
`script/rpc_protocol_test.py` spawn `wisteria_rpc --rpc`，驱动以下用例：

```text
1. 握手：hello 字段齐全（protocolMajor/Minor、engineVersion、
   abiVersion、capabilities、methods），capabilities.trace == false
2. 版本：protocolMajor 不兼容 → 客户端拒绝
3. 基本方法往返：context / model / motion / scene / entity / window /
   timeline / checkpoint 每组至少一个成功用例
4. 错误映射：INVALID_ARGUMENT / NOT_FOUND / IO / PARSE
   → -33000 区 + data.status + data.detail（来自 last_error）
5. 协议错误：未知方法 -32601；坏 JSON -32700；超大行（>1MB）拒绝
6. 客户端通知：v1 拒绝/忽略（只允许引擎→客户端通知）
7. 事件：window.render 后 shouldClose → window.closed 通知
8. 生命周期：system.shutdown → exit 0；stdin EOF → exit 0
9. Windows \r\n：_setmode 二进制或 SDK 容忍，跨平台同一脚本通过
10. schema 一致性：listMethods/describeMethod 返回的 schema
    与实际 handler 参数校验一致（防 schema 漂移；
    实现时用同一份 schema 驱动校验器与注册表）
11. stdout/stderr 隔离：hello 前故意注入 stderr 日志，
    断言 stdout 第一行仍是合法 JSON（§1.1 硬约束的回归）
12. 非 ASCII 路径：中文 PMX/VMD 路径加载成功（仓库实际资产即中文名）
13. 非有限值：NaN/Infinity 输入/输出编码约定测试
    （JS JSON.stringify(NaN) → null；引擎侧必须定义拒绝或报错，
    不得静默吞掉）
14. 迟到响应：客户端超时后，引擎迟到的响应被安全忽略
    （未知 id → 记录日志，不崩溃）
15. 错误后会话可用：一个失败请求后，下一个成功请求正常返回
    （§4.6 语义）
16. Poisoned 恢复：触发 POISONED 状态码 → prepareFrameZero 恢复 →
    后续确定性命令可用
17. 句柄销毁后复用：destroy 后再用该句柄 → -33000 区 NOT_FOUND
    （翻译层复测 C ABI 的 wrong-handle 拒绝语义）
18. 顺序压力：连续快速发送 100 个请求，无乱序、无丢失
19. checkpoint wire：RPC serialize → 字节交给新会话 deserialize →
    restore → 三 hash 一致（复用 R1.4 跨进程模式）
20. 多窗口：创建第二个窗口成功；各自 render / shouldClose / destroy；
    窗口句柄隔离（A.render 不影响 B）；window.closed 事件携带对应句柄
21. 多 context：create 第二个 context 成功；A/B 状态隔离
    （A 加载的模型在 B 中不可见）；省略 context 走默认；
    销毁显式 context 后旧句柄 → NOT_FOUND；销毁默认 context → INVALID_STATE

环境 gate：用例 7（window.closed）需要真实窗口；无头 CI（llvmpipe
createHidden）跳过或单独挂“有窗口环境”标签，避免不可执行。
```

### 5.3 引擎侧：确定性 golden

新增 CTest：`wisteria.rpc-golden`：

```text
场景：load pmx_physics → prepareFrameZero → stepExact(150)
      → checkpointCreate → 输出 pose/physics/vertex hash
断言：两个独立进程（非同进程两个 context）跑同一命令序列
      → 三个 hash 完全一致；命令序列固定（含 ReplayConfig）
VMD 变体：合成 VMD（CORE）；生产 VMD 挂 FULL_ASSETS tier →
          同一命令序列 hash 一致
形态一致性：同一命令序列在 one-shot 与 rpc 会话两种形态下
          → 三个 hash 一致（one-shot 每次冷启动也应确定性一致）
失败输出：hash 不一致时打印三组 hash + 第一分叉帧（可定位）
```

### 5.4 Node SDK 测试（sdk/node/test/，node:test）

```text
client：hello 超时 / 版本拒绝 / 请求-响应关联 / 请求超时 /
        事件分发 / 崩溃 reject（ENGINE_EXITED + stderrTail）/
        尾部半行丢弃
fake transport：client.js 必须可注入 mock spawn/stdio；
         超时 / 崩溃 / 迟到响应 / 半行 全部用 fake transport 做
         纯单元测试——不依赖真实引擎的不可控慢操作
api：   uint64 字符串编解码、transform/vector3 映射、
        结构化异常（code/message/data）
cli：   one-shot 退出码 0/1/2、--json 单行输出、
        rpc 交互会话 stdin/stdout 往返
并发：   10 个并发请求经 client 队列串行化，全部按序返回
迟到：   请求超时后，迟到响应不触发未定义行为
启动失败：引擎路径不存在 / 引擎启动即崩溃 → 结构化错误而非挂死
集成：  对真实引擎二进制（WISTERIA_ENGINE_PATH 或 CMake 传入目标路径）
```

### 5.5 门禁与验收

```text
CTest 新增：wisteria.rpc-protocol / wisteria.rpc-golden / 协议 fuzz
Windows + Linux 双平台（llvmpipe 下 createHidden 无头路径）
Node 侧 npm test 在本地发布前必须全绿
fuzz 确定性：固定坏样本表 + 可选随机种子（CI 用固定种子防 flaky）
时间敏感测试：超时用例用短超时 + 可控慢路径/注入，不真实等 30s
      （引擎侧不做不可控慢操作；SDK 侧用 fake transport）
sanitizer：协议 fuzz 在 build-asan（ASan+UBSan）下跑一遍
时长预算：协议 + golden 全套 < 60s（用 pmx_physics 小 fixture）
环境 gate：真实窗口用例（window.closed）单独标签，无头环境跳过
CI 归属：本地四矩阵 + npm test；.github 未配时文档注明手动门禁
验收：fuzz 不崩溃、golden 跨会话一致、schema 与实现一致、
      既有四矩阵不回归
```

## 6. 待办 / 待评审

```text
1. 第 5 段测试方案已写入本文档（待实现时落地为 CTest/npm test）
2. 引擎路径与资产根：见 §12 最小 ABI 包（引擎二进制路径解析 +
   WISTERIA_ASSET_ROOT 由 SDK 显式设置为包内 assets 绝对路径）
3. daemon：后置（2026-08-12 拍板；协议已预留 hello/ping/EOF）
4. 包名：wisteria-cli（2026-08-12 拍板）
5. 错误码基准：RPC 使用 stable 固定状态码集（0–17，含确定性码），
   legacy 兼容码只作别名
6. 路径语义：v1 要求绝对路径，或显式 spawn cwd 选项；相对路径行为待定
7. 日志流：SDK 提供 engine.on('log')（stderr 逐行），不只崩溃 tail
8. 转契约后补：RPC method ↔ C 函数完整映射表、方法 schema 表、
   stable v2 新增函数清单（trace、完整 physics 配置）、验收矩阵
9. 版本策略：protocolMajor/protocolMinor 分开；
   additive 变化只升 minor，破坏性变化升 major
10. schema 体积控制：describeMethod 返回精简字段（参数名/类型/示例），
    不内嵌整份文档
11. 测试补充项：协议 fuzz（坏 JSON/超大行/未知 id/乱序）、
    崩溃半行、shutdown 竞态、确定性 golden（同一命令序列 → 同一 hash）
12. 多窗口：v1 已支持（§2.4 / §5.2.20 已写，2026-08-12 拍板）
13. 多 context：v1 已支持（§3.2 / §5.2.21 已写，2026-08-12 拍板）
14. 客户端通知：v1 客户端不发通知（§1.1 已写）
15. P0 Entity 双世界：stable/legacy 实体句柄不互用，
    方案 A/B 待拍板（见 RPC_BRIDGE_CONTRACT.md §11.5）
```

## 7. 一轮审查记录（2026-08-12）

```text
P1-1 确定性时间线方法组遗漏        → 已补 timeline.*（stable 已有映射）
P1-2 uint64 JSON 精度丢失          → 已改：uint64 一律十进制字符串
P1-3 “每个 method 都调 C ABI”不自洽 → 已改：协议类自实现 + 缺口清单
P2-1 错误码基准未指定              → 已定：stable 固定状态码集（0–17）
P2-2 底座 stable vs legacy 未定    → 已定：优先 stable，legacy 标注实验性
P2-3 引擎日志只读崩溃 tail         → 已补 on('log') 需求
P2-4 路径/cwd 语义未写             → 已补：绝对路径或显式 cwd
合理部分：独立进程/stdio/显式渲染/双形态/AI 可发现性/大数据阶梯
          ——与 ABI 链路和未来方向一致，保持不变
```

## 8. 二轮审查记录（2026-08-12，全面把关）

```text
P1-1 Windows stdio 文本模式 \r\n      → §1.7：_setmode 二进制或容忍 \r\n
P1-2 stable/legacy 覆盖矩阵          → §2.8：window/scene/light 只能 legacy，
                                        建议 a（接受现状，stable v2 单独排期）
P1-3 双平行模型面（headless vs scene）→ §2.3：v1 以 scene/entity 为主面，
                                        model.* 降为可选/别名
P1-4 显式 context 参数对 AI 不友好   → §3.2：hello 后自动默认 context
P1-5 system.ping 缺失                → §2.3：补健康检查
P2-1 崩溃半行 / id 策略 / 背压       → §1.7：逐条定死
P2-2 引擎/SDK 退出码语义             → §1.7：区分并 remap
P2-3 trace.* 范围收缩                → §2.3：明确延迟 v2
P2-4 版本策略（major/minor）         → §5.9
P2-5 readPixels 已有 C 函数          → §2.3：window.captureFrame 列为 v1 可选
P2-6 schema 体积控制                 → §5.10
P2-7 资源上限/多模型卸载提示         → §4.8
P2-8 测试补充项（fuzz/半行/竞态/golden）→ §5.11
确认保留：独立进程/stdio/显式渲染/双形态/大数据阶梯/错误线程复用
```

## 9. 三轮审查记录（2026-08-12，补丁自洽性）

```text
P1-1 hello 示例与 v2 延迟矛盾        → capabilities.trace=false；
                                        methods 列表与 §2.3 对齐
P1-2 版本字段单 int 与 §5.9 冲突    → hello/§1.2 改 protocolMajor/Minor
P1-3 日志污染 stdout 协议流          → §1.1：RPC 模式日志只走 stderr
P2-1 客户端通知方向未定义            → §1.1：v1 客户端只发请求
P2-2 多窗口语义未定                  → §2.4：v1 单窗口会话
P2-3 确定性 hash 比较责任未写       → §1.5：以引擎返回为准
P2-4 Linux SIGTERM/SIGINT 未提       → §4.3：v1 可选
P2-5 AI 任意路径信任边界未写         → §4.8：allowlist/配额后置
P3-1 hello 补 abiVersion（legacy/stable）→ 已加
确认：方向与架构对齐，无新增 P0
```

## 10. 四轮审查记录（2026-08-12，测试方案）

```text
P1-1 schema 一致性测试缺失        → §5.2.10：同一 schema 驱动校验器与注册表
P1-2 stdout/stderr 隔离未测       → §5.2.11：hello 前注入日志，首行仍合法 JSON
P1-3 用例 spawn 策略未定          → §5.2 说明：会话内多用例共用引擎，
                                     进程级用例（版本/崩溃/EOF）单独 spawn
P1-4 中文/非 ASCII 路径未测       → §5.2.12（仓库实际资产即中文名）
P1-5 NaN/Infinity 编码未定义      → §5.2.13：引擎侧显式拒绝或报错
P1-6 会话分组性能                 → §5.2 说明（同上 P1-3）
P2-1 checkpoint serialize 未定    → §2.3：v1 暴露并测（stable 已有）
P2-2 迟到响应                     → §5.2.14
P2-3 错误后会话可用               → §5.2.15（§4.6 语义）
P2-4 Poisoned 恢复                → §5.2.16
P2-5 one-shot vs rpc golden       → §5.3 形态一致性
P2-6 VMD 变体 tier 归属           → §5.3：CORE 合成 VMD / FULL_ASSETS 生产 VMD
P2-7 并发排队                     → §5.4
P2-8 启动失败/引擎路径错误        → §5.4
P2-9 fuzz 确定性                  → §5.5：固定样本 + CI 固定种子
P2-10 时间敏感用例防 flaky        → §5.5：短超时 + 可控慢路径
确认：测试分层、CTest/npm 基础设施、golden 语义与既有模式一致
```

## 11. 五轮审查记录（2026-08-12，测试可执行性）

```text
P1-1 超时/崩溃测试依赖真实引擎慢操作 → §5.4：client 注入 fake transport，
      纯单元测试；引擎侧不做不可控慢操作
P1-2 window.closed 在无头 CI 不可执行 → §5.2 环境 gate：
      真实窗口用例单独标签，无头环境跳过
P1-3 checkpoint serialize wire round trip 未覆盖 → §5.2.19
P1-4 golden“两会话”粒度未定 → §5.3：明确两个独立进程
P1-5 句柄销毁后复用未测 → §5.2.17（翻译层复测 NOT_FOUND）
P2-1 单窗口约束未测            → §5.2.20
P2-2 显式 context.create 语义未定 → §5.2.21（先定契约再测）
P2-3 顺序压力缺失              → §5.2.18（100 请求无乱序）
P2-4 fuzz 未挂 sanitizer       → §5.5：build-asan 下跑
P2-5 用例时长/无头 gate 未写   → §5.5：<60s、真实窗口标签、CI 归属
确认：schema 一致性、中文路径、非有限值、Poisoned 恢复等
      上一轮补充项全部保留
```

## 12. 最小 ABI 包（部署单元）

### 12.1 为什么先定包

引擎运行时不依赖模型文件，但**必须能读到 shader**（运行时从
`assets::Root()/shaders/*.vert|*.frag` 加载，不内嵌）。所以最小可分发包
不是“单个 exe”，而是“二进制 + shader 目录 + 版本说明”。

### 12.2 包布局（v1）

```text
wisteria-rpc-v1/
  bin/
    wisteria_rpc(.exe)          RPC 入口
    wisteria_native.dll         Windows 必需（静态链 core/platform，自包含）
    （Linux：wisteria_rpc 可全静态或附 .so，随构建矩阵定）
  assets/
    shaders/                    必须：全部 .vert/.frag
    textures/                   可选（用户模型引用时由调用方提供）
  README.md
  LICENSE
  THIRD_PARTY_NOTICES
```

### 12.3 路径解析链

```text
引擎二进制：
  WISTERIA_ENGINE_PATH（显式）
  → Node 包内 ../bin/wisteria_rpc（相对 SDK 位置）
  → PATH

资产根：
  SDK spawn 时显式设置 WISTERIA_ASSET_ROOT = <包内 assets 绝对路径>
  不依赖 spawn cwd（当前 assets::Root() 回退 <cwd>/assets，RPC 模式
  下 cwd 不可靠，必须显式传 env）
```

### 12.4 排除与边界

```text
模型资产（assets/models、assets/motions）：版权敏感，不入包；
  用户通过 load_model/load_motion 传自己的路径
测试 fixture（pmx_physics）：只进 dev/CI，不进 ABI 包
打包校验：脚本在打包后断言 shaders 非空、二进制存在、
          DLL（Windows）存在；缺失即失败
```

### 12.5 对测试的影响

```text
§5.4 集成测试使用“包布局”而不是源码树：spawn 包内 bin，
设置包内 assets；同时验证路径解析链三种来源
```

## 附录 A：与现有架构的关系

```text
状态世界（Scene/ModelInstance/SabaMmdRuntimeModel）   ← RPC 控制的目标
渲染世界（Renderer/RenderGraph/RenderDevice）         ← 引擎自己执行，RPC 不碰
C 门户（wisteria_native v0.7 + stable v1）            ← 桥接的调用底座
wisteria_rpc                                          ← 新薄层：C ABI ↔ JSON
```

桥接只站在 C 门户的稳定承诺之上，不引入新句柄体系、不重新设计错误/
线程模型。RPC 层是“翻译器”而不是第二套引擎。

## 附录 B：大数据拓展阶梯（后续，不在 v1）

```text
1. 偶尔截图：readPixels → PNG（miniz 已有）→ base64 走同一条 stdio
2. 实时帧流：named pipe（Windows）/ Unix socket（Linux）+ frame_ready 通知
3. 共享内存：CreateFileMapping + Node 侧最小 helper（零拷贝，最后考虑）
4. 文件交换：引擎写临时目录，Node 按通知读（适合离线序列）
```

原则：控制协议永远不变，数据协议按需生长；`capabilities.dataChannel`
由引擎在 hello 中声明，客户端按能力自适应。
