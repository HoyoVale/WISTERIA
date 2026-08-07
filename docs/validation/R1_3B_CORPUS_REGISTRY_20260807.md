# R1.3B Phase 0B — 对照 Corpus Registry（2026-08-07 冻结）

> 契约：`docs/architecture/R1_3B_MMD_COMMUNITY_COMPARISON_CONTRACT.md`
> §4.2 / §12 Step 8。机器可读副本：`tools/reference_trace/corpus.json`
> （本 Markdown 为人工评审 Source of Truth）。
>
> **资产合规（Frozen Decision 7）**：公开文档只保留 alias 与 hash；
> FULL_ASSETS 真实名称与路径为 PRIVATE（仅本地）。真实名称曾进入
> Git 历史，历史清理属单独决策；本文件只保证 HEAD 合规。

## 身份与运行约定（全部资产统一）

```text
corpus identity：
  sha256      跨实现 canonical asset identity（reference harness 输出）
  fnv1a64     WISTERIA R1.2C 内部 deterministic asset identity
              （FNV-1a 64 over raw file bytes，与 HashFileBytes 一致）

localPath：PRIVATE（FULL_ASSETS，仅本地归档）
initialTransform：identity（position (0,0,0)、rotation identity、scale 1），
  两侧均不施加 root motion
comparisonPoints：motionFrame 0 / 10 / 30 / 100 / 300
  （长跑可加 720 / 1200 = 24s / 40s）
environmentMode：NormalizedComparison
gravity：-98（10:1 尺度）
fixedTimeStep：1/120
groundPolicy（reference 侧）：synthetic-ground-box-v1
executionProfile：
  reference  = reference-continuous-120hz-v1
  WISTERIA   = deterministic-cold-step-v1
  （executionProfile 不同 → 只能作观察证据，不能声称因果）
坐标：canonicalTarget = WISTERIA；
  babylonRigidBodyMapping = Identity（契约 §5，已核验）；
  fallback = ZReflectionNormalizationV1（证据需要时）
```

## 资产清单（alias 公开，身份 hash 公开）

### corpus-asset-01（Class 1：简单标准模型，Mode 1 为主）

```text
pmx sha256：a3b7105dc3cfef22540e7aa77e6f0497453541f600f9002bc98c6c7cd2db7680
pmx fnv1a64：b4cecdc35c9c7926
rigid bodies：9（FollowBone 1 / Physics 8 / PhysicsWithBone 0）
dynamic：8，mass 1.00 – 1.00

motion：motion-01
vmd sha256：cb35fd4a6194a35ab46061ce8de13d5371d91dff6b81471e3dd1733490482395
vmd fnv1a64：14145a55cbb3840d
verified：motionFrame 0/1/10（displacement 3.71→3.45）与 300（tick 1200 = 10s）
```

### corpus-asset-02（Class 2：复杂裙摆/长发，Mode 2 密集）

```text
pmx sha256：208e7484db2e495010a1dbe103bd2f76a903356a43f1a1b6b9b3bf42b06738f3
pmx fnv1a64：b53b21f696c7335f
rigid bodies：495（FollowBone 38 / Physics 74 / PhysicsWithBone 383）
dynamic：457，mass 0.01 – 218.31

motion：motion-02
vmd sha256：d7b19886bb28830274c278739975edda89575fccbc1f2c54ddfd7bb966e05c20
vmd fnv1a64：3aa39fe68e622f20
verified：motionFrame 0/1/10（displacement 3.58→7.85）与 300（tick 1200 = 10s）
```

### corpus-asset-03（Class 3：Mode 1/2 混合 + 关节/IK）

```text
pmx sha256：f1f503ed7a0a4922fc16e6f28bff6b4130b02202ece1cbedae663c6cbca6679a
pmx fnv1a64：369e5cfa96731cfa
rigid bodies：633（FollowBone 89 / Physics 269 / PhysicsWithBone 275）
dynamic：544，mass 0.01 – 4146.89

motion：motion-03
vmd sha256：4dddf29105a1bdeae2e5cc3eb001d8bb7c2cde880fa2a0fac72fd145e53c7b6e
vmd fnv1a64：e5b56b2d1c86d690
verified：motionFrame 0/1/10（displacement 8.18→10.98）与 300（tick 1200 = 10s）
```

## 未入选候选（记录原因，不公开名称）

```text
候选-01（internal fixture id：production-pmx-suibian）：rigid body count 0
候选-02（production-pmx-yixuan）：269（37/82/150），Class 3 备选
候选-03（production-pmx-aimisi）：695（40/643/12），Mode 1 单极
```

## 冻结状态

```text
Corpus Freeze：2026-08-07
后续任何资产替换必须更新本 registry 与 corpus.json，
并重新跑 0/1/10/300 smoke 验证。
```
