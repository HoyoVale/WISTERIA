# glTF / GLB / VRM 第二内容线审计

> 状态：C1 能力审计（2026-08-25）。目标是在 MMD 单线之外，打通一条
> 通用模型渲染线路。原则：简单部分自研，复杂部分参考 MMD 经验引入
> 合适开源后端。

## 1. 现状

当前通用线路已经存在，并且有三类 fixture 测试在跑：

| Fixture | 验证内容 |
| --- | --- |
| `Box.glb` | 静态 GLB 导入 + 渲染 |
| `pbr_quad.gltf` | glTF PBR 材质（baseColor / metallic-roughness） |
| `animated_triangle.gltf` | 骨骼 + 骨骼动画 + Generic runtime + 离线渲染 |

运行时：

```text
ModelImporter (Assimp 5.2.5)
  → ImportedModelData
  → ModelAsset
  → WisteriaGenericRuntimeDriver
      Pose + Animator + MorphState + 确定性时间线
  → GPU skinning（Pose palette）
  → RenderGraph / OpenGL
```

## 2. 能力矩阵

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| glTF 2.0 / GLB 容器 | ✅ | Assimp 读取；`.gltf` / `.glb` 均可 |
| 静态网格 | ✅ | `Box.glb` 测试覆盖 |
| 多 mesh / 多 material | ✅ | parts + materialIndex 映射存在 |
| 骨骼导入 | ✅ | `ImportSkeleton` 基于 aiBone |
| 顶点蒙皮权重 | ✅ | 4-influence 规范化，root fallback |
| GPU 蒙皮 | ✅ | 通过 Pose::SkinningMatrices |
| 骨骼动画 | ✅ | aiNodeAnim 通道按骨骼名导入 |
| 动画 clip 自动播放 | ✅ | Generic runtime 默认播放 clip 0 |
| 节点动画（非骨骼） | ❌ | 当前明确忽略，设计文档已记录 |
| PBR baseColor / metallicRoughness | ✅ | `pbr_quad.gltf` 覆盖 |
| normal / emissive / occlusion 贴图 | ✅ | `ImportMaterial` 已映射 |
| alphaMode / alphaCutoff / doubleSided | ✅ | `ImportMaterial` 已映射 |
| Morph target（mesh） | ✅ C2 已实现 | `ImportGenericMorphTargets` 消费 `aiAnimMesh` |
| Morph weight 动画 | ✅ C2 已实现 | `ImportAnimations` 消费 `aiMeshMorphAnim`，生成 `MorphWeightTrack` |
| 多动画 clip | ✅ | 全部导入，默认播放第 0 个 |
| 场景节点层级动画 | ❌ | 与节点动画同一缺口 |
| glTF 相机 / 灯光 | ❌ | 未接入 Camera/Light（demo 可程序化处理） |
| Draco / KHR 扩展 | 部分 | 依赖 Assimp；未单独验证 |
| 通用角色物理 | ❌ | Generic runtime 明确无物理；glTF 角色先走无物理线路 |
| VRM 0.x / 1.0 | 部分 | VRM 0.x/1.0 元数据 + humanoid 映射已导入；表情驱动、lookAt 运行时、spring bone 未支持 |

## 3. 结论：自研 vs 后端

### glTF/GLB 核心线：自研

理由：

- 基础设施已经存在：Importer、ModelAsset、Generic runtime、GPU skinning、
  MorphState、RenderGraph 都已就绪；
- 两个主要缺口（morph target + morph 动画）的 Assimp 数据已经齐备，
  只是 WISTERIA 没有消费；
- 改动范围集中在 `src/assets/importer.cpp` 和少量测试 fixture。

### VRM：优先寻找成熟后端，找不到再自研解析

VRM 是 glTF 的扩展层，难点不在网格/骨骼，而在：

```text
humanoid 骨骼映射
expression（表情 preset）
lookAt
spring bone（次级物理）
meta / 版权信息
```

建议：

1. 先调研可 vendor 的 C/C++ 实现（例如 VRM 相关 C 库或可集成的
   glTF 后端）；若存在 Saba 级别的成熟项目，按 Saba 模式
   `third-party/<name> + WISTERIA.cmake` 接入。
2. 若候选都不成熟：
   - VRM 0.x：JSON 扩展自研解析，成本可控；
   - VRM 1.0：glTF 扩展自研解析，配合 `vrma` 动画；
   - spring bone 用现有 Bullet 后端做适配；
   - 先保证静态/蒙皮渲染，物理后置。

## 4. 实施顺序

```text
C1  本审计                                                    ✅ 完成
C2  glTF morph target + morph 动画导入                         ✅ 已实现并测试
C3  glTF 基础 Demo：
      ✅ 新增 `--gltf <glb>` 通用查看模式，Generic runtime 驱动
      ✅ 真实 GLB 验证：今汐.glb（30 meshes / 784 bones /
         30 skinned meshes / 96 morphs）导入并成功渲染
      ✅ 包围盒自动取景已实现
      ✅ 两骨骼动画 glTF fixture（tests/data/animated_bone_chain.gltf）
         验证双骨骼蒙皮 + 双通道旋转动画导入与 Pose 驱动
      ⏳ 多资产 PBR 表现（本地 GLB 目前仅含 baseColor 贴图）
C4  PBR 渲染质量：
      IBL / tone mapping / alpha 排序（现有基础）
C5  VRM 调研与选型：
      ✅ 已完成，选定 infosia/VRM.h（MIT）作为扩展解析层
      ✅ C5A/C5B 完成：VRM.h vendor + GLB JSON chunk 提取
      ✅ C5C VRM 0.x Phase 0 完成：元数据解析 + humanoid 骨骼映射
        （`minimal_humanoid.vrm` fixture，见 VRM_BACKEND_SELECTION.md）
      ⏳ 运行时读取、VRM 1.0、demo `--vrm` 待后续

      成熟后端 → vendor；否则自研 VRM0.x 静态链路
C6  Vulkan / 物理等后续路线
```

## 5. 验收标准

每一阶段必须满足：

- 新增 fixture 进入 `tests/data/`（模型体积尽量小）；
- importer 保持 CPU-only，渲染前不需要 GL context；
- Generic runtime 的确定性时间线 / checkpoint 继续通过；
- CTest 全绿；
- 与 MMD 线共用 RenderGraph，不得出现第二套渲染调度。
