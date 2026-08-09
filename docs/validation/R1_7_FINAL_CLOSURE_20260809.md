# R1.7 — True Headless Context Provider Final Closure（2026-08-09）

> 状态：**FROZEN / IMPLEMENTED / VALIDATED / 0A–0D CLOSED；
> 0E FINAL CLOSURE PENDING（native-Linux 硬件 release gate）**。
> 契约：`docs/architecture/R1_7_HEADLESS_CONTEXT_CONTRACT.md`。
> 四矩阵（2026-08-09 实测）：
> Windows CORE 8/8、Windows FULL 9/9、Linux CORE 9/9、Linux FULL 10/10。
> 注：Linux 两套矩阵当前为 WSL + llvmpipe（兼容性记录）；
> 真实 Linux 硬件 EGL 发布基线待真机执行
> `script/verify_r17_native_linux.sh` 后并入。

## 1. 一句话

WISTERIA 已经可以从头到尾**不依赖任何 Window System** 完成离线渲染：
引擎自己创建 EGL context，`HeadlessRenderSession` 零窗口组合根
跑通 `Scene → Renderer → SceneFramebuffer → RGBA8` 与确定性
`OfflineFrameSequence`（PNG + manifest + A/B checkpoint）。

## 2. Phase 汇总

```text
0A  契约与四项决策                     FROZEN ✅
0B  HeadlessContext + EGL Provider     CLOSED ✅
     （surfaceless → device-hw → device-sw，pbuffer 兜底，
       strict forceSoftware gate）
0C  双身份 ownership 模型              CLOSED ✅
     （GraphicsContextToken +
       GraphicsShareGroupToken，
       shared / context-local 分域删除）
0D  HeadlessRenderSession              CLOSED ✅
     （零窗口 RenderOffline +
       OfflineFrameSequence +
       compatibility probe）
0E  四矩阵验证 + Final Closure         ⏳ PENDING
     （Windows/WSL 矩阵已绿；
       native-Linux 硬件 release gate 待真机执行）
```

## 3. 最终架构

```text
CreateHeadlessContext(options)
  ├─ EGL surfaceless（Mesa）        ← 主路径
  ├─ EGL device-hardware           ← 回退
  └─ EGL device-software           ← forceSoftware / 兜底
        ↓
   IHeadlessContext
  （ContextToken + ShareGroupToken）
        ↓
HeadlessRenderSession
  ├─ GraphicsDevice（双身份删除队列）
  ├─ ResourceManager
  └─ Renderer
        ↓
RenderOffline / OfflineFrameSequence
        ↓
RGBA8 / PNG / manifest / checkpoint
```

关键不变量（全部冻结并有测试固定）：

```text
1. CreateHeadlessContext 返回后 native context 不保持 current
2. MakeCurrent = native current → ContextToken → ShareGroupToken
   → FlushPendingDeletes（单一事务）
3. shared object 按 ShareGroupToken 判断删除合法性；
   context-local object（VAO/FBO）按 owning ContextToken 判断，
   兄弟 context 不得 flush
4. 销毁 native context 后两个 tracker 必须为空
5. forceSoftware 必须验证 GL_RENDERER（llvmpipe/softpipe/swrast），
   D3D12 一律 FAIL
6. ReleaseAll ownership 校验失败 → fail-stop，不继续 GL 删除
```

## 4. 矩阵（2026-08-09 实测）

```text
Windows 回归矩阵（MSVC RelWithDebInfo）：
  CORE 8/8 Passed
  FULL（完整资产）9/9 Passed

WSL 兼容性矩阵（GCC，LIBGL_ALWAYS_SOFTWARE=1 → llvmpipe）：
  CORE 9/9 Passed（含 headless-smoke）
  FULL（完整资产）10/10 Passed

Native Linux 硬件发布基线（真实 Linux 机器，硬件 EGL）：
  ⏳ PENDING —— 至少 headless-smoke（session + sequence probe）
  必须 PASS；推荐 CORE/FULL 全量。
  执行：./script/verify_r17_native_linux.sh [--core|--full]
```

WSL 使用 `LIBGL_ALWAYS_SOFTWARE=1`（llvmpipe），符合
"WSL Mesa D3D12 不可靠、必须软件回退"的兼容性口径。Windows 无 EGL
provider，headless-smoke 只在 Linux 矩阵出现；Windows 保持 GLFW
hidden 回归基线。

测试清单：

```text
CORE：unit / runtime / integration / render-fbo / abi-safety-matrix /
      abi-c-smoke / checkpoint-cross-process /
      stable-checkpoint-cross-process
Linux 追加：headless-smoke（EGL lifecycle + FBO + session + sequence）
FULL 追加：stable-checkpoint-cross-process-full（生产 PMX/VMD）
```

## 5. 冻结边界（R1.7 不做）

```text
Windows WGL PBuffer / ANGLE 真 headless（保持 GLFW hidden）
OSMesa provider（v2/按需）
Stable Render C Portal
Application 零窗口模式
多线程渲染 / 多 context 并发
视频编码 / FFmpeg / Audio
跨平台 / 跨渲染器 pixel hash 一致
```

## 6. 已知兼容性记录

```text
WSLg Mesa D3D12：FBO/SceneFramebuffer 路径可用（session probe PASS），
默认 back buffer 读回仍为已知平台问题；WSL 实验统一使用
LIBGL_ALWAYS_SOFTWARE=1 → llvmpipe（GL 4.5 Core）。
```

## 7. 后续方向（R1.7 之后）

```text
R1.8  Generic Deterministic Runtime
      把 Saba 的 exact-step/checkpoint 能力升格为 WISTERIA 通用能力
R1.9  Stable Runtime / Render C ABI
R2.x  RenderDevice / RenderTarget / RenderGraph / 多后端
```

## 8. 冻结声明

R1.7 Phase 0A–0D 已停止开发。0E 在 native-Linux 硬件 release gate
通过后正式 CLOSED；在此之前不扩大反例空间，后续工作必须消费已
冻结的 ownership 模型、factory 不变量与 fail-stop teardown 语义。

## 9. Closure Fix（2026-08-09 终审后）

```text
1. HeadlessRenderSession 析构 fail-stop：
   teardown 时 MakeCurrent() 失败 → std::terminate()，
   绝不继续 renderer.Release() / ReleaseAll() 的 glDelete*。
2. native-Linux 硬件发布基线：
   script/verify_r17_native_linux.sh 在真实 Linux 机器执行，
   无 LIBGL_ALWAYS_SOFTWARE；smoke 必须 software=no 且
   session + sequence probe 全 PASS。
```
