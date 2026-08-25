# WISTERIA 发布说明

## v1.1.0（开发中）

首个 SDK 版本，正式发布 **Stable C ABI** 安装包。

### SDK 内容

- `Wisteria::native`：共享库，导出 stable runtime / render C ABI。
- `Wisteria::cpp`：header-only C++ RAII 封装（Context / Entity / Checkpoint / RenderSession）。
- 公共头文件：
  - `wisteria/native/wisteria_stable_runtime.h`（runtime ABI v1）
  - `wisteria/native/wisteria_stable_render.h`（render ABI v1）
  - `wisteria/native/wisteria_native.h`（legacy v0.7，experimental）
  - `wisteria/core/version.hpp`
  - `wisteria/sdk/`：C++ RAII 封装头文件
- CMake package：`find_package(Wisteria 1.1 CONFIG REQUIRED)`。
- 纯 C 消费测试：`tests/sdk_consumer/`，并已接入 CTest。
- C++ 消费测试：`tests/cpp_sdk_consumer/`，并已接入 CTest。

### 使用

```powershell
.\run.ps1 sdk        # 构建、安装 SDK 并运行消费测试
.\run.ps1 package    # 构建并生成 SDK zip 包
```

详细文档：`docs/SDK.md`。

### 已知边界

- `wisteria/sdk/` 之外的 C++ 引擎头文件仍为源码级 API，不承诺二进制兼容。
- `wisteria_core` / `wisteria_platform` 的 install/export 尚未提供。
- legacy `wisteria_native.h` v0.7 不参与稳定承诺。

---

## v1.0.0

首个正式封版：以“可运行 MMD 技术演示 + 可复用的运行时/渲染核心 + 冻结工程文档”为交付形态。

### 版本定位

- 主演示：PMX + VMD 的 MMD 全链路实时播放。
- 核心运行时：Saba 后端负责 VMD 动画、Morph、IK、物理与 CPU 蒙皮；
  WISTERIA 负责资源管理、场景编排与 OpenGL 渲染。
- 实验面：Native C ABI、确定性时间线/检查点、Headless 与离线帧序列输出。

### 主要能力

- PMX 导入（含 BDEF/SDEF/QDEF CPU 蒙皮）与 VMD 动作/相机/灯光导入。
- MMD 刚体物理：Saba 自有 Bullet world，默认 120Hz 固定步长、最多 10 子步。
- 渲染：阴影贴图（CSM）、地面阴影投影、程序化天空、OIT、Morph 混合、物理调试线框。
- 确定性能力：时间线步进、物理快照 RestoreState、FrameCheckpoint 创建/恢复/回放。
- Headless：EGL headless context、离屏渲染与确定性帧序列输出。
- 测试：12 个 CTest 目标覆盖单元、运行时、集成、渲染、C ABI 与跨进程检查点。

### 运行

Windows PowerShell：

```powershell
.\run.ps1 run    # 构建并启动默认 MMD 演示
.\run.ps1 test   # 构建并运行全部测试
```

演示模型与动作**不随仓库分发**。首次运行前请先准备资产：

```powershell
.\script\setup_demo_assets.ps1 -SourceRoot <包含 models/motions 的目录>
```

或按 `docs/ASSETS.md` 手工放置到 `assets/models/` 与 `assets/motions/`。资产缺失时可用 `--ground-lab` 运行不依赖模型的地面阴影场景。

窗口控制：`Space` 暂停/继续，`C` 开关 VMD 相机，`←/→` 调整相机速度。

### 已知边界（本版本明确不承诺）

- `R2_1_VULKAN_BACKEND_CONTRACT.md` 仅为契约草案，Vulkan 后端未实现。
- RPC bridge（JSON-RPC over stdio）仅有设计与契约草案，未实现。
- `MmdPhysicsInstance + PhysicsWorld + MmdPhysicsRuntimePolicy` 属于
  Legacy WISTERIA-owned physics path，不参与 Saba 主运行链。
- MMD 社区兼容 preset（R1.3B 后续）尚未完成。
- Native C ABI 仍标记为 experimental，暂不作为二进制兼容承诺。

### 工程文档

- 架构契约：`docs/architecture/`，索引见 `docs/README.md`。
- 验证基线：`docs/validation/`。
- 物理分层：`docs/architecture/PHYSICS_LAYER_AUDIT.md`。
- R1 → R2 边界：`docs/architecture/R1_TO_R2_BOUNDARY_AUDIT.md`。

### 许可证

WISTERIA 自身代码以 MIT License 发布（见 `LICENSE`）。第三方库与演示资产保留各自原始许可证；仓库不重新分发 `assets/models/` 与 `assets/motions/` 下的模型和动作文件。
