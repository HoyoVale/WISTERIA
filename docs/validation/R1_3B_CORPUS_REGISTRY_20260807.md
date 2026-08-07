# R1.3B Phase 0B — 对照 Corpus Registry（2026-08-07 冻结）

> 契约：`docs/architecture/R1_3B_MMD_COMMUNITY_COMPARISON_CONTRACT.md`
> §4.2 / §12 Step 8。机器可读副本：`tools/reference_trace/corpus.json`
> （本 Markdown 为人工评审 Source of Truth）。

## 身份与运行约定（全部资产统一）

```text
corpus identity：
  sha256      跨实现 canonical asset identity（reference harness 输出）
  fnv1a64     WISTERIA R1.2C 内部 deterministic asset identity
              （FNV-1a 64 over raw file bytes，与 HashFileBytes 一致）

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
  （executionProfile 不同 → 只能作观察证据，不能直接声称因果）
坐标：ReferenceCoordinateNormalization v1（契约 §5）
```

## 资产清单

### corpus-asset-01：凑企鹅（Class 1：简单标准模型，Mode 1 为主）

```text
path：assets/models/mmd/凑企鹅/凑企鹅.pmx
pmx sha256：a3b7105dc3cfef22540e7aa77e6f0497453541f600f9002bc98c6c7cd2db7680
pmx fnv1a64：b4cecdc35c9c7926
rigid bodies：9（FollowBone 1 / Physics 8 / PhysicsWithBone 0）
dynamic：8，mass 1.00 – 1.00

motion：assets/models/mmd/凑企鹅/penguin_walking.vmd
vmd sha256：cb35fd4a6194a35ab46061ce8de13d5371d91dff6b81471e3dd1733490482395
vmd fnv1a64：14145a55cbb3840d
verified：motionFrame 0/1/10（displacement 3.71→3.45）与 300（tick 1200 = 10s）
```

### corpus-asset-02：叶瞬光（Class 2：复杂裙摆/长发，Mode 2 密集）

```text
path：assets/models/mmd/叶瞬光_pmx/叶瞬光.pmx
pmx sha256：208e7484db2e495010a1dbe103bd2f76a903356a43f1a1b6b9b3bf42b06738f3
pmx fnv1a64：b53b21f696c7335f
rigid bodies：495（FollowBone 38 / Physics 74 / PhysicsWithBone 383）
dynamic：457，mass 0.01 – 218.31

motion：assets/motions/皮卡皮卡皮卡丘+/身体动作.vmd
vmd sha256：d7b19886bb28830274c278739975edda89575fccbc1f2c54ddfd7bb966e05c20
vmd fnv1a64：3aa39fe68e622f20
verified：motionFrame 0/1/10（displacement 3.58→7.85）与 300（tick 1200 = 10s）
```

### corpus-asset-03：蕾米埃尔-白（Class 3：Mode 1/2 混合 + 关节/IK）

```text
path：assets/models/mmd/蕾米埃尔-白/蕾米埃尔-白.pmx
pmx sha256：f1f503ed7a0a4922fc16e6f28bff6b4130b02202ece1cbedae663c6cbca6679a
pmx fnv1a64：369e5cfa96731cfa
rigid bodies：633（FollowBone 89 / Physics 269 / PhysicsWithBone 275）
dynamic：544，mass 0.01 – 4146.89

motion：assets/motions/梦的翅膀/梦的翅膀motion.vmd
vmd sha256：4dddf29105a1bdeae2e5cc3eb001d8bb7c2cde880fa2a0fac72fd145e53c7b6e
vmd fnv1a64：e5b56b2d1c86d690
verified：motionFrame 0/1/10（displacement 8.18→10.98）与 300（tick 1200 = 10s）
```

## 未入选候选（记录原因）

```text
随便观（suibian）：rigid body count 0，无物理内容，不满足对照需求
仪玄（yixuan）：269（37/82/150），可作为 Class 3 备选，本轮不启用
爱弥斯（aimisi）：695（40/643/12），Mode 1 单极，不覆盖 Mode 2 对照需求
```

## 冻结状态

```text
Corpus Freeze：2026-08-07
后续任何资产替换/路径变更必须更新本 registry 与 corpus.json，
并重新跑 0/1/10/300 smoke 验证。
```
