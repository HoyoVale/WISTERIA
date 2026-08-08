# R1.6 Phase 0E — Deterministic Frame Sequence 契约

> 状态：**CONTRACT FROZEN（2026-08-09，契约级审查闭合）**。
> 基线：R1.6 Phase 0A–0D CLOSED（`99577e6`，含 0C UV 约定修复）。
> 一句话：给已经闭合的
> `Runtime → Render State → Presentation → RenderOffline → RGBA8` 链
> 加上确定性的批量时间序列 orchestration；不再改 Renderer。

## 1. 冻结的主流程（FROM START）

```text
PrepareFrameZero
↓
PublishCurrentRuntimeFrame
↓
若 startFrame == 0：output frame 0
否则：StepExact(1..startFrame)（必须顺序真实走过，中间可不渲染）
↓
PublishCurrentRuntimeFrame(startFrame)
↓
ApplyPresentation(startFrame)      ← sample frame N，不用 realtime clock
↓
RenderOffline
↓
RGBA8 + rgbaHash
↓
Encode PNG / optional RAW
↓
SerializeCheckpoint(startFrame)
↓
persist output + checkpoint（A/B 双 slot，旧 checkpoint 在新 commit 前不可破坏）
↓
append manifest commit record
↓
frame committed（lastCommittedFrame 前移）
↓
StepExact(startFrame + 1) → … → M
```

## 2. 首要 correctness：Exact step 后必须重新发布 ModelInstance

Renderer 不直接读取 Runtime，它读取
`ModelInstance::LastRenderFrameView()`；该缓存目前只在
`ModelInstance::Update(deltaTime)` 内刷新。直接
`StepMotionFrameExact(N) → RenderOffline` 会得到
`Runtime = N、LastRenderFrameView = N-1`。

**禁止用 `Update(0)` 补救**（会再次执行 runtime evaluation，破坏
exact boundary）。

冻结新增入口：

```cpp
void ModelInstance::PublishCurrentRuntimeFrame();
```

语义：

```text
runtime state 已由 exact step / restore 改变
↓
ProduceRenderFrameView()
↓
generic validation
↓
LastRenderFrameView / LastFrameView / metadata 同步
↓
不调用 runtime.Update()
不消费 root motion
不推进时间
```

主流程固定为：

```text
Prepare/Step exact frame N
→ PublishCurrentRuntimeFrame
→ presentation N
→ RenderOffline
```

## 3. startFrame > 0 的顺序 pre-roll

`IDeterministicFrameStepper` 状态机冻结：

```text
PrepareFrameZero → Step 1 → Step 2 → …（不可跳步，否则 NonSequentialFrame）
```

from-start `N..M`：

```text
PrepareFrameZero
Publish frame 0
若 startFrame > 0：
  Step 1..startFrame 顺序真实执行（0..N-1 可不渲染）
到达 N
→ PublishCurrentRuntimeFrame
→ presentation N
→ output N
之后 N+1 → … → M
```

## 4. Resume：checkpoint wire bytes 必须持久化

当前只有 checkpoint hash 无法 restore。冻结：

```text
FrameCheckpoint
→ SerializeCheckpoint()
→ checkpoint wire bytes（自带 build compatibility identity）
→ 持久化到磁盘
→ wire hash
→ manifest commit record

Resume：
  read persisted checkpoint bytes
  → verify wire hash
  → DeserializeCheckpoint
  → RestoreCheckpoint
  → verify frame == committedFrame
  → Step N+1
```

**旧 checkpoint 在新 manifest commit 前不可破坏**（双 slot 不变式）：

```text
checkpoint-A.bin / checkpoint-B.bin
manifest 当前指向 A（frame N committed）
做 N+1：写 B → 写 frame N+1 → commit manifest 指向 B
        → 此后 A 才可复用
中途崩溃 → manifest 仍指 A，A 完整 → 上一 committed state 可恢复
```

## 5. Manifest 是唯一 committed-state authority；允许 orphan

