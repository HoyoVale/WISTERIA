# R1.8 — Generic Deterministic Runtime Final Closure（2026-08-09）

> 状态：**FROZEN / IMPLEMENTED / VALIDATED / CLOSED**。
> 契约：`docs/architecture/R1_8_GENERIC_DETERMINISTIC_RUNTIME_CONTRACT.md`。
> 四矩阵（2026-08-09 实测）：
> Windows CORE 8/8、Windows FULL 9/9、Linux CORE 9/9、Linux FULL 10/10。

## 1. 一句话

WISTERIA 的确定性能力正式从 Saba MMD 特有能力升格为 Runtime 标准能力：
`WisteriaGenericRuntimeDriver` 具备 exact step / snapshot-restore /
checkpoint（payload kind 2）/ replay，`OfflineFrameSequence` 后端无关，
Generic 可以在零窗口 headless session 上跑 `RenderRange + Resume`，
并与 from-start 完全等价。

## 2. Phase 汇总

```text
0A  契约 + 五项决策 + deterministic subset   FROZEN ✅
0B  Generic 30Hz canonical timeline          CLOSED ✅
     PrepareFrameZero / StepMotionFrameExact /
     loop wrap / non-loop clamp / root delta
0C  Generic checkpoint kind 2                CLOSED ✅
     R1.4 envelope 复用 / restore / replay /
     语义校验 / 强化 asset fingerprint
0D  OfflineFrameSequence 后端无关化          CLOSED ✅
     IModelRuntimeDriver + capability 门控 /
     checkpoint 按 payload kind 分派 /
     零窗口 Generic RenderRange + Resume
0E  四矩阵 + Final Closure                   CLOSED ✅
```

## 3. 最终架构

```text
IModelRuntimeDriver
  ├─ SabaMmdRuntimeModel    exact step ✅ checkpoint kind 1 ✅
  └─ WisteriaGenericRuntime exact step ✅ checkpoint kind 2 ✅
        ↓
DeterministicBackendCapabilities（authoritative）
        ↓
OfflineFrameSequence(IModelRuntimeDriver&, ...)
  ├─ capability 门控（exact + checkpoint capture/restore/replay）
  ├─ IDeterministicFrameStepper（能力/接口不一致 = contract violation）
  ├─ checkpoint 分派：MmdRuntimeModel(kind1) /
  │                    IDeterministicCheckpoint(kind2)
  └─ 零窗口 HeadlessRenderSession 上 RenderRange / Resume
```

冻结的关键语义：

```text
1. MotionFrameIndex = engine-owned 30Hz canonical 坐标；
   AnimationClip 保持连续时间域，在 N/30 采样
2. StepMotionFrameExact(N) = 绝对边界求值，禁止从 0 重放；
   frozen config（含 loopMotion）漂移 → DeterminismViolation
3. 每个 exact boundary 恰好一个 deterministic root delta →
   pending state → 编排层消费至多一次
4. Generic Deterministic Mode v1 subset：
   crossfade / state machine / trigger / speed≠1 / paused /
   IK override → UnsupportedDeterministicState（绝不部分 checkpoint）
5. checkpoint 语义校验 in-memory == wire：
   finite / playing / clipClamped / 单位四元数 / fingerprint
6. assetFingerprint = ModelAsset::DeterministicFingerprint()
   （backend + parts + mesh topology + mesh morph offsets +
    skeleton + morph definitions + clips/keys）
7. OfflineFrameSequence v1 对 Generic root motion enabled 显式拒绝
   （Entity/world-transform checkpoint 留待后续）
```

## 4. 四套矩阵（2026-08-09 实测）

```text
Windows CORE（MSVC RelWithDebInfo）            8/8  Passed
Windows FULL（MSVC + 完整资产）                9/9  Passed
Linux CORE（GCC / WSL，llvmpipe）              9/9  Passed
Linux FULL（GCC / WSL + 完整资产）            10/10 Passed
```

Linux 使用 `LIBGL_ALWAYS_SOFTWARE=1`（WSL 兼容性口径）。
headless-smoke 在 Linux 矩阵中验证：零窗口 Generic 序列
`RenderRange(0..2) → fresh Resume(4) → from-start(0..4)` 等价、
manifest 记录 `wisteria-generic`。

## 5. 冻结边界（R1.8 不做）

```text
Saba 迁移到 IDeterministicCheckpoint（保留 MmdRuntimeModel kind 1）
Entity/world-transform checkpoint（sequence root motion v1 拒绝）
VRM / 新后端
Stable Runtime/Render C ABI（R1.9）
RenderDevice / RenderGraph / Vulkan（R2.x）
```

## 6. 与 R1.7 的关系

R1.7 native-Linux 硬件 release gate 仍是共享待办（真实 Linux 机器执行
`script/verify_r17_native_linux.sh`）；R1.8 为 CPU/runtime 层工作，
不阻塞其 0E 结果，但 R1 系列整体签收前需补上。

## 7. 后续方向

```text
R1.9  Stable Runtime / Render C ABI
R2.x  RenderDevice / RenderTarget / RenderGraph / 多后端
```

## 8. 冻结声明

R1.8 Phase 0A–0E 至此停止开发；后续工作必须消费 frozen 的
deterministic capability、GenericR18 payload、fingerprint 与
sequence root-motion 边界，不重新打开。

