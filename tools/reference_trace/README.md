# MMD 参考实现对照轨迹（R1.3 Phase 0B 工具链）

> 状态：**Phase 0B 工具链已冻结（2026-08-07）**。
> 依赖版本与 lockfile 已固定；synthetic clock calibration 通过；
> babylon-mmd 参考轨迹导出器可运行（加载 → ammo 物理 → CPU 蒙皮 →
> CSV，统一时钟字段）。

## 冻结身份（2026-08-07）

### 源码仓库（commit pin）

| 实现 | commit | 许可证 | 角色 |
| ---- | ------ | ------ | ---- |
| babylon-mmd | `3f523d392c176d5c9c9f9264f622d0631c1d298e` | MIT | Runnable Trace（强制） |
| nanoem | `30acffaa29f5d2eb9e997d69418f2e4b97b5894f` | physics 组件 MIT/X11；应用层 MPL | Source Semantics（feasibility 结论：见 `docs/validation/R1_3B_NANOEM_FEASIBILITY_20260807.md`） |
| libmmd | `091e70c55dc4c6f2e7ad8d46fea92ce3d1849ba5` | Boost Software License 1.0 | Historical Reference |
| blender_mmd_tools | `0ba5ca97c7d9652cca8d3c626553063c146bf315` | GPLv3（仅证据，不复制） | 模型制作/修复侧参考 |
| saba / Bullet | vendored | MIT / zlib | 内部基线 / API 语义 |

### npm 发布包（精确版本 + integrity，lockfile 为准）

| 包 | 版本 | integrity（npm registry） |
| -- | ---- | ------------------------- |
| `babylon-mmd` | 1.3.0 | `sha512-+ms/0h43a77DM/txFGEFY3YnOnJSVlEYDYlRZTDg9m1mterByX+bpRM6R8xg+0iMVZZ86TXUks8NEfMJizuBRQ==` |
| `@babylonjs/core` | 9.20.0 | `sha512-iWM/Re+FnqTEOxkGxvjRjChcPZzydcb0OGtnVk5o0vDQizqwkcpWFlJzfIv1Ij7Li2edJJJDZd6jRzC8QSWuuw==` |
| `ammojs-typed` | 1.0.6 | `sha512-ut/tD0m5eEdlJ5KK97ma6SIEGZ4FN3AJTSVz8wl6C4EtnpVQdP70lOwgDMJKsHjTohxc2/xEZl6yJfMBZNlQ5w==` |
| `esbuild`（dev） | 0.28.1 | 见 package-lock.json |

规则：源码证据引用 commit A 时，实际 trace 必须运行同一 commit 或明确
记录发布包差异（npm artifact 与源码 commit 不是同一份代码）。

## 环境与运行

```bash
cd tools/reference_trace
npm ci                   # 使用 package-lock.json（精确版本，clean install）

npm run spike -- <model.pmx>          # 加载验证
npm run trace -- <model.pmx> <out.csv> [motionFrames] [sampleInterval] [vmdPath] [environmentMode]
npm run calibrate                     # 时钟校准
npm run coordinate                    # 坐标归一化 golden test
```

等价的手工命令：

```bash
npx esbuild spike_load.mjs --bundle --platform=node --format=esm --outfile=bundle.mjs
node bundle.mjs "<model.pmx>"

npx esbuild trace.mjs --bundle --platform=node --format=cjs --outfile=bundle_trace.cjs
node bundle_trace.cjs "<model.pmx>" "<out.csv>" 300 1
```

要点（踩坑记录）：

- babylon-mmd 的 ESM 是无扩展名导入，Node 直跑不行，必须用 esbuild 打包；
- 必须从 **pure 子路径**导入（`pmxLoader.pure.js`），避免 wasm-rayon
  worker 的浏览器全局（`self`）崩溃；
- 直接调插件 `loadFile(ArrayBufferView)` + `importMeshAsync` 绕过 XHR
  （Node 无 XMLHttpRequest）；
- 材质层用桩 `materialBuilder` 跳过（物理轨迹不需要材质），桩必须调用
  `onTextureLoadComplete` 回调，否则 `textureLoadPromise` 永不 resolve
  导致挂起。
- 物理引擎用 babylon-mmd 自己的 `MmdAmmoJSPlugin`（不是 Babylon 原生
  `AmmoJSPlugin`），`setMaxSteps(120)` + `setFixedTimeStep(1/120)` 对齐
  saba 基线；世界重力 `-98`（10:1 尺度，与 saba 一致）。
- 蒙皮在 GPU shader 里，CPU 顶点缓冲永远是绑定位；轨迹读取必须在 CPU 上
  用 `worldTransformMatrices × bone.getAbsoluteInverseBindMatrix()` 手动蒙皮。
- 坐标约定：saba 与 babylon-mmd 的 **Z 轴相反**，逐项对比时按
  ReferenceCoordinateNormalization v1 归一化（见 Phase 0B 契约 §5）。
- 动画时钟：`model.beforePhysics(frameTime)` 接收**绝对 30fps 帧号**
  （babylon-mmd runtime 传入 `elapsedFrameTime`）。trace 因此按
  motionFrame 循环，且与校准共用 frame_driver.mjs：
  `beforePhysics(N) ×1 → 4 个 120Hz tick → afterPhysics ×1`；
  frame 0 为 `beforePhysics(0) → initializePhysics → afterPhysics`。
