# MMD 性能路线图

## 当前基线（2026-08-04，叶瞬光 + 身体动作 VMD）

优化后 profile（Saba MESH 窗口）：

| 指标 | 优化前 | 优化后 |
|---|---|---|
| 每帧上传总量 | ~60ms（24 × 全量 63277 顶点） | ~2ms（24 × 子集） |
| Saba CPU（物理+蒙皮） | 22–27ms | ~5.6ms |
| 帧率 | ~49fps | ~65fps |

## 已落地的优化

- **顶点子集化**：每个子网格只存自己材质用到的顶点 + 局部索引 +
  `sourceVertexIndices` 全局映射；每帧只重建/上传子集，上传量降 ~30 倍。
- **交错缓冲重建纯函数**：`Mesh::RebuildInterleavedVertices` 避免连续块覆盖
  布局，并有单测防护。
- **GL 上传移到渲染 context**：`MeshDynamicVertexProvider` 由 Renderer 在
  窗口 context 内调用，避免无效调用和跨 context 错误。

## 未来优化方向（按性价比排序）

### 1. Saba 物理参数对照（已完成，阶段 4）

当前 Saba 内部默认 120Hz / 最多 10 子步，`updateAvgMs` 大头在物理。
做法：`SabaPhysicsSettings` 暴露 `fixedTimeStep` / `maxSubSteps` / `gravity`，
demo 用环境变量 `WISTERIA_SABA_PHYSICS_FPS`、`WISTERIA_SABA_PHYSICS_MAXSTEPS`
做 120/60 对照，量化“帧率提升 vs 物理手感损失”。

结论（2026-08-04）：120Hz 与 60Hz 都流畅，60Hz 每帧 Saba CPU 开销再降约
1ms，物理手感可接受。接口已可配置（构造参数 / `SetPhysicsSettings`），
不再写死；未来接入播放列表时，物理参数可以做成每个动作一个配置。

### 2. CPU 蒙皮并行化

63277 顶点逐顶点蒙皮可并行。Saba 自带 `PMXModel::SetParallelUpdateHint`，
当前不是主瓶颈，留作后续。

### 3. GPU Compute 蒙皮（终极方案）

目标：顶点属性一次上传，每帧在 GPU 用 compute shader 做 BDEF/SDEF/QDEF
蒙皮，写回 SSBO。保留 SDEF/QDEF 效果，同时消除 CPU 蒙皮 + 逐帧上传。
工程量大，建议阶段 5 后再评估。

### 4. 缓存策略分析

旧实现的“上传一次缓存”对**静态/低频数据**有效（骨骼矩阵按 Pose revision
缓存、morph 缓存）。Saba CPU 蒙皮输出每帧变化，不能整体缓存；只能减少
更新量（已完成）或把蒙皮搬到 GPU（方向 3）。

## 测量方式

- `[SABA PROFILE] frames=... updateAvgMs=... uploadAvgMs=...`
  （demo 每 60 帧输出一次）；
- 旧实现已删除，不再有双窗口对照；对照基准改为
  `simple_mmd_viewer_glfw`（Saba 官方 viewer）。
