# WISTERIA 工程目录与依赖规则

## 目录职责

| 模块 | 职责 | 允许依赖 |
|---|---|---|
| `core` | Transform、Timer、Root Motion 等基础运行时 | GLM、标准库 |
| `animation` | Skeleton、Pose、Animator、Morph | `core` |
| `physics` | Bullet 世界、通用 body/constraint API、handle | `core`，Bullet |
| `mmd` | PMX/VMD 专用姿态与格式语义 | `animation`、`physics`、`core` |
| `assets` | 导入与不可变资源组织 | `animation`、`mmd`、`rendering` |
| `rendering` | OpenGL 资源与渲染 | `core`、`animation` |
| `scene` | Entity、Scene、Behaviour、生命周期编排 | 上述公共接口 |
| `platform` | Window、Input、Application | `scene`、`rendering` |
| `common` | PCH 和小型公共实现 | 不承载领域逻辑 |
| `vendor` | 单头第三方库 | 禁止放项目逻辑 |

## 依赖方向

```text
platform
   ↓
scene ───────────────┐
 ↓       ↓           ↓
assets  rendering  PhysicsInstance
 ↓                   ↓
mmd ───────────── PhysicsWorld
 ↓                   ↓
animation ──────── core
```

关键规则：

1. `physics/` 不得 include `mmd/`；
2. `PhysicsWorld` 不得出现 Bone、PMX、VMD、Skirt、Hair 等概念；
3. `scene/` 的模拟循环只调用 `PhysicsInstance`；
4. MMD typed access 仅作为过渡调试接口；
5. WISTERIA adaptive 参数只能从 `mmd/physics` 或更高层进入；
6. 模型 profile 不得修改 `MmdPhysicsAsset` 原始数据，应生成运行时 override；
7. 第三方项目实现的行为应记录来源、commit、许可证和本地回归证据。

## 命名与 include 规范

```cpp
#include "wisteria/physics/physics_world.hpp"
#include "wisteria/mmd/physics/mmd_physics_policy.hpp"
#include "wisteria/scene/entity.hpp"
```

禁止重新引入：

```cpp
#include "physics_world.hpp"
#include "entity.hpp"
```

CMake 源文件按模块变量列出，新增文件必须进入对应模块，而不是继续追加到无分类长列表。
