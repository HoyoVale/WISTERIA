# MMD 社区物理实现借鉴计划

## 目标

WISTERIA 不再通过不断增加模型名称启发式来猜测 MMD 行为。后续路线是：

```text
先建立可复现的社区兼容基线
→ 再移植有来源的兼容 quirks
→ 最后启用 WISTERIA 自适应增强
→ 特殊模型使用外部 profile
```

## 参考实现角色

### babylon-mmd

仓库：`https://github.com/noname0310/babylon-mmd`

用途：

- 活跃的 PMX/PMD + VMD/VPD 完整 runtime；
- 具有 Bullet physics backend；
- 适合研究现代实现中的 Mode 2、reset、模型 transform、physics toggle、damping 和 constraint 处理；
- 适合作为可运行的逐帧对照对象。

### libmmd Bullet binding

仓库：`https://github.com/itsuhane/libmmd`

用途：

- 代码量较小，适合逐行审计 body/bone motion state；
- `strict` 模式明确体现“骨骼长度不应被物理摇动”；
- 作者明确说明它只是参考绑定，行为可能不同于 MMD，因此不能作为唯一真值。

### Saba

仓库：`https://github.com/benikabocha/saba`

用途：

- C++、OpenGL、Bullet 技术栈与 WISTERIA 接近；
- 支持 PMD/PMX/VMD/VPD 播放；
- 适合比较对象创建、姿态更新顺序和 C++ 数据组织。

### nanoem

仓库：`https://github.com/hkrn/nanoem`

用途：

- MMD-compatible 跨平台应用；
- 适合参考完整编辑/播放应用中的模块边界、模型生命周期和兼容性测试思路。

### blender_mmd_tools

仓库：`https://github.com/MMD-Blender/blender_mmd_tools`

用途：

- 作为模型制作和修复侧参考；
- 用于检查 collision group/mask、刚体尺寸、关节与骨骼配置；
- 不作为运行时数值真值，因为 Blender 的 Bullet/约束行为与 MMD 存在已知差异。

### Bullet 官方源码

仓库：`https://github.com/bulletphysics/bullet3`

用途：

- 查证 Bullet API、solver、constraint、CCD 和 collision filter 的真实语义；
- MMD 兼容行为仍需以上层 MMD runtime 为准。

## Phase 0：建立对照实验框架

### 0.1 冻结测试资产

至少选择三类合法、授权可测试的模型：

1. 简单标准模型；
2. 当前复杂裙摆/长发/尾巴模型；
3. Mode 1 / Mode 2 混合明显的模型。

为每个模型固定：

- PMX 文件 hash；
- VMD 文件 hash；
- 初始 transform；
- 物理固定步；
- 比较时间点；
- Bullet 开关与 reset 时机。

### 0.2 统一导出轨迹

各实现尽可能导出：

```text
frame/tick
body world position + rotation
bone world position + rotation
constraint violation
contact pair（若实现可取）
```

WISTERIA 增加 JSON/CSV trace，而不是只依靠截图。

### 0.3 建立指标

```text
body position RMS / max
body rotation RMS / max
bone position RMS / max
bone rotation RMS / max
父子骨骼长度变化
Reset 后第一帧偏差
长时间漂移
```

视觉差异仍保留，但不再作为唯一证据。

## Phase 1：`MMD_COMPAT` 基线

新增明确的兼容 profile，默认关闭：

- recovery；
- adaptive CCD；
- chain gravity profile；
- decorative fallback；
- near-neighbor/skirt semantic filtering；
- FULL_BODY / TRANSLATION_DELTA 自动使用；
- 单模型 override。

逐项审计：

### 1.1 单位与重力

确认：

- PMX 长度到 WISTERIA world length 的比例；
- body shape、joint linear limit、model transform 是否同尺度；
- 社区实现采用的 MMD 重力及其单位理由；
- 当前 `-9.8` 是否已经通过模型缩放等价转换。

在单位审计完成前，不直接把重力改为 `-98`。

### 1.2 rigid body mode

确认：

- FollowBone 的 Kinematic 更新时机；
- Physics 的 full transform 回写；
- PhysicsWithBone 的标准平移/旋转组合；
- 是否存在父子关系触发的 Mode 2 quirks；
- 是否需要 strict bone-length 选项。

### 1.3 constraint 构造

确认：

- frameA/frameB 计算；
- Euler 顺序和角限制；
- spring stiffness/damping 的 Bullet 版本差异；
- 零范围/极小角限制的社区钳制；
- 相连刚体碰撞是否由 group/mask 决定。

