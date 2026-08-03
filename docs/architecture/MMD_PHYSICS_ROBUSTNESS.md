# MMD 链路鲁棒性清单

## 目的

阶段 2（Saba importer + Saba 蒙皮渲染）过程中踩过的坑，必须固化为自动防线，
避免换模型、换环境、换编译器时再次出现。

## 问题清单与防线

| # | 问题 / 症状 | 根因 | 防线 |
|---|---|---|---|
| 1 | 中文路径抛 `No mapping for the Unicode character...` | MSVC `std::filesystem::path` 用窄字符串走 ACP | 统一 `PathFromUtf8` / `ToNarrowUtf8`；禁止 `path.string()` 拼异常消息 |
| 2 | 模板 C4267 警告刷屏 | `optional<uint32_t>` 被赋 `size_t` | 显式 `static_cast`；编译输出零 C4267 |
| 3 | `GLAD ERROR 1282 in glBufferSubData/glBindBuffer` | Scene::Update 阶段没有激活 GL context 就改 VBO | 顶点上传只允许在 Renderer 绘制回调里执行（`MeshDynamicVertexProvider`） |
| 4 | 模型部分部位用 bind 顶点、部分用蒙皮顶点 | 24 个子网格 VBO 都含全量顶点，只上传了第 1 个 | demo 遍历 `model.Parts()` 给每个 mesh 设置 provider |
| 5 | 全部三角形被剔除 / 模型“拉了一地” | Saba 镜像 Z 后反转面绕序（v2,v1,v0），importer 用原始顺序 | importer 索引按 v2,v1,v0 写入；`[SABA INDEX] mismatches=0` 回归 |
| 6 | 脸部被拉到地上（仅动态上传后出现） | VBO 是交错布局，却把 position/normal 当连续块写入，覆盖整个 buffer | `Mesh::RebuildInterleavedVertices` 纯函数重建交错数组 + 单测 |
| 7 | 物理/动画基准不一致导致疯狂运动 | 漏调 `InitializeAnimation()`（Saba viewer 加载后必调） | runtime Initialize 顺序：Load → InitializeAnimation → VMD |
| 8 | `dynamic=0`（CPU 蒙皮未生效） | provider 调用条件依赖“已上传”标记，形成死锁 | 只要设置 provider 就每帧上传，不再依赖标记 |

## 自动防线（测试）

| 测试 | 覆盖 |
|---|---|
| `TestMeshDynamicUpload` | 交错重建纯函数：position/normal 槽位正确、其它属性不变、尺寸校验 |
| `TestSabaSkinningWhenAvailable` | 10 秒长跑：顶点 finite/范围/位移；bind 与 importer 一致；索引逐段一致 |
| `TestSabaImporterAcrossModelsWhenAvailable` | 多模型（叶瞬光/今汐/凑企鹅/爱弥斯…）Saba vs Assimp 元数据对照 |
| `TestSabaImporterPMXDataComparison` | 叶瞬光全字段对照 + 小资产刚体/关节数 |
| `TestInterfaceCompilation` | 新接口可实例化、默认行为正确 |

## 换模型 / 换环境检查清单

1. 模型 PMX 用 Saba importer 导入：刚体/关节/材质/morph 与 Assimp 对照一致；
2. 顶点 bind 对照 mismatches=0（顺序/坐标）；
3. 每个子网格索引与 Saba runtime 逐段一致（含 winding）；
4. 运行 demo：无 GLAD 1282、`[SKINNING]` 中 Saba 窗口 `dynamic=1`；
5. 运行 ≥10 秒：`[SABA SKIN]` 顶点范围稳定、`maxBindDisplacement` 不随时间爆炸；
6. 中文路径 / 非 UTF-8 环境：导入和纹理加载无 ACP 异常；
7. 全量测试 `wisteria_tests.exe` PASS=全部、FAIL=0。

## 诊断开关（保留，供排查）

- `WISTERIA_SABA_NO_UPDATE=1`：Saba 窗口不上传蒙皮顶点（保持 bind），
  用于区分“数据/上传”与“渲染/变换”问题。

## 环境约定

- `std::filesystem::path` 与 UTF-8 字符串互转必须走 `PathFromUtf8`/`ToNarrowUtf8`；
- 任何 GL 调用不得出现在 Behaviour/Scene::Update 阶段，只允许在 Renderer
  的窗口 context 内执行；
- 交错顶点缓冲更新必须整块重建，禁止按属性连续 `glBufferSubData`。
