# R1.9 Phase 0C — Deterministic Stepping + Checkpoint C ABI
# 实现基线（2026-08-09）

> 状态：**COMPLETED**。
> 契约：`docs/architecture/R1_9_STABLE_RUNTIME_RENDER_ABI_CONTRACT.md`。

## 1. 一句话

Generic payload kind 2 的 checkpoint 现在通过 stable C ABI 完成
**跨进程等价验证**：同一 glTF/Generic 实体在两个独立进程中，
checkpoint N 与继续步进的 N+1 wire 字节完全一致；Saba kind 1 与
production-full 路径回归无损。

## 2. 代码改动

```text
CMakeLists.txt
  +wisteria.stable-checkpoint-cross-process-generic
    （stable_checkpoint_cross_process_test.py + stable_checkpoint_cli +
      animated-triangle-gltf，无 VMD，--frame 30 --require-n1）

复用（无改动）：
  stable_checkpoint_cli 的 dump/load 已支持无 VMD 的 Generic 实体
  （0B 的 backend-neutral entity + checkpoint variant 分派）
```

## 3. 验证结果（2026-08-09）

```text
Windows CORE：stable-checkpoint 2/2（pmx-physics + generic）PASS
Windows FULL：stable-checkpoint 3/3（+ production-full）PASS
Linux CORE（WSL）：stable-checkpoint 2/2 PASS
```

## 4. 语义验收点

```text
1. Generic N wire 字节跨进程一致（dump A == load B）
2. Generic N+1 wire 字节跨进程一致（--require-n1）
3. Saba kind 1 跨进程回归通过（字节兼容未破坏）
4. production-full（叶瞬光 + VMD）跨进程回归通过
5. CLI 只走 public stable surface，无内部类型泄漏
```

## 5. Phase 0C 边界确认

```text
未做：Render/offline C 面（0D）、0E ABI 矩阵
```

## 6. 下一步

Phase 0D：`wisteria_stable_render.h`（RenderSession + 单帧
RenderOffline + OfflineFrameSequence C 面）。

