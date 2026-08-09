# R1.8 Phase 0D — OfflineFrameSequence 后端无关化 实现基线（2026-08-09）

> 状态：**COMPLETED**。
> 契约：`docs/architecture/R1_8_GENERIC_DETERMINISTIC_RUNTIME_CONTRACT.md`。

## 1. 一句话

`OfflineFrameSequence` 不再依赖 `MmdRuntimeModel`：
它接受 `IModelRuntimeDriver&`，按 capability + interface 门控，
checkpoint 按 payload kind 分派（MMD kind 1 / Generic kind 2）。
Generic 确定性运行时现在可以在零窗口 `HeadlessRenderSession` 上跑
`RenderRange(0..2)` 并产出 PNG + manifest + A/B checkpoint。

## 2. 代码改动

```text
include/wisteria/scene/offline_frame_sequence.hpp + .cpp
  构造签名：MmdRuntimeModel& → IModelRuntimeDriver&
  校验：modelInstance.TryGetRuntime() == &runtime
  capability 门控：supportsExactFrameStepping == false → invalid_argument；
    stepper cast 失败 → logic_error（contract violation）
  checkpoint 表面：mmdRuntime（FrameCheckpoint）或
    genericCheckpoint（GenericRuntimeCheckpoint）至少一个，否则拒绝
  CheckpointData = variant<FrameCheckpoint, GenericRuntimeCheckpoint>
  Capture/Restore/Serialize/DeserializeCheckpoint 按持有接口分派
  ApplyPresentation：MMD camera/light 只对 mmdRuntime 生效；
    Generic 保留显式 host presentation
  SetMotionLooping(false)：只对 mmdRuntime 调用；
    Generic 的 loop 语义来自 ReplayConfig（默认 loopMotion=false）

tests/headless_smoke.cpp
  sequence probe 改用 animated_triangle.gltf（Generic 确定性运行时）
  + Box.glb，验证零窗口 RenderRange(0..2) 全落盘

tests/runtime_tests.cpp
  +R1.8 sequence backend-neutral gate：
    timeline Generic 被接受；plain Generic 被 invalid_argument 拒绝
```

## 3. 验证结果（2026-08-09）

### 3.1 Windows（MSVC RelWithDebInfo）

```text
runtime：8/8 R1.8 用例 PASS（新增 backend-neutral gate）
render-fbo PASS（R1.6 MMD 确定性序列全回归）
integration PASS
```

### 3.2 WSL Ubuntu 22.04（GCC，LIBGL_ALWAYS_SOFTWARE=1）

```text
headless-smoke：
  session probe PASS（Box.glb + light 全链路）
  sequence probe PASS（Generic 运行时，frames 0..2，
    manifest + A/B checkpoints）
runtime：8/8 R1.8 PASS
unit / integration：全 PASS
```

## 4. 语义验收点

```text
1. 同一个 OfflineFrameSequence 类同时承载 Saba（kind 1）与
   Generic（kind 2），没有 backend-specific 并行序列类
2. capability 门控：不支持 exact stepping 的后端构造即失败；
   capability true + 接口缺失 = contract violation（logic_error）
3. checkpoint 分派：恢复路径用与写入路径相同的 codec，
   混用目录/后端时 envelope kind 不匹配 → InvalidCheckpoint
4. Generic 序列零窗口运行：RenderRange(0..2) 产出
   PNG / manifest.jsonl / checkpoint-A/B.bin
5. MMD camera/light presentation 语义未变（render-fbo 回归）
6. 跨进程 checkpoint（R1.2C/R1.4）未受影响
```

## 5. Phase 0D 边界确认

```text
未做：四矩阵（0E）、Saba 迁移到 IDeterministicCheckpoint（保留
      MmdRuntimeModel FrameCheckpoint 路径，0D 已按 capability 分派）
```

## 6. 下一步

Phase 0E：四矩阵（Windows CORE/FULL、Linux CORE/FULL + native gate）
+ Final Closure。

