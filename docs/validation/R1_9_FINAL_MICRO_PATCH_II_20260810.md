# R1.9 Final Micro Patch II 基线（2026-08-10）

> 状态：**COMPLETED**；2026-08-10 ChatGPT 复审通过，R1.9 Final Closure ✅。
> 依据：ChatGPT 对 `b865be9 — R1.9 0A–0D fixup` 的代码级复审，
> 0B/0D APPROVED WITH FINAL MICRO PATCH、0E HOLD。

## 1. 修复清单

```text
P0-1  borrow transaction 异常安全（EntityBorrowGuard 先于
       BindModelInstanceParts，三个 Stable Render 路径统一）
P0-2  owner commit 时机（所有 CPU-only setup 成功后、第一次真实
       GPU operation 前；MakeCurrent 失败不再留下永久绑定）
P0-3  status 语义拆分：
       NOT_FOUND        = handle 不存在
       UNSUPPORTED      = entity 存在但无对应 backend/runtime
       UNSUPPORTED_REPLAY_PROFILE = runtime 无 deterministic profile
       INVALID_CHECKPOINT = checkpoint 与 backend 不兼容
4.    negative-status 集成测试（TestR19StableStatusSemantics）
```

## 2. 代码改动

```text
src/native/wisteria_stable_render.cpp
  三个路径（session_render / sequence_range / sequence_resume）：
  SetModelInstance → EntityBorrowGuard → BindModelInstanceParts；
  ownerRenderSession 移到 RenderOffline / MakeCurrent+RenderRange 前；
  sequence 无 runtime → UNSUPPORTED（原 INVALID_STATE）

src/native/wisteria_stable_runtime.cpp
  RequireStableRuntime / RequireStableMmd 增加 out_status 输出：
    unknown handle → NOT_FOUND
    存在 entity 但无对应 runtime/MMD → UNSUPPORTED
  10 个调用点（morph / load_motion / unload_motion / prepare /
  step / replay / preview / checkpoint_create）统一返回 lookupStatus

tests/integration_tests.cpp
  +TestR19StableStatusSemantics：
    Generic + load_motion                    → UNSUPPORTED
    Static + morph override                  → UNSUPPORTED
    Static + prepare_frame_zero              → UNSUPPORTED
    Static + step_exact / replay_exact       → UNSUPPORTED
    Static + checkpoint_create               → UNSUPPORTED
    Static + sequence_range                  → UNSUPPORTED
    garbage entity + step/checkpoint_create  → NOT_FOUND
```

## 3. 验证结果（2026-08-10）

```text
Windows CORE：9/9 PASS
Windows FULL：10/10 PASS（生产资产）
Linux CORE（WSL，llvmpipe）：11/11 PASS
Linux FULL（WSL，llvmpipe）：12/12 PASS
ABI safety matrix：94 legacy + 30 stable（导出面不变）
```

## 4. 当前状态

```text
R1.9 0A  CLOSED ✅
R1.9 0B  CLOSED ✅（Final Micro Patch II 已复审通过）
R1.9 0C  CLOSED ✅
R1.9 0D  CLOSED ✅（Final Micro Patch II 已复审通过）
R1.9 0E  CLOSED ✅（R1.9 Final Closure — 2026-08-10）
```

## 5. 复审注意事项

1. borrow 顺序：guard 必须先于 `BindModelInstanceParts()`，保证
   bad_alloc 等异常路径由 RAII 归还 `entry->modelInstance`。
2. owner commit：单帧在 `PublishCurrentRuntimeFrame()` 之后、
   `RenderOffline()` 之前；sequence 在 `MakeCurrent()` 成功之后、
   `RenderRange/Resume()` 之前。
3. status：`NOT_FOUND` 仅用于 handle 不存在；`UNSUPPORTED` 用于
   entity 存在但能力不属于该 backend；`UNSUPPORTED_REPLAY_PROFILE`
   用于 runtime 存在但无 deterministic surface；`INVALID_CHECKPOINT`
   用于 checkpoint 与 backend 不兼容（restore null runtime 保持）。
