# R1.9 Final Micro Fix 基线（2026-08-10）

> 状态：**COMPLETED**；0E HOLD 至复审通过。

## 1. 修复清单

```text
P0-1  Scene::BindModelInstanceParts（engine-owned，ResolveMesh）
       InstantiateModel 与 stable borrow 共用；RenderParts 不再为空
P0-2  单帧 fill 前 PublishCurrentRuntimeFrame；size query 无副作用
P0-3  Static entity 恒有 ModelInstance；restore null runtime →
       INVALID_CHECKPOINT
4.    owner 绑定只在真实 GPU 操作前
5.    teardown MakeCurrent 失败 → std::terminate（fail-stop）
6.    ProbeCheckpointEnvelope 完整 envelope（header/size/build/checksum）
7.    ModelAssetBundle 共享 ProgramCache 参数
8.    像素正确性 + 生命周期测试
9.    ctest WORKING_DIRECTORY 统一（shader 相对路径）
```

## 2. 代码改动

```text
src/scene/scene.cpp + scene.hpp
  Scene::BindModelInstanceParts（InstantiateModel 复用）
src/native/wisteria_stable_render.cpp
  borrow 后 BindModelInstanceParts；fill 前 Publish；
  owner 绑定移到真实渲染前；默认平行光（非背景画面）
src/native/wisteria_stable_runtime.cpp
  Static 恒建 ModelInstance；restore null runtime 拒绝
src/native/internal/stable_native_context.hpp
  teardown MakeCurrent 失败 → fail-stop
src/runtime/checkpoint_serialization.cpp
  Probe 验证完整 envelope + checksum（noexcept 无分配）
include/wisteria/assets/model_asset_bundle.hpp + src/assets/*.cpp
  BuildModelAssetBundle 增加 programCache 参数
src/assets/manager.cpp
  LoadModel 传 this->programCache
CMakeLists.txt
  tier 测试统一 WORKING_DIRECTORY
tests/integration_tests.cpp
  +非背景 / 帧间变化 / stable==engine 像素 /
   Static render + Static checkpoint restore 拒绝 /
   size-query 无 owner 副作用
```

## 3. 验证结果（2026-08-10）

```text
Windows CORE：9/9 PASS（integration 8/8 R1.9 用例，ctest 环境修复）
Windows FULL：integration PASS（生产资产）
Linux CORE（WSL，llvmpipe）：11/11 PASS
ABI safety matrix：94 legacy + 30 stable
```

## 4. 当前状态

```text
R1.9 0A  CLOSED ✅
R1.9 0B  FINAL MICRO FIX APPLIED（待复审）
R1.9 0C  CLOSED ✅
R1.9 0D  FINAL MICRO FIX APPLIED（待复审）
R1.9 0E  HOLD
```

