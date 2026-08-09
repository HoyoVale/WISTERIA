# R1.8 Phase 0C — Generic Snapshot/Restore + Checkpoint Payload Kind 2
# 实现基线（2026-08-09）

> 状态：**COMPLETED**。
> 契约：`docs/architecture/R1_8_GENERIC_DETERMINISTIC_RUNTIME_CONTRACT.md`。

## 1. 一句话

Generic Runtime 现在可以捕获/恢复/重放完整确定性状态，并通过
**payload kind 2** 序列化到 R1.4 envelope；Saba R12C（kind 1）字节
兼容不变。`restore + continue` 与 `from-start` 在 pose / time /
morph / root-motion delta 上完全等价。

## 2. 代码改动

```text
include/wisteria/runtime/generic_checkpoint.hpp（新增）
  GenericRuntimeCheckpoint（timeline / morph overrides / root motion
    配置 + pending delta / clip identity / fingerprint）
  IDeterministicCheckpoint（Create/Restore/ReplayFromCheckpoint）

include/wisteria/runtime/checkpoint_serialization.hpp + .cpp
  EnvelopeWriter / EnvelopeHeader / ReadEnvelopeHeader 抽取
    （R1.4 envelope 单一实现，R12C 字节布局不变）
  CheckpointPayloadKindGenericR18 = 2 / Schema 1 /
    BackendIdWisteriaGeneric = 2 / Profile GenericV1 = 2
  Encode/DecodeGenericPayload + Serialize/DeserializeGenericCheckpoint
    严格校验：finite、morph 排序、count/string 上限、剩余字节

include/wisteria/runtime/runtime_model_base.hpp
  IModelRuntimeDriver 新增 SetMorphOverride / ClearMorphOverride /
  ClearAllMorphOverrides（默认 false / no-op）

include/wisteria/runtime/wisteria_generic_runtime_driver.hpp + .cpp
  继承 IDeterministicCheckpoint
  CreateCheckpoint：prepared + subset gate → 完整 payload
  RestoreCheckpoint：clip/帧域/fingerprint/root/morph/canonicalTime
    语义校验 → Reset+Play+SetLooping+root config → subset gate →
    EvaluateCanonicalFrame(t,t) → pending delta → overrides 重放
  ReplayFromCheckpoint：target > frame 校验 → restore → 顺序步进
  morphOverrides 持久化 + exact step/Update 后重放
  Capabilities：deterministic checkpoint 三比特 + 镜像打开

tests/runtime_tests.cpp
  fixture 增加 blink vertex morph
  0B capability 断言更新为 checkpoint 打开
  +2 个 0C 用例
```

## 3. 验证结果（2026-08-09）

### 3.1 Windows（MSVC RelWithDebInfo）

```text
runtime：7/7 R1.8 用例 PASS（0B 5 个 + 0C 2 个）
unit / integration：全 PASS
CTest CORE 8/8 PASS（含 checkpoint-cross-process 与
  stable-checkpoint-cross-process —— 证明 R12C envelope 重构字节兼容）
```

### 3.2 WSL Ubuntu 22.04（GCC）

```text
runtime：7/7 R1.8 用例 PASS
unit / integration：全 PASS
```

## 4. 语义验收点

```text
1. CreateCheckpoint 在未 prepare 时 InvalidState；
   out-of-subset（如 paused）时 UnsupportedDeterministicState
2. wire round trip 保持 frame / canonicalTime / morphOverrides /
   fingerprint 逐字段一致
3. restore + continue（16..30）与 from-start（1..30）在 frame 30：
   pose 矩阵完全一致、time 一致、morph weight 一致、
   root-motion delta 一致
4. ReplayFromCheckpoint(15→30) 与 from-start 等价
5. wire 篡改 / 截断 / build 身份不匹配 → InvalidCheckpoint
6. 语义拒绝：clip index 越界、fingerprint 不匹配、帧域越界、
   canonicalTime 不一致、未知 morph name → InvalidCheckpoint
7. Saba R12C payload 字节兼容（跨进程 checkpoint 回归通过）
```

## 5. Phase 0C 边界确认

```text
未做：OfflineFrameSequence 运行时无关化（0D）、四矩阵（0E）、
      Saba 迁移到 IDeterministicCheckpoint（0D 按 capability 分派）
```

## 6. 下一步

Phase 0D：OfflineFrameSequence 改为 `IModelRuntimeDriver&` +
capability/interface 门控，Generic 序列跑零窗口 RenderRange/Resume。

