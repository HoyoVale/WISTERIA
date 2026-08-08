# R1.6 Phase 0E — Deterministic Frame Sequence 实现基线（2026-08-09）

> 状态：**COMPLETED（四矩阵全绿）**。
> 契约：`docs/architecture/R1_6_PHASE0E_CONTRACT.md`
> （CONTRACT FROZEN）。

## 1. 一句话

给已闭合的 `Runtime → Render State → Presentation → RenderOffline →
RGBA8` 链加上确定性批量时间序列 orchestration：from-start 连续输出、
JSONL manifest 事务、checkpoint 持久化 + A/B 双 slot、resume 等价、
三种 overwrite 策略；Renderer 未改。

## 2. 代码改动

### ModelInstance::PublishCurrentRuntimeFrame()

```text
exact step / restore 后只做 publication：
  ProduceRenderFrameView → generic validation →
  LastRenderFrameView / LastFrameView / metadata 同步
不调用 runtime.Update()、不消费 root motion、不推进时间
```

### PNG encoder（utility，无 Renderer 依赖）

```text
include/wisteria/common/png_encoder.hpp
src/common/png_encoder.cpp
EncodePngRgba8(width, height, rgba) → PNG bytes
  top-left RGBA8、filter=0、miniz deflate（mz_compress2 level 6）
miniz.cpp 放开 deflate/zlib API（原只编译 inflate）
```

### OfflineFrameSequence（orchestration）

```text
include/wisteria/scene/offline_frame_sequence.hpp
src/scene/offline_frame_sequence.cpp

配置：
  outputDirectory / renderRequest（显式 Camera+Projection）
  overwritePolicy（Reject / Overwrite / VerifySkip）
  writePng / writeRaw（至少一个 persistent 格式开启）

主流程：
  RenderRange(start, end)：
    SetMotionLooping(false)（确定性前提）
    → PrepareFrameZero + 顺序 pre-roll 1..start
    → 每帧：PublishCurrentRuntimeFrame → presentation N →
      RenderOffline → rgbaHash → PNG/raw（temp+rename）→
      CreateCheckpoint → SerializeCheckpoint → A/B slot 持久化 →
      JSONL manifest append → commit
  Resume(end)：
    manifest 最后完整记录 → session/build 校验 →
    checkpoint wire 读取 + hash 校验 → Deserialize →
    RestoreCheckpoint → 校验 frame == committed →
    Step N+1..end → 同上 commit

失败语义：fail-stop（Failed() 置位并抛异常，禁止继续）
manifest = JSONL append-only（session 记录 + 每帧一条）
文件名：uint64 MotionFrameIndex、最小 8 位补零
hash：FNV-1a 64（rgbaHash/pngFileHash/rawFileHash/checkpointWireHash）
```

## 3. 测试

```text
runtime_tests：
  PNG encoder round trip（stb_image 解码回读逐字节一致）

integration_tests：
  publish current runtime frame：
    PrepareFrameZero → publish（幂等）；
    StepMotionFrameExact(1) → publish 反映新帧（revision 变化）

render_fbo_tests（GL）：
  deterministic frame sequence：
    from-start 0..2 → PNG/manifest/checkpoint-A/B 落盘
    from-start 0..3 参考目录 → 0..2 两目录逐字节一致
    Resume(3) → frame 3 PNG == from-start frame 3（同环境）
    Reject 遇已有 artifact → fail-stop
    Overwrite → 成功重写
    VerifySkip → 重渲染 + rgbaHash 比对后跳过（文件不变）
```

## 4. 四套矩阵（2026-08-09 实测）

```text
Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）。

## 5. 边界（Phase 0E 不做）

```text
不改 Renderer / 不建 OfflineRenderer
不冻结 Stable Render C API
不做 Headless context provider（R1.7）
不修 GraphicsDevice share-group identity（R1.7）
不做视频编码 / FFmpeg / audio
不支持多 deterministic driver 同步（v1）
raw 落盘 optional（默认关闭）
```

## 6. 下一步

R1.6 整体 Closure：0A–0E 全部完成后写 Final Closure，随后转入
横向方向（Stable C Portal / MMD Advanced / 更多 Backend）。

## 7. Final Guard（2026-08-09 第二轮审查闭合）

```text
A. Presentation：同帧 camera sample 决定是否重建 projection
   （perspective true/nullopt → Camera FOV + width/height 重建；
    perspective false → 保留 fallback）
