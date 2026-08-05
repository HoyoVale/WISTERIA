# MMD 参考实现对照轨迹（#5 Phase 0.2 框架）

> 状态：**完整可用**。babylon-mmd 参考轨迹导出器已跑通（加载 → ammo 物理
> → CPU 蒙皮 → CSV），并已产出与 WISTERIA 基线的第一次对照结果。

## 参考实现选型（已核实）

- **libmmd**（itsuhane/libmmd）：作者已标记 obsolete，基本不支持 PMX，
  只能当逐行审计参考，**不能跑对照**。
- **babylon-mmd**（noname0310/babylon-mmd）：活跃的 PMX/PMD + VMD/VPD
  runtime，Bullet（ammo.js）物理。**在 Node 24 headless（NullEngine）下
  已验证可加载我们的 PMX**：解析出 24 个材质、604 个骨骼（与 WISTERIA
  引擎完全一致），模型元数据 `isMmdModel: true`。

## 环境与运行

```bash
cd tools/reference_trace
npm init -y
npm install babylon-mmd @babylonjs/core esbuild
npx esbuild spike_load.mjs --bundle --platform=node --format=esm --outfile=bundle.mjs
node bundle.mjs "<model.pmx>"
```

完整轨迹导出（`trace.mjs`）：

```bash
npm install ammojs-typed
npx esbuild trace.mjs --bundle --platform=node --format=cjs --outfile=bundle_trace.cjs
node bundle_trace.cjs "<model.pmx>" "<out.csv>" 300 10
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
- 坐标约定：saba 与 babylon-mmd 的 **Z 轴相反**，逐项对比时需对一侧取反。

## 第一次对照结果（叶瞬光，physics-only，120Hz，300 帧）

| 实现 | frame 10 | frame 300 | 收敛 |
|---|---|---|---|
| WISTERIA（saba） | 0.052 | 0.068 | 30 帧内收敛 |
| babylon-mmd | 0.79 | 8.08 | 300 帧仍在缓慢增长 |

**发现**：同一资产、同一配置下，saba 的物理几乎静止（位移 0.068），而
babylon-mmd 的裙摆/头发持续下落（位移 8.08）。这是社区差异矩阵的第一个
真实分歧，下一步需要：确认是刚体分类/kinematic 驱动差异，还是固定步/重力
处理差异，再决定 WISTERIA 侧是否补“物理模式”语义。

## 产出目标（已达成）

同一资产（叶瞬光.pmx，无 VMD）在两个实现下的物理轨迹 CSV：

```text
frame,min_x,min_y,min_z,max_x,max_y,max_z,max_displacement
```
