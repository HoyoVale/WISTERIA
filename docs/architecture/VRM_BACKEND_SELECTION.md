# VRM 后端选型（C5）

> 状态：调研完成，推荐方案已确定（2026-08-25）。

## 1. 结论

**采用 `infosia/VRM.h` 作为 VRM 扩展数据解析层，渲染与动画运行时继续由 WISTERIA 自研。**

```text
infosia/VRM.h（MIT，header-only）
  → 解析 VRM 0.x / 1.0 的 JSON extension
  → WISTERIA 自己负责：
       glTF JSON 提取
       humanoid 骨骼映射
       expression / lookAt
       spring bone 运行时（后续用 Bullet）
       ModelAsset / Runtime 接入
       OpenGL 渲染
```

这与 MMD 的 Saba 模式有本质区别：Saba 是完整运行时；VRM.h 只是规范数据模型解析器。原因是 C++ 生态里没有 Saba 级别的成熟 VRM 运行时后端，成熟实现多在 TypeScript / Rust / Unity 侧。

## 2. 候选对比

| 候选 | 语言 | License | 评估 |
| --- | --- | --- | --- |
| `infosia/VRM.h` | C++ header-only | MIT | ✅ 采用。支持 VRM 0.x/1.0、springBone、node constraint、MToon、VRMA extension |
| `vittorioromeo/vrm_core` | C++ | NOASSERTION | ❌ 经查与 VRM 无关，是 SSVUtils 工具库 |
| `unavi-xyz/unavi` | Rust | MPL-2.0 | ❌ 是完整社交 VR 引擎，引入成本过高 |
| Blender mmd_tools | Python | GPL | ❌ 只适合离线转换，不能 vendor 进引擎 |
| 完全自研 VRM parser | C++ | MIT | 备选。若 VRM.h 无法满足再考虑 |

## 3. 为什么 VRM.h 只承担解析

VRM 的难点有两层：

```text
数据层：VRM extension JSON 结构        ← VRM.h 覆盖
运行时层：
  humanoid 骨骼语义                    ← WISTERIA 映射
  expression preset / custom           ← WISTERIA MorphState
  lookAt                               ← WISTERIA Pose 后处理
  springBone / collider                ← WISTERIA + Bullet（后续）
  MToon 材质                           ← WISTERIA Shader（后续）
```

## 4. 实施计划

```text
C5A  Vendor VRM.h 到 third-party/VRM.h
      记录 commit、MIT license、本地编译回归

C5B  glTF/GLB JSON 提取工具
      用现有 miniz + nlohmann/json 从 .vrm/.glb 中取 JSON chunk

C5C  VRM 0.x / 1.0 元数据 + humanoid 映射
      ✅ VRM 0.x / 1.0 元数据与 humanoid 节点→骨骼映射已实现
      ✅ `.vrm` 进入 GLB 容器导入路径，`--gltf <model.vrm>` 可加载
      ⏳ Generic runtime 消费 humanoid 语义、表情/lookAt 驱动待后续

C5D  Expression / lookAt
      ✅ Expression morph bind 数据层已导入（0.x/1.0）
      ⏳ expression → MorphState 运行时驱动、lookAt Pose 后处理

C5E  Spring bone（物理）
      先接 Bullet 通用链路，再做 spring bone 专用适配

C5F  MToon
      实现或近似为现有 MmdToon / PBR variant
```

## 5. 验收标准

- 不新增渲染后端，VRM 仍走现有 RenderGraph / OpenGL。
- `--gltf` 查看器可直接加载 `.vrm`（VRM 0.x / 1.0 至少一种）。
- humanoid 骨骼、expression、lookAt 通过 fixture 测试。
- CTest 全绿。