### 1.4 reset 与 warmup

确认：

- 模型加载时 body 初始 transform；
- VMD seek/restart/physics toggle 的重置顺序；
- 是否需要空白预演算帧；
- 速度和 activation state 的清理；
- Kinematic body 在第一物理步前的更新。

### 1.5 damping 与时间步

确认 PMX damping 是直接传入，还是需要按固定步转换。对照必须在相同 fixed step 下进行。

## Phase 2：`MMD_COMMUNITY_QUIRKS`

只有同时满足以下条件的规则才进入该层：

1. 可指出参考实现和具体源码位置；
2. 有模型回归证明；
3. 可以独立开关；
4. 不依赖模型文件名或 runtime chain 编号；
5. 有许可证审计记录。

候选项：

- strict bone-length；
- Mode 2 父子关系修正；
- 极小角限制钳制；
- damping/spring 时间步换算；
- MMD 风格 reset/physics toggle；
- linked-body collision 行为；
- Bullet 版本兼容补偿。

## Phase 3：`WISTERIA_ADAPTIVE`

保留当前成果，但默认作为可选增强：

- 局部 recovery 与 fuse；
- 锚点相对 runaway；
- adaptive CCD；
- chain gravity/damping；
- decorative fallback；
- 语义碰撞过滤；
- 接触、约束和蒙皮诊断。

每项增强必须能与 `MMD_COMPAT` 做 A/B，并在日志中输出当前启用来源。

## Phase 4：外部模型 profile

建议后续文件结构：

```json
{
  "schema": 1,
  "model_sha256": "...",
  "base_profile": "mmd-community-compat-v1",
  "adaptive": {
    "recovery": false,
    "ccd": true
  },
  "chain_overrides": [],
  "collision_overrides": [],
  "body_overrides": []
}
```

设计原则：

- 以模型 hash 定位，不以文件名定位；
- 不修改原 PMX；
- override 使用刚体/骨骼稳定标识，并检查歧义；
- 配置加载失败时回退到 base profile；
- 所有 override 在启动日志中可见；
- 提供 dry-run，只报告将要修改的内容。

## 推荐代码结构

兼容行为冻结后，再拆分当前大文件：

```text
mmd/physics/
├─ mmd_physics_asset.*
├─ mmd_physics_instance.*
├─ mmd_physics_policy.*
├─ mmd_physics_compat.*
├─ mmd_physics_sync.*
├─ mmd_physics_reset.*
├─ mmd_physics_diagnostics.*
├─ mmd_physics_adaptive.*
└─ mmd_physics_profile_loader.*
```

建议未来引入两个不同概念：

```text
MmdPhysicsCompatibilityProfile
标准/社区兼容行为和 world preset

MmdPhysicsRuntimePolicy
WISTERIA adaptive 与模型 override
```

不要把两者继续合并成一个“万能参数结构”。

## 首个实施里程碑

下一阶段建议只做：

1. `MMD_COMPAT` 开关，关闭所有 adaptive 行为；
2. trace 导出；
3. 单位/重力审计；
4. linked-body collision 审计；
5. Mode 2 + strict bone-length 对照；
6. reset/warmup 对照；
7. 形成第一份 babylon-mmd / libmmd / Saba 差异矩阵。

在差异矩阵完成前，不再新增裙摆名称规则或全局调参。

## P0 实验状态（2026-08-03 更新）

差异矩阵已核实（three.js / Saba / babylon-mmd / MikuMikuPhysics），结论写入：

- [P0 Bullet 2.75 约束兼容实验](./MMD_PHYSICS_P0_BULLET275_COMPAT.md)

当前 P0 第一轮已完成（2026-08-03）：

- vendor Bullet 3.25 打 2.75 兼容补丁；
- `PhysicsWorld` 增加 legacy `btGeneric6DofSpringConstraint` 路径；
- `MmdPhysicsRuntimePolicy` 增加 `MmdCompatDefaults()`；
- 叶瞬光 720 帧 A/B 回归。

第一轮结果：legacy 约束在 adaptive 下降低线性违规（0.916 → 0.723）和严重关节数
（4 → 2），raw 下降低角向违规（45.2° → 26.0°）；linked-body 碰撞开启则显著变差，
已从 `MmdCompatDefaults()` 中排除，留待 P1。完整数据见
[P0 Bullet 2.75 约束兼容实验](./MMD_PHYSICS_P0_BULLET275_COMPAT.md)。
