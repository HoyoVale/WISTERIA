# MMD 参考实现对照轨迹（#5 Phase 0.2 框架）

> 状态：环境可行性已验证，harness 未完（几何提取 → ammo 物理 → 步进 → CSV）。

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

要点（踩坑记录）：

- babylon-mmd 的 ESM 是无扩展名导入，Node 直跑不行，必须用 esbuild 打包；
- 必须从 **pure 子路径**导入（`pmxLoader.pure.js`），避免 wasm-rayon
  worker 的浏览器全局（`self`）崩溃；
- 直接调插件 `loadFile(ArrayBufferView)` + `importMeshAsync` 绕过 XHR
  （Node 无 XMLHttpRequest）；
- 材质层用桩 `materialBuilder` 跳过（物理轨迹不需要材质），桩必须调用
  `onTextureLoadComplete` 回调，否则 `textureLoadPromise` 永不 resolve
  导致挂起。

## 剩余步骤（下一步）

1. 几何提取：loader 构建了 25 个命名网格但顶点缓冲未挂上（桩材质路径下
   `applyToMesh` 未生效）——需修复或保留真实材质 builder（NullEngine 下
   应该可行）；
2. ammo.js wasm 物理：实例化 `MmdAmmoPhysics`（包内 wasm），接到
   `MmdModel`；
3. `MmdRuntime` 接线 + physics-only 步进（120Hz / maxSubSteps 10，与
   WISTERIA 基线一致，无 VMD）；
4. 按 `WISTERIA_PHYSICS_TRACE` 的 CSV 格式导出每 10 帧的顶点 bounds +
   max displacement；
5. 与 `docs/architecture/MMD_PHYSICS_COMPAT_BASELINE.md` 的基线指标逐帧
   对照（RMS/max，见 Phase 0.3）。

## 产出目标

同一资产（叶瞬光.pmx，无 VMD）在两个实现下的物理轨迹 CSV：

```text
frame,min_x,min_y,min_z,max_x,max_y,max_z,max_displacement
```
