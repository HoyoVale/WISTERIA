# R1.9 Phase 0B — Runtime / Capability C ABI 实现基线（2026-08-09）

> 状态：**COMPLETED**。
> 契约：`docs/architecture/R1_9_STABLE_RUNTIME_RENDER_ABI_CONTRACT.md`。

## 1. 一句话

Stable C ABI 的实体层正式后端无关：同一个 `WisteriaEntity` 承载
Saba（payload kind 1）与 Generic（payload kind 2），capability 查询
直接映射引擎 authoritative 模型，checkpoint 按 payload kind 分派，
新增 persistent morph override 与 asset fingerprint 稳定函数。

## 2. 代码改动

```text
include/wisteria/native/wisteria_stable_runtime.h
  +WISTERIA_BACKEND_ID_WISTERIA_GENERIC 2u
  +WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18 2u
  +WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_GENERIC_R18 1u
  +WISTERIA_DETERMINISTIC_PROFILE_GENERIC_V1 2u
  +WISTERIA_STATUS_UNSUPPORTED 17u
  +wisteria_stable_entity_set_morph_override
  +wisteria_stable_entity_clear_morph_override
  +wisteria_stable_entity_clear_all_morph_overrides
  +wisteria_stable_entity_asset_fingerprint

src/native/internal/stable_native_context.hpp
  StableEntityEntry 拥有 meshes/material（晚于 asset 析构，
    修复 RenderPart 悬垂）
  checkpoints map 值改为 variant<FrameCheckpoint,
    GenericRuntimeCheckpoint>

src/native/wisteria_stable_runtime.cpp
  entity_create：engine 选择 backend（PMX→Saba，其他→按
    skeleton/morph/animation → Generic/Static）；asset 完整构建
    （skeleton/morphs/clips/parts）
  capabilities：按 runtime 映射 backend id / payload kind /
    profile / max frame；能力位来自 deterministic（authoritative）
  prepare/step/replay：IDeterministicFrameStepper 后端无关；
    MMD-only（motion/preview）保留 MmdRuntimeModel 门
  checkpoint：variant 分派 create/restore/info/serialize/
    deserialize（deserialize 按 wire header payload kind 选择 codec）
  LowerAsciiExtension：截断 MSVC path::string() 的尾随 NUL
    （Windows 上 ".pmx\0" != ".pmx" 的真实 bug）

tests/integration_tests.cpp
  +TestStableRuntimeGenericAbi：
    Generic entity create / capabilities（backend=2, kind=2,
    max=2^20）/ fingerprint / prepare+step+replay /
    checkpoint create/info/serialize/deserialize/restore /
    morph override unsupported / clear

docs/architecture/C_ABI_SAFETY_MATRIX.md（重新生成）
  94 legacy + 23 stable exports
```

## 3. 验证结果（2026-08-09）

```text
Windows CORE：8/8 PASS（integration 含 R1.4 stable E2E + R1.9 Generic）
Windows FULL：integration PASS（R1.4 stable E2E / motion lifecycle /
  R1.9 Generic，生产 PMX/VMD 路径无回归）
Linux CORE（WSL，llvmpipe）：9/9 PASS
```

## 4. 语义验收点

```text
1. Saba PMX 实体 capabilities 不变：backend=1、kind=1、max=2^24
2. Generic glTF 实体：backend=2、kind=2、profile=2、max=2^20、
   exact + checkpoint 能力位齐全
3. 同一 stable 函数集同时服务两种 backend，无 backend selector
4. checkpoint wire 按 kind 分派；Saba kind-1 跨进程回归通过
5. morph override 无 morph 时返回 UNSUPPORTED（显式，非静默）
6. asset fingerprint 非零且来自 ModelAsset 全量指纹
7. entity 生命周期：mesh/material 晚于 asset 析构（Linux 悬垂修复）
```

## 5. Phase 0B 边界确认

```text
未做：Generic 跨进程 checkpoint（0C）、Render/offline C 面（0D）、
      0E ABI 矩阵
```

## 6. 下一步

Phase 0C：Deterministic stepping + checkpoint C ABI（Generic 跨进程
checkpoint、payload kind 2 的 stable-checkpoint CLI 路径）。

