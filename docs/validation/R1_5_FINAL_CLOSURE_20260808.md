# R1.5 — Second Dynamic Runtime Validation Final Closure（2026-08-08）

> 状态：**FROZEN / IMPLEMENTED / VALIDATED / CLOSED（待最终审查盖章）**。
> 契约：`docs/architecture/R1_5_SECOND_DYNAMIC_RUNTIME_CONTRACT.md`
> （CONTRACT FROZEN）。

## 1. 一句话

R1.5 证明了 `IModelRuntimeDriver / ModelInstance / ModelBackendRegistry`
不是 Saba 特制：WISTERIA 现在实际拥有两套性质不同的动态 Runtime
（Saba MMD、Wisteria Generic）和一条 Static no-runtime 路径，上层统一
通过 `ModelAsset → Registry → ModelInstance → Entity → Renderer/Snapshot`
治理，不再需要知道模型由谁执行。

## 2. 最终语义冻结：Generic Reset = B

审查拍板（B 语义）：

```text
Generic Reset() 恢复该 Runtime 的默认启动 playback state：

有默认 clip  → AnimationClipAt(0)、t=0、playing
无 clip      → bind pose、stopped
morph-only   → 初始 0 权重
pending RootMotion → identity

Reset 不得重新分配 Pose/MorphState/Animator 对象；
不承诺恢复 Speed / Looping / RootMotionBone / RootMotionEnabled 等
宿主配置的 Animator 选项（不扩张 factory-default reset）。
```

实现（同一对象上恢复，`Animator::Stop(true)` 已回 bind/清 morph，
不再二次 reset，避免多余 Pose revision bump）：

```cpp
if (animator) {
    animator->Stop(true);
    if (asset->AnimationClipCount() > 0)
        animator->Play(asset->AnimationClipAt(0));
} else {
    if (pose) pose->ResetToBindPose();
    if (morphState) morphState->Reset();
}
pendingRootMotion = {};
```

永久回归 `TestWisteriaGenericReset`：

```text
fresh:            CurrentClip == clip0、Time == 0、IsPlaying == true
Update(0.5):      Time == 0.5
Reset:
                  CurrentClip == clip0、Time == 0、IsPlaying == true
                  ConsumeRootMotion == identity
                  TryGetPose / TryGetAnimator 地址不变
                  ModelInstance snapshot invalid
Update(0):        snapshot valid
                  Reset 后 Pose == 新 Runtime Update(0) 后 Pose
morph-only:       weight 0.8 → Reset → 0
```

## 3. 最终静态审计（全仓搜索）

```text
src/rendering + include/wisteria/rendering
  包含 SabaMmdRuntimeModel / WisteriaGenericRuntimeDriver
  → 0 处（Renderer 不感知 backend）

src 下 backendDriven
  → 0 处（Scene 双轨已删除）

BackendKind() 读取 sourceDescriptor->backend
  → 0 处（ModelAsset::backendKind 是唯一权威）
```

`ModelSourceDescriptor.backend` 的准确定位：
**legacy informational metadata / compatibility residue**；真正 source
identity 是 `sourcePath`。字段保留但不参与任何 Runtime 选择。

## 4. 三路最终验证

```text
Static（box.glb）
  → Static → ModelInstance → Runtime == nullptr
  → RenderPart/MorphMaterialIndex 原样保留，不伪造动态状态

Generic（animated_triangle.gltf）
  → 导入结果含 Skeleton+Clip → WisteriaGeneric
  → Scene::InstantiateModel 自然得到 wisteria-generic Runtime
  → 默认播放 clip0、Pose/Snapshot 与旧路径等价

PMX（extended-morph / production 叶瞬光等）
  → SabaMmd → saba-mmd Runtime
  → 多实例独立 Pose/geometry/physics
  → Stable C ABI 跨进程 checkpoint FULL E2E 字节等价
```

## 5. 回归矩阵（2026-08-08 实测）

```text
Phase 0B procedural canary（vertex / one-bone / root-motion /
                       malformed / legacy-suppression）   全部 PASS
Phase 0C generic（equivalence / morph / root-motion /
                multi-instance）                          全部 PASS
Phase 0E reset semantics                                  PASS
Static / PMX 路由回归                                     PASS
R1.2–R1.4 deterministic / checkpoint / Stable ABI FULL     PASS

Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）。

## 6. R1.5 正式停止开发

R1.5 到此冻结，不再增加功能。后续阶段（R1.6 及以后）只处理
`R1_5_SECOND_DYNAMIC_RUNTIME_CONTRACT.md` §10 Debt Ledger 中记录的
既有债务，不扩张 R1.5 语义。
