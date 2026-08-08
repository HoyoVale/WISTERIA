# R1.6 — Deterministic Offline Output Pipeline Final Closure（2026-08-09）

> 状态：**FROZEN / IMPLEMENTED / VALIDATED / CLOSED（待最终盖章）**。
> 基线：R1.6 Phase 0A–0E CLOSED（`2ce1f59`）。

## 1. 一句话

WISTERIA 已经从模型资产、动态 Runtime、动画/物理、presentation、
Renderer 一路统一到**可复现的最终像素序列输出**：

```text
PMX/VMD → Saba Runtime ─────┐
                            │
glTF → Generic Runtime ─────┼→ ModelInstance
                            │
Static ─────────────────────┘
                                    ↓
                          ModelRenderFrameView
                                    ↓
                           Entity / Scene
                                    ↓
                 Camera + Projection + Scene Lights
                                    ↓
                                 Renderer
                                    ↓
                             SceneFramebuffer
                                    ↓
                               ReadbackRgba8
                                    ↓
                    Deterministic Frame Sequence (PNG/JSONL/checkpoint)
```

## 2. Phase 汇总

```text
0A  Output Contract            FROZEN ✅
0B  SceneFramebuffer → RGBA8   CLOSED ✅
0C  Runtime → Unified Render State
    Saba UV / Material bridge  CLOSED ✅（含 post-closure UV 约定修复）
0D  Explicit Presentation
    Authority                 CLOSED ✅
0E  Deterministic Frame
    Sequence                  CLOSED ✅
```

## 3. 0E 关键能力（最终形态）

```text
OfflineFrameSequence：
  from-start RenderRange(start,end) 顺序 pre-roll（frame <= 2^24）
  Resume：持久化 checkpoint wire + hash 校验 + restore
  JSONL append-only manifest（crash-tail in-place truncate）
  durable 原子事务（fsync/_commit、原子 replace、目录 fsync）
  Reject / Overwrite / VerifySkip（committed-record 权威 + artifact hash）
  A/B checkpoint 交替（last committed slot 的 opposite）
  fail-stop 全覆盖
  Session identity（presentation 输入 + scenePresentationIdentity，
    路径无关）

PublishCurrentRuntimeFrame：
  exact step/restore 后只发布不推进（不 Update、不 root motion、
  不增 updateSerial）

PNG encoder utility：top-left RGBA8 → PNG，无 Renderer 依赖
BMP 截图：canonical top-left → positive-height bottom-up BMP（方向修正）
```

## 4. 修复记录（人工检查驱动）

```text
0C post-closure：Saba GetUpdateUVs 为 V-up，WISTERIA 约定 V-down；
  adapter 翻回（demo 贴图垂直翻转修复，哈希级验证）
R1.6 截图：SaveWindowScreenshotBmp 行序反了（BMP bottom-up 与
  glReadPixels 匹配）；抽成 WriteBmp24 utility + 定向回归
```

## 5. 四套矩阵（2026-08-09 实测，多轮）

```text
Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）；Mesa D3D12 LLVM 双注册为已知环境问题。

## 6. R1.6 边界（冻结）

```text
Exact State ≠ Cross-platform Pixel Exact（同环境 byte-identical）
Offscreen ≠ Headless（R1.7）
Stable Render C Portal：未冻结（输出链正确后再议）
GraphicsDevice share-group identity：R1.7
PNG encoder 为 utility；Renderer 0 个 backend/sequence/checkpoint 概念
```

## 7. 后续横向方向

```text
A. Stable C Portal（Asset/Runtime/Scene/RenderTarget/Pixel Output）
B. MMD Advanced（物理 tuning / UV-material morph 补全 / fidelity）
C. 更多 Backend（VRM 等）
```

## 8. 冻结声明

R1.6 至此停止开发，不再往 0A–0E 增加功能；除非出现具体反例证据，
不再扩大反例空间。R1.7（Headless Context Provider / Platform
Lifetime）另行立项。