- VMD 路径必须先在 `createRuntimeAnimation` 前调用
  `RegisterMmdRuntimeModelAnimation()`（pinned babylon-mmd 要求）。
- `environmentMode` 目前只支持 `NormalizedComparison`；
  `NativeCompatibilityAudit` 未实现前显式报错，禁止标签造假。
- 逐刚体读取使用 pinned 版本的内部 API（`model._physicsModel._impostors`
  + `impostor.physicsBody`），版本由 package-lock.json 锁定。
- Normalized ground 当前是 Babylon `CreateGround` + BoxImpostor
  （env 记录为 `synthetic-ground-box-v1`），不是 Saba 的
  `btStaticPlaneShape`；研究 ground/contact topology 前需显式区分。

## 统一时钟与 CSV 输出

CSV 头（Phase 0B 契约 §4.1）：

```text
motionFrame,physicsTick,simulatedSeconds,
min_x,min_y,min_z,max_x,max_y,max_z,max_displacement
```

```text
motionFrame 0 = physicsTick 0   = 0.000s（prepared boundary，无物理步）
motionFrame 1 = physicsTick 4   = 0.033s
motionFrame N = physicsTick 4N  = N/30 s
```

默认每个 motionFrame 采样一行（sampleInterval=1，单位是 motionFrame），
并额外输出 motionFrame 0 的 bind-pose 行。

### 三个输出文件

```text
<out>.csv            聚合网格 bounds + max displacement（每 motionFrame 一行）
<out>.bodies.csv     逐刚体：sourceRigidBodyIndex（PMX index）、
                     world transform（列主序 9 浮点）、线/角速度
<out>.env.json       环境头：environmentMode / executionProfile /
                     gravity / fixedTimeStep / groundPolicy /
                     sourceRepositoryCommit / package version+integrity /
                     model/motion hash / availability
```

bodies.csv 只输出能权威读取的字段；`env.json.availability` 记录
`interpolationTransformAvailable=false`、
`motionStateAvailable=false`、`jointMetricsAvailable=false`、
`contactTopologyAvailable=false`（当前参考适配器不伪造这些字段，
对照时按 NOT_COMPARABLE 处理）。

VMD 路径：第 6 个参数传入 `.vmd` 文件，harness 用 `VmdLoader` 加载并
绑定到模型（`createRuntimeAnimation` + `setRuntimeAnimation`），随后按
motionFrame 语义采样动画并执行物理。

参数校验：`motionFrames` 必须是整数 ≥ 0，`sampleInterval` 必须是整数 ≥ 1，
非法输入直接报错退出。

## 时钟校准（synthetic fixture）

`npm run calibrate` 只用 ammojs-typed（地面 + 一个动态盒）验证参考侧
步进模式与 WISTERIA 确定性时钟一致：

```text
motionFrame=0   physicsTick=0    （0 步）
motionFrame=1   physicsTick=4    （累计 4 步）
motionFrame=2   physicsTick=8    （累计 8 步）
motionFrame=300 physicsTick=1200 （累计 1200 步，10s）
```

WISTERIA 侧同款校验由现有 C++ 测试覆盖：
`TestR13TraceReproducibleAndSchema`（断言 physicsTick == motionFrame × 4）
与 R1.2C 等价性矩阵。

## 第一次对照结果（Historical Preliminary，待复验）

叶瞬光.pmx 刚体构成：**495 个**（38 FollowBone / 74 Physics /
383 PhysicsWithBone），质量 0.01–218.31，模型以 mode 2 为主。

| 实现 | frame 10 | frame 300 | 收敛 |
|---|---|---|---|
| WISTERIA（saba） | 0.052 | 0.068 | 30 帧内收敛 |
| babylon-mmd | 0.79 | 8.08 | 水平收缩（x/z ±10.5→±6.8），未收敛 |

> **按 Phase 0B 契约降级为 Historical Preliminary Observation**：
> 该结果中 babylon 侧“frame 300”是 300 个 120Hz tick（= 2.5s），与
> WISTERIA motionFrame 300（= 10s）不是同一时间轴；统一 Clock 后必须
> 复验，旧数值不得直接作为正式证据。

**调查结论（已排除的假设）**：

- 不是“无 VMD 运行时漂移”：关掉物理（`buildPhysics:false`）后 babylon 位移
  ≈ 0.000002，运行时静止正确；
- 不是地面缺失：babylon-mmd 物理世界确实没有 MMD 地面（saba 有 y=0 静态
  平面），harness 已补地面但 min_y 稳定在 0.049、曲线不变——模型不是下落，
  是 **mode-2 刚体水平向内收拢**；
- 不是刚体分类差异：双方读同一个 PMX mode 字段。

**真正分歧**：在 **mode 2（PhysicsWithBone）** 处理上——saba 的
`DynamicAndBoneMergeMotionState` 让刚体与骨骼互相拉回，模型几乎静止；
babylon-mmd 的 mode-2 刚体 300 帧内向内塌缩。复验路径：统一 Clock →
逐刚体轨迹对比（sourceRigidBodyIndex 对齐）→ nanoem source semantics
第三参考。

## 产出目标（已达成）

同一资产在两个实现下的物理轨迹 CSV：

```text
motionFrame,physicsTick,simulatedSeconds,
min_x,min_y,min_z,max_x,max_y,max_z,max_displacement
```
