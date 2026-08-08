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