```text
Manifest commit record 是唯一的 committed-state authority。

artifact 存在 + 无 committed manifest record
  = orphan / uncommitted artifact
  ≠ committed frame

合法的 crash 点：frame.tmp → rename frame.png ✅ → CRASH →
manifest 尚未 commit ❌。因此磁盘必然可能出现
“frame file exists、manifest record absent”。
```

失败语义 = **fail-stop**：

```text
任何 frame transaction 失败
→ Session enters Failed
→ 不允许继续 N+1
→ 当前进程可销毁/reopen
→ resume 从 last committed frame 恢复
```

不再写 “manifest 标记 incomplete”（manifest 未 commit 时无处可靠标记）。

## 6. Overwrite policy 作用域

```text
Reject / Overwrite / VerifySkip

作用对象：某个 frameIndex 的 output artifact / committed frame record
不作用于：manifest container 本身
（manifest/commit log 是 sequence metadata，每帧必须更新）
```

`VerifySkip`（v1 冻结）：

```text
先 exact-evaluate frame N
→ PublishCurrentRuntimeFrame
→ presentation N
→ RenderOffline
→ canonical RGBA8 hash

已有 committed record 的 rgbaHash 相同
→ skip encode/write，sequence 继续
不同 → error

即：省磁盘写入，不省 deterministic evaluation/render。
```

orphan file（无 committed record）：

```text
Reject     → error
VerifySkip → error（无可信 reference）
Overwrite  → 可恢复并重写
```

## 7. v1 runtime scope：单一 deterministic MMD driver

```text
一个 OfflineFrameSequence = 一个 deterministic sequence driver

v1：
  Saba MMD runtime
  + IDeterministicFrameStepper
  + R1.2C checkpoint support

Scene 可包含：static model / ground / environment / lights

v1 不支持：
  两个独立 deterministic MMD runtime 同步推进
  Generic runtime exact sequence

以后其他 Runtime 实现相同 deterministic capabilities 即可接入，
不需要改 Renderer。
```

## 8. Raw 落盘 optional

```text
Canonical exchange format：Rgba8Frame ✅ 必须
Persisted .rgba：optional
PNG：v1 standard output

canonical determinism evidence = canonical RGBA8 bytes/hash（编码前完成）
≠ 必须把 raw bytes 长期写盘

配置：
  writePng = true
  writeRaw = false（默认）
  至少一个 persistent output format 必须开启

测试的 resume equality 直接比较 Rgba8Frame，不依赖 .rgba 文件。
```

## 9. Manifest = JSONL append-only commit log

```text
manifest.jsonl

第一条：{"type":"session", sessionIdentity, buildIdentity, ...}
之后：{"type":"frame","frameIndex":0,...} 每帧一条

每帧 commit：
  frame/checkpoint artifacts 落盘
  → append 恰好一条 JSON record
  → flush/fsync

crash 半行：resume 只接受最后一个完整、合法、
newline-terminated JSON record；尾部 partial record 截掉。

O(n) 总增长，不做全量 rewrite。
```

## 10. Hash 契约

```text
算法：FNV-1a 64（复用现有 determinism/checkpoint fingerprint）
用途：deterministic/integrity fingerprint，非安全/非加密

字段区分：
  rgbaHash           权威（VerifySkip 比较项）
  pngFileHash        有 PNG 时
  rawFileHash        有 raw 时
  checkpointWireHash
  sessionIdentity
```

## 11. Session ownership：Renderer 借用

```text
OfflineFrameSequence 借用：Scene&、Renderer&、MmdRuntimeModel&（driver）、
  ModelInstance&（publication）
Session 持有：输出目录、manifest/commit log、sequence cursor、
  checkpoint 持久化（A/B）、presentation 状态、失败状态
Session 不拥有：ModelAsset、GL context、Renderer
  （Renderer 析构需要 current GL context，Session 拥有会混入 GL lifecycle）
```

## 12. PNG encoder utility（不冻结第三方实现）