B. Durable transaction：temp 写入 → _commit/fsync → 原子 replace
   （Windows MoveFileEx REPLACE+WRITE_THROUGH / POSIX rename + dir fsync）
C. JSONL crash-tail：读取前截掉最后一个 '\n' 之后的所有 bytes；
   完整行 parse 失败 = fail-stop
D. committed-record authority：先查 record → 校验 artifacts 存在 + file
   hash 匹配 → 再应用 policy；VerifySkip 仅 rgbaHash 相同 + artifacts
   完整才 skip；orphan（无 record 有文件）Reject/VerifySkip=error、
   Overwrite=恢复
E. A/B：next slot = opposite(last committed slot)；新 session 选 A；
   禁止 frame parity 推导；Overwrite 旧帧不刷新 checkpoint
   （否则会破坏更新的 committed 帧依赖的槽——本轮实测发现的 bug）
F. Session identity：hash camera/projection/clearColor/width/height +
   scenePresentationIdentity；constructor 校验
   modelInstance.TryGetMmdRuntime() == &runtime
G. fail-stop 全覆盖：RenderRange/Resume 最外层 catch(...) → failed=true
H. Resume：即使无新帧也先读/校验/restore checkpoint；
   frame domain 冻结 frame <= 2^24
I. PublishCurrentRuntimeFrame 不再递增 updateSerial
```

测试新增：

```text
RenderRange(2,3) pre-roll == from-start 参考帧
crash tail JSONL → Resume 截断并继续
committed frame4(A) → 非顺序 RenderRange(6) 必须落 B（A 不被破坏）
VerifySkip：篡改 PNG / 删除 PNG 必须拒绝；恢复后通过
orphan artifact：Reject/VerifySkip 拒绝、Overwrite 恢复
FOV 45 vs 60 → 离屏像素不同（projection 随 Camera 重建）
publish updateSerial 不随重复 publish / exact-step publish 变化
```

测试中发现 pmx-physics 在无 IBL 场景渲染全黑，序列场景增加静态
Box.glb 使像素断言有效。

## 8. Final Closure Guard（2026-08-09 二轮闭合）

```text
1. ApplyPresentation：只有 cameraApplied && cameraSample 存在 &&
   perspective true/nullopt 才重建 projection；无 track 时 custom
   projection 原样保留（回归：FOV45/60 + 同一 custom projection →
   输出逐字节一致）
2. TruncateJsonlTail 改为 in-place truncate（_chsize_s/ftruncate +
   durable sync），不再 rewrite 整个文件
3. FlushDurably/AtomicReplace 检查 fflush/_commit/fsync/目录 fsync
   返回值，失败 → fail-stop
4. historical Overwrite/VerifySkip 不再修改 lastCommitted/slot；
   RenderRange 入口同时初始化 frame + slot；
   回归：frame6(B) 后 Overwrite(4) → cursor 仍 6；RenderRange(7)
   → slot A，B 在新 commit 前不被破坏
5. 读取已有 manifest 后 sessionRecordWritten=true；
   回归：复制目录后 Resume 成功且 session record 数量 == 1
6. SessionIdentity 移除 outputDirectory；
   回归：复制输出目录后 Resume 通过（路径无关）
7. committed Overwrite rgbaHash 不同 → fail-stop；
   回归：移动场景物体后 Overwrite(4) 拒绝
8. camera-track FOV 生效回归：加载 perspective 相机 VMD（FOV 30）
   后输出 != 无 track 基线
```

## 9. Micro Closure Guard（2026-08-09 三轮闭合）

```text
1. POSIX AtomicReplace：open(parent dir) 失败 → false；
   AppendDurable 首次创建 manifest 后 fsync parent directory
2. append-only forward commit log：
   frame <= lastCommitted && 无 committed record → fail-stop
   （回归：tail=7 时 RenderRange(1,1) 被拒，manifest tail 与
     checkpoint-A 均不变）
3. latest committed Overwrite：
   checkpointWireHash != committed hash → fail-stop
   （回归：Overwrite(7) 成功且 checkpoint-A 字节不变）
4. 空 manifest = fresh session：
   （回归：只写 partial session 行 → RenderRange(0,0) 恢复为
     全新目录，恰好一个 session record + frame0 commit 成功）
```
