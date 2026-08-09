# R1.9 Phase 0D — Render / Offline Execution C ABI 实现基线（2026-08-09）

> 状态：**COMPLETED**。
> 契约：`docs/architecture/R1_9_STABLE_RUNTIME_RENDER_ABI_CONTRACT.md`。

## 1. 一句话

Stable Render C ABI 上线：外部程序可以创建 headless render session，
对 stable entity 做单帧 `RenderOffline → RGBA8`（size query + caller
buffer），或跑 `OfflineFrameSequence` 的 range/resume——同一
Context/error/handle 约定，跨 Windows（GLFW hidden）与 Linux（EGL）。

## 2. 代码改动

```text
include/wisteria/native/wisteria_stable_render.h（新增）
  WISTERIA_STABLE_RENDER_ABI_VERSION 1u
  WisteriaRenderSession / RenderSessionOptionsV1 / RenderCameraV1 /
  SequenceOptionsV1
  render_session_create/destroy
  render_session_render（单帧 RGBA8）
  render_session_sequence_range/resume/last_committed/failed

src/native/wisteria_stable_render.cpp（新增）
  session 包装 HeadlessRenderSession
  EntityBorrowGuard：stable entity 的 ModelInstance 借入临时 Scene
    → render/sequence → 归还（异常安全）
  camera/projection 显式构建；序列 overwrite policy 映射；
  完整 deterministic capability 门控

src/native/wisteria_stable_runtime.cpp / stable_native_context.hpp
  StableEntityEntry 持有 ModelInstance（支持借用）；
  renderSessions 注册表

src/platform/egl_headless_context.cpp
  +GlfwHeadlessContext（hidden window provider）：
    Windows 主路径 + Linux EGL 失败回退；
    factory 不变量（返回不保持 current）、forceSoftware 严格 gate

src/native/windows_path.cpp
  修复 WisteriaNativeUtf8ToWide：移除 MultiByteToWideChar(-1)
  写入的终止 NUL（路径内嵌 NUL 导致 operator/ 与 rename 全坏）

src/scene/offline_frame_sequence.cpp
  Resume 的 checkpoint ifstream 收窄作用域：
    Windows CRT 无 FILE_SHARE_DELETE，同次 resume 改写恢复源 slot
    会 ERROR_ACCESS_DENIED；AtomicReplace 失败附带 GetLastError

src/scene/entity.cpp
  +Entity::TakeModelInstance()

CMakeLists.txt
  wisteria_stable_render.cpp 加入 wisteria_native

script/gen_abi_safety_matrix.py
  扫描 wisteria_stable_render.h；矩阵 94 legacy + 30 stable

tests/integration_tests.cpp
  +TestStableRenderAbiGeneric：
    session create / 单帧 size query + fill + 确定性（两次一致）/
    sequence range 0..2（PNG+manifest+checkpoint）/ resume 4 /
    last_committed / failed / destroy
```

## 3. 验证结果（2026-08-09）

```text
Windows CORE：9/9 PASS（integration 含 R1.9 render ABI）
Windows FULL：integration PASS（R1.4/R1.9 stable + render，生产资产）
Linux CORE（WSL，llvmpipe）：10/10 PASS（含 generic 跨进程）
```

## 4. 语义验收点

```text
1. 单帧渲染两次字节一致（stable entity exact state 借用渲染）
2. size query 与 caller buffer 模式与 checkpoint serialize 一致
3. sequence range 0..2 + fresh resume 4 全落盘，
   last_committed=4、failed=0
4. Windows 用 GLFW hidden provider；Linux 用 EGL（llvmpipe）——
   同一 stable 面跨 provider
5. factory 不变量保持：session create 后不残留 current context
6. 路径转换修复：无内嵌 NUL（PMX 扩展名、序列输出路径均受益）
```

## 5. Phase 0D 边界确认

```text
未做：RenderDevice/RenderGraph/Vulkan、async/job、scene serialization、
      editor API、raw EGL surface（均 DEFER R2）
```

## 6. 下一步

Phase 0E：ABI compatibility matrix + Python ctypes gate + Node smoke
+ Final Closure。

