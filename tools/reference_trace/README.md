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

叶瞬光.pmx 刚体构成：**495 个**（38 FollowBone / 74 Physics /
383 PhysicsWithBone），质量 0.01–218.31，模型以 mode 2 为主。

| 实现 | frame 10 | frame 300 | 收敛 |
|---|---|---|---|
| WISTERIA（saba） | 0.052 | 0.068 | 30 帧内收敛 |
| babylon-mmd | 0.79 | 8.08 | 水平收缩（x/z ±10.5→±6.8），未收敛 |

**调查结论（已排除的假设）**：

- 不是“无 VMD 运行时漂移”：关掉物理（`buildPhysics:false`）后 babylon 位移
  ≈ 0.000002，运行时静止正确；
- 不是地面缺失：babylon-mmd 物理世界确实没有 MMD 地面（saba 有 y=0 静态
  平面），harness 已补地面但 min_y 稳定在 0.049、曲线不变——模型不是下落，
  是 **mode-2 刚体水平向内收拢**；
- 不是刚体分类差异：双方读同一个 PMX mode 字段。

**真正分歧**：在 **mode 2（PhysicsWithBone）** 处理上——saba 的
`DynamicAndBoneMergeMotionState` 让刚体与骨骼互相拉回，模型几乎静止；
babylon-mmd 的 mode-2 刚体 300 帧内向内塌缩。哪个更接近 MMD 官方行为需要
第三参考或逐刚体轨迹对照。下一步：给 saba 侧加逐刚体状态导出（引擎目前
不暴露刚体状态），对比同一刚体的 0~300 帧轨迹。

## 产出目标（已达成）

同一资产（叶瞬光.pmx，无 VMD）在两个实现下的物理轨迹 CSV：

```text
frame,min_x,min_y,min_z,max_x,max_y,max_z,max_displacement
```
