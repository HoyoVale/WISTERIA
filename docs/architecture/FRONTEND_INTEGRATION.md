# 前端接入与模型格式路线

## 1. 数据类型：不要盲目把 `unsigned int` 换成 `size_t`

项目里的索引/句柄分三类，各用各的正确类型，**不做全局替换**：

| 数据 | 当前类型 | 为什么这样是对的 |
|---|---|---|
| GPU 索引（Mesh::IndexType） | `unsigned int`（32 位） | EBO/`glDrawElements` 用 `GL_UNSIGNED_INT`；`size_t` 在 x64 是 64 位，会破坏跨平台 ABI 与上传格式 |
| 资源句柄（BoneIndex / MorphIndex / RigidBodyIndex） | `std::uint32_t` | 固定宽度、可序列化、跨进程稳定；已正确 |
| 宿主侧数量/大小（vertexCount、bytes、`vector::size`） | `size_t` | 只活在进程内，不跨边界，已是正确选择 |

结论：**内部类型不动**。将来做 C ABI / WASM 时，在绑定层定义固定宽度类型
（`uint32_t`/`uint64_t` + 不透明句柄），不要直接暴露 `size_t` 或裸指针。

## 2. 前端控制：需要一层独立接口，但不是“改内部类型”

按前端形态分三种接入方式：

### A. 桌面窗口 + 前端控制（推荐先做）

Electron/React 前端控制现有 GLFW 渲染窗口：

- 新增 `wisteria_control` 层：JSON 命令/事件，通道用 stdin、WebSocket 或
  named pipe；
- 命令：`load_model`、`play_motion`、`set_camera`、`set_speed`、
  `pause/resume/reset`、`query_stats`；
- 事件：`model_loaded`、`frame_stats`、`log`、`error`；
- 关键约束：命令进队列，渲染线程消费，前端不直接碰 GL context。

改动最小，C++ 核心完全不动。

### B. 前端自己渲染（WebGL/WebGPU）

把 importer/runtime/物理/蒙皮计算编译到 WASM，渲染后端换成 WebGL/WebGPU。
工程量最大：renderer 后端抽象、纹理/着色器迁移、ABI 边界。MMD 的 CPU 蒙皮
可以保留，Bullet/glm/Saba 均有 WASM 可行性，但属于独立项目。

只有在这种形态下，“跨边界类型”才真正重要：WASM ABI 只允许固定宽度类型，
禁止 `size_t` 和指针。

### C. 原生模块（NAPI / FFI）

把引擎包成 DLL + `extern "C"` 门面，前端（Electron main / Tauri）直接调用。
比 A 少一层序列化，但需要自己管生命周期和线程模型。

**建议**：先做 A（命令协议），同时预留 C 的 C ABI 门面；B 作为长期目标
单独评估。

## 3. MMD 链路完成度与下一个格式

### MMD 已完成

- PMX 导入（骨骼/材质/纹理/morph/刚体/关节，15 个角色 + 2 个场景容错）；
- Saba 动画 / IK / morph / 物理 / CPU 蒙皮（BDEF/SDEF/QDEF）；
- VMD 动作、相机接口；场景 PMX 浏览（`--scene` 自动取景）；
- 演示：梦的翅膀 + 镜头循环；相机速度接口。

### MMD 剩余缺口（都不是阻塞项）

- PMD 老格式（Saba 有 `PMDFile` 解析器，我们的 importer 目前只收 `.pmx`）；
- VPD 姿势导入（Saba 原生支持，未接）；
- 音频同步（MMD 动作带 wav/mp3）；
- GPU compute 蒙皮优化；
- 多动作编排（按既定决策不做，薄接口已预留）。

### 下一个格式：glTF/GLB（第一优先）

理由：

- 通用标准：Blender/3ds Max 工具链和 web（three.js）都原生支持，前端接入
  时 glTF 是成本最低的跨端格式；
- 资源层已经就绪：`ModelImporter`（Assimp）已启用 OBJ/GLTF/MMD，输出
  `ImportedModelData`；
- 运行时层有现成基础：`RuntimeModelBase` 设计时已预留
  `GltfRuntimeModel`；Animator/Pose/MorphState 和 Renderer 的 GPU 蒙皮
  路径都还在（Saba 之外的通用链路），可复用；
- 接口契约文档（`SABA_ADAPTER_INTERFACE.md`）里的分层图已经把这格画好了。

### 其他格式优先级

| 格式 | 优先级 | 说明 |
|---|---|---|
| glTF/GLB | 高 | 通用标准 + 前端友好 |
| PMD | 中 | Saba 已能解析，补上让 MMD 兼容完整 |
| FBX | 低 | Assimp 顺带支持，动画/蒙皮质量参差 |
| OBJ | 已支持 | 静态模型 |

### 建议路线

1. Stage A：glTF 导入数据对照（沿用 `TestSabaMmdImporterWhenAvailable`
   的对照模式，Saba 链 vs Assimp 链）；
2. Stage B：`GltfRuntimeModel`（动画采样 + GPU 蒙皮 + morph）；
3. Stage C：前端命令协议（第 2 节方案 A）。
