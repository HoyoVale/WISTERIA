# R2.0 — Backend-neutral Render Architecture Final Closure（2026-08-12）

> 状态：**REVISED（2026-08-12 ChatGPT 横向审查拒绝首版 closure；
> 5 个架构 blocker 已在 R2_0_FINAL_ARCHITECTURE_CLOSURE_20260812.md
> 全部修复并重新验证；最终状态以该文档为准）**

## 阶段状态

```text
R2.0 0A  Contract + Boundary Audit          CLOSED ✅
R2.0 0B  RenderDevice Foundation            CLOSED ✅
R2.0 0C  CPU asset / GPU realization split  CLOSED ✅
R2.0 0D  RenderFramePacket + RenderGraph    CLOSED ✅
          Stage 1  packet extraction        CLOSED ✅
          Stage 2A RenderGraph core         CLOSED ✅
          Stage 2B registration + execution CLOSED ✅
          Stage 2C pass executors           CLOSED ✅
R2.0 0E  PresentSurface split               CLOSED ✅

R2.0     Backend-neutral Render Architecture CLOSED ✅
```

## 最终架构

```text
Runtime / Scene
      ↓
RenderFramePacket（frame-lifetime CPU authority）
      ↓
BuildCurrentRenderGraph（pass existence + resource semantics）
      ↓
RenderGraph::Execute（ordering + hazard + preflight authority）
      ↓
Execute* pass executors（OpenGL implementation layer）
      ↓
SceneColor（offline 输出边界，Present/FXAA 之前）
      ↓
PresentSurface（窗口呈现端点：Present + Swap）
      ↓
Window（platform native window）

OffscreenRenderSession 无 PresentSurface 要求。
```

## 最终 gates

```text
Gate A0/A   PASS  backend-neutral public headers 0 GL
Gate B0/B   PASS  GL 仅存在于 OpenGL backend / renderer /
                  approved platform bridge
Gate C      PASS  Runtime/Scene/ModelAsset/checkpoint 零 RenderDevice
Gate D      PASS  94 legacy + 30 stable exports
Gate E      PASS  wisteria_stable_runtime.h / _render.h 零改动
```

## 最终验证

```text
Windows CORE   12/12 PASS
Windows FULL   13/13 PASS
WSL CORE       14/14 PASS
WSL FULL       15/15 PASS
ABI            94 legacy + 30 stable

像素证据：
  stable ABI render == engine render（byte-identical）
  graph 路径重复帧 byte-identical
  full graph RGB 内容 + OIT fallback RGB 内容
  OIT + PhysicsDebug SceneColor A/B adversarial（含 mutation 反证）
  四矩阵全部像素/IBL/headless 回归绿色
```

## Known-debt ledger（转 R2.1）

```text
1. RenderDevice 驱动的 pass execution / Vulkan executor → R2.1
2. 无 historical golden byte comparison（不阻塞；证据链已足够）
3. HasRenderedRgb 黑色 clear 假设（非阻塞）
4. Texture facade 复制 TextureData（未来 shared immutable payload）
5. EnvironmentHdrImage 可变 shared_ptr payload（watchpoint）
6. GitHub 无可见 CI status（本地验证证据）
```

## 下一步

```text
R2.1  Vulkan device/resource/pipeline backend
R2.2  Vulkan feature parity + window/headless + cross-backend validation
```