```text
冻结接口：
  Rgba8Frame → PNG bytes/file
  top-left RGBA8
  无 Renderer 依赖
实现阶段用现有可用 utility 或引入 stb_image_write 均可。
```

## 13. 文件名（uint64 MotionFrameIndex）

```text
zero-padded decimal MotionFrameIndex
minimum width = 8
never truncate

00000000.png / 00000001.png / … / 16777216.png
```

## 14. Resume 像素等价的范围限定

```text
RGBA8 resume == from-start

只要求同 session compatibility / 同 build /
同 renderer + GL environment 下 byte-identical；
绝不升级为 Windows ↔ Linux / 不同 GPU 的 pixel-exact 保证
（与 R1.6 0A：Exact State ≠ Cross-platform Pixel Exact 一致）。
```

## 15. 验收（0E 完成标准）

```text
1. from-start N..M 连续输出：PNG(+optional raw) + JSONL manifest 全绿
2. frame identity：文件序号 == MotionFrameIndex（uint64、最小 8 位）
3. presentation exactness：presentation sample 与 runtime frame 同帧
4. resume 等价：restore N → N+1 Rgba8Frame == from-start N+1
   （同 build / 同 render environment）
5. transaction：中途失败 → fail-stop；reopen 后从 last committed 恢复
6. checkpoint A/B：旧 checkpoint 在新 manifest commit 前不可破坏
7. overwrite 三策略各一条测试（含 orphan 行为）
8. 四套矩阵全绿
```

## 16. 明确不做（0E 边界）

```text
不改 Renderer / 不建 OfflineRenderer
不冻结 Stable Render C API
不做 Headless context provider（R1.7）
不修 GraphicsDevice share-group identity（R1.7）
不做视频编码 / FFmpeg / audio
不扩 ModelFrameSnapshot
不支持多 deterministic driver 同步（v1）
```

## 17. Final Contract Addendum（2026-08-09，Final Guard 闭合）

```text
A. Presentation projection refresh：
   同帧 CameraTrackSample perspective == true/nullopt 且应用成功后，
   request.projection 必须由更新后的 Camera FOV + width/height 重建；
   perspective == false 保留 fallback projection。

B. Durable transaction：
   temp 写入 → OS durable flush（Windows _commit / POSIX fsync）→
   atomic replace（Windows MoveFileEx REPLACE_EXISTING+WRITE_THROUGH /
   POSIX rename）；JSONL append 同样 durable flush。
   禁止 remove-then-rename 的非原子窗口。

C. JSONL crash-tail 恢复：
   读取前 binary 扫描最后一个完整 '\n'，截掉其后所有 bytes；
   完整行 parse 失败 = 错误（fail-stop）；
   仅尾部非 newline 片段可作为 crash residue 删除。

D. committed-record authority：
   policy 判断先查 committed record，再校验其声明的 artifacts
   存在且 file hash 匹配；缺失/篡改 = 错误。
   VerifySkip：rgbaHash 相同 + artifacts 完整才 skip；
   缺失/篡改即使 rgbaHash 相同也拒绝。
   orphan（artifact 存在但无 committed record）：
     Reject/VerifySkip = error；Overwrite = 可恢复并重写。

E. Checkpoint A/B：
   next slot = opposite(last committed checkpointSlot)；
   新 session 无 committed record 时选 A；禁止 frame parity 推导。

F. Session identity：
   hash 已知 presentation 输入（camera param / projection / clearColor /
   width / height）+ host 提供的 scenePresentationIdentity；
   constructor 必须验证 modelInstance.TryGetMmdRuntime() == &runtime。

G. Fail-stop 全覆盖：
   RenderRange / Resume 最外层 catch(...) → failed=true → rethrow。

H. Resume 边界：
   即使 lastRecord.frame >= end，也必须先读、校验、restore checkpoint；
   frame domain 冻结：frame <= 2^24（float(frame) 逐整数精确）。

I. Publication：
   PublishCurrentRuntimeFrame 不增加 updateSerial；
   updateSerial 只由 runtime Update publication 推进。
```
