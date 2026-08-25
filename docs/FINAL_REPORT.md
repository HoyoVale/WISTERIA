# WISTERIA v1.0.0 最终收尾报告

> 本报告是 v1.0.0 封版时的工程总结。面向两类读者：想快速理解项目全貌的新读者，以及之后继续做稳定引擎库（SDK）的维护者。

## 1. 项目定位

WISTERIA 是一个以 OpenGL、Assimp、GLM 和 Bullet 构建的 C++20 实时渲染实验引擎。项目的实际主线不是“通用引擎框架”，而是一条可验证的 MMD 纵向链路：

```text
PMX 导入 → VMD 动画 → Morph → IK → 刚体物理 → 物理后骨骼 → OpenGL 渲染
```

v1.0.0 决定以 **A + D** 形态封版：

- **A**：可运行、可展示的 MMD 技术演示。
- **D**：工程文档与验证过程归档，为后续开发保留决策上下文。

稳定引擎库（SDK 化）是下一阶段目标，不在 v1.0.0 范围内。

## 2. 版本内容

### 2.1 可运行演示

- 默认 MMD 演示：`蕾米埃尔-黑` 角色 + `梦的翅膀` 动作/相机轨。
- 场景模式：舞台 PMX + 角色。
- 地面阴影测试场景：`--ground-lab`，不依赖任何外部资产。
- 渲染冒烟：`--frames N --fixed-dt <秒>`，用于无交互验证。
- 备用模型 preset：`--alternate-model`。

### 2.2 引擎能力

| 领域 | 内容 |
| --- | --- |
| 资源导入 | PMX（Saba 导入器）、glTF/GLB（Assimp）、纹理、Shader |
| 动画 | 骨骼、Pose、Morph、VMD 动作/相机/灯光 |
| 物理 | Saba 自有 Bullet world，默认 120Hz 固定步长 |
| 渲染 | CSM、地面阴影、程序化天空、OIT、物理调试线框 |
| 确定性 | 时间线步进、物理快照 RestoreState、FrameCheckpoint |
| Headless | EGL headless context、离线帧序列 |
| C ABI | legacy v0.7 与 stable runtime/render 实验接口 |

### 2.3 测试与验证

v1.0.0 封版时 Windows RelWithDebInfo 构建下：

```text
12/12 CTest passed
```

覆盖：

- 单元测试、运行时测试、集成测试
- C ABI 安全矩阵
- 渲染设备/资产中立性编译检查
- FBO 渲染测试
- C 语言 ABI 冒烟
- 三类跨进程 checkpoint 测试
- stable ABI ctypes 测试

## 3. 架构主线

### 3.1 模型运行时

```text
Scene / Entity / Renderer
        ↓ 只依赖
IModelRuntimeDriver
        ↑
ModelBackendRegistry → SabaMmdBackend → SabaMmdRuntimeModel
                      → WisteriaGenericRuntimeDriver（通用确定性运行时）
```

`Scene` 不关心后端是 Saba、glTF 还是未来其他格式。模型后端由导入结果决定：
PMX 默认进入 Saba 后端，带骨骼/动画/Morph 的其他资产进入 Generic runtime，
其余为静态模型。

### 3.2 物理边界

```text
physics/                  格式无关 Bullet 后端
mmd/physics/              PMX/MMD 运行时适配
SabaMmdRuntimeModel       当前活动 MMD 物理后端（Saba 自有 Bullet world）
```

`MmdPhysicsInstance + 共享 PhysicsWorld + MmdPhysicsRuntimePolicy`
属于 Legacy WISTERIA-owned physics path，不参与 Saba 主运行链，
保留为未来通用 `PhysicsInstance` 的参考。

### 3.3 渲染主线

R2.0 完成渲染后端中立化：

```text
RenderFramePacket → RenderGraph → RenderPass
                                  ├─ OpenGL backend（当前实现）
                                  └─ Vulkan backend（仅契约草案）
```

CPU 资产与 GPU 资源已分离（R2.0 Phase 0C），presentation 通过
PresentSurface 显式表达（R2.0 Phase 0E）。

### 3.4 确定性能力

R1.2 至 R1.9 依次落地：

- 确定性时间线与物理回放
- PhysicsSnapshot RestoreState
- FrameCheckpoint 编排与跨进程等价性
- Stable runtime boundary 与 C ABI
- 第二动态运行时（Generic deterministic runtime）
- Headless 与离线帧序列输出

## 4. 工程过程回顾

项目采用“契约 → 实现 → 验证报告”的阶段开发方式：

- `docs/architecture/`：契约与设计，冻结语义。
- `docs/validation/`：实现基线与收口报告。
- `git log`：每次阶段收口都有独立 commit。

主要里程碑：

| 时间 | 里程碑 |
| --- | --- |
| 2026-08-05 | R1.0 校正基线 |
| 2026-08-06 | R1.2 确定性时间线 / RestoreState / FrameCheckpoint |
| 2026-08-07 | R1.3 MMD 物理兼容契约 Phase 0A |
| 2026-08-08 | R1.4 / R1.5 运行时边界与第二动态运行时 |
| 2026-08-09 | R1.6–R1.8 离线输出、Headless、Generic runtime |
| 2026-08-10 | R1.9 稳定 Runtime / Render C ABI |
| 2026-08-11 | R2.0 Phase 0C / 0D 渲染架构 |
| 2026-08-12 | R2.0 最终架构收口 |

## 5. 已知边界与遗留项

以下内容在 v1.0.0 中**明确不承诺**：

1. **稳定引擎库 / SDK**：尚无 install 规则、SDK 发布包与二进制兼容承诺。
2. **RPC bridge**：`RPC_BRIDGE_DESIGN.md` 已 design closed，契约草案已冻结
   大部分语义；但无实现，P0 “Entity 双世界”待拍板。
3. **Vulkan 后端**：`R2_1_VULKAN_BACKEND_CONTRACT.md` 仅为 Phase 0A 草案。
4. **MMD 社区兼容 preset**：R1.3B 的完整轨迹对照与单位审计未完成。
5. **Legacy physics path**：保留代码，不参与主运行链。
6. **演示资产**：仓库不分发 PMX/VMD，需用户按 `docs/ASSETS.md` 准备。

## 6. 资产与许可证策略

- WISTERIA 代码：MIT License（`LICENSE`）。
- `assets/models/` 与 `assets/motions/` 不入库。
- 资产准备：`script/setup_demo_assets.ps1`，说明见 `docs/ASSETS.md`。
- 第三方库：`third-party/` 下各自保留原许可证。

## 7. 后续路线（B：稳定引擎库）

建议顺序：

1. 冻结 public C++ SDK surface：`core`、`animation`、`physics`、
   `assets`、`scene`、`runtime` 中的稳定头文件清单。
2. 以 stable C ABI 为边界，定义版本与兼容策略（现有测试作为回归底座）。
3. 增加 CMake `install` 规则：头文件、库、示例、pkg-config/CMake config。
4. 编写 `docs/SDK.md`：生命周期、线程模型、错误处理、后端扩展点。
5. 在 SDK 之上评估 RPC bridge 是否进入 v1.1。

## 8. 封版记录

```text
版本       v1.0.0
形态       A（Demo）+ D（Archive）
测试       12/12 CTest passed
许可证     MIT
资产       setup_demo_assets.ps1 + docs/ASSETS.md
文档索引   docs/README.md
```
