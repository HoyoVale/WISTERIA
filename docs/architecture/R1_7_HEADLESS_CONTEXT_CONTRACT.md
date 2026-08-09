# R1.7 — True Headless Context Provider（契约草案）

> 状态：**FROZEN v1.0；R1.7 0A–0D CLOSED，0E FINAL CLOSURE PENDING**
> （2026-08-09：Windows/WSL 四矩阵通过；native-Linux 硬件 release gate
> 待真实 Linux 机器执行 `script/verify_r17_native_linux.sh`）
> 前置：R1.6 Phase 0A–0E CLOSED（`R1_6_OFFLINE_OUTPUT_CONTRACT.md` 第 383 行：
> `Phase 0F 拆为 R1.7 — Headless Context Provider`）。
> 本文件只冻结 R1.7 的范围、边界、验收口径与阶段计划；精确接口字段在
> Phase 0B 对照 EGL 实测后冻结。

## 1. 一句话

R1.7 让 WISTERIA 的离线渲染链
（`Scene → Renderer → SceneFramebuffer → ReadbackRgba8`）不再依赖任何
Window System：无窗口、无 Present、无 SwapBuffers、无桌面环境，
由引擎自有的 Headless Context Provider 提供 OpenGL 上下文，
并顺带修复 R1.6 留下的 GraphicsDevice share-group identity 债务。

```text
Model / Motion
      ↓
   Runtime
      ↓
    Scene
      ↓
  Renderer
      ↓
EGL surfaceless / software fallback context
      ↓
 SceneFramebuffer → RGBA8 → PNG / raw
```

## 2. 现状盘点（2026-08-09 实测）

### 2.1 当前 GL context 创建路径

```text
窗口路径   Application → GLFW → Window(glfwCreateWindow) → MakeContextCurrent
离屏路径   R1.6 = 同一个 Application 的 hidden GLFW window（visible=false），
           或测试内直接 glfwCreateWindow(GLFW_VISIBLE=FALSE)
Linux NULL 后端 = GLFW 无平台后端：glfwInit 失败 → render 测试 SKIP（无 GL）
```

`RenderOffline`（`include/wisteria/rendering/offline_render.hpp`）只要求
"owning GL context 当前"；`OfflineFrameSequence` 从宿主借用
Scene/Renderer/Runtime/ModelInstance，不拥有 context。因此
**Headless Context 可以只替换"context 从哪来"，不改 Renderer 与序列管线**。

### 2.2 R1.6 明确留给 R1.7 的债务

```text
A. Headless context provider（EGL/OSMesa/surfaceless）
B. GraphicsDevice share-group identity：
   - context token 目前用"第一个 GLFWwindow*"近似
   - multi-window 共享 context 下非 owner window 的
     ContextIsCurrent() 为 false → FlushPendingDeletes 不执行
   - MakeContextCurrent 后没有自动 FlushPendingDeletes
   - 开放 Stable Render C Portal 前必须解决
```

### 2.3 WSL / 真实 Linux 环境实测（本机 WSL Ubuntu 22.04）

```text
EGL client extensions:  EGL_EXT_platform_base、EGL_EXT_platform_device、
                        EGL_MESA_platform_surfaceless、...（存在）
EGL 版本:               1.5（Mesa Project）
GBM platform:           eglInitialize 失败（WSL 无 /dev/dri 访问）
Wayland platform:       可用（WSLg），但依赖窗口系统，不是 headless
OSMesa:                 未安装（无头文件、无库）→ v1 不作为必选 provider
已知问题:               WSLg Mesa D3D12（vendor=Microsoft）首帧后读回全黑，
                        必须 LIBGL_ALWAYS_SOFTWARE=1 软件回退；
                        真实独立 Linux 机器渲染链路正常（R0.4 已定性）
```

结论：**v1 只做 EGL**：首选 EGL surfaceless（Mesa），软件回退用
`EGL_EXT_platform_device` + `EGL_MESA_device_software`（llvmpipe），
不依赖 DISPLAY/WAYLAND_DISPLAY。WSL 只做兼容性验证，真实 Linux 是发布基线。
OSMesa 明确留到 v2/按需，R1.7 v1 不实现。

## 3. R1.7 范围

### 3.1 必须交付

```text
1. HeadlessContext 抽象（platform 层）：
   - 创建 / 销毁 / MakeCurrent / ReleaseCurrent
   - 返回 opaque context token（接入 GraphicsDevice identity）
   - 返回诊断信息（vendor / renderer / GL version / platform）
2. EGL provider（Linux，含 WSL）：
   - 首选 EGL_MESA_platform_surfaceless
   - 回退 EGL_EXT_platform_device + EGL_MESA_device_software（llvmpipe）
   - 失败时给出可读诊断，不静默
3. GLFW hidden-window provider：
   - 保留为通用参考实现 / 失败回退 / Windows 现有路径回归
4. GraphicsDevice share-group identity 修复：
   - context token 由 provider 注册，不再用"第一个窗口"近似
   - MakeContextCurrent 后自动 FlushPendingDeletes
   - ReleaseAll 在 headless 或窗口 context 下都成立
5. Headless 渲染入口（无窗口）：
   - RenderOffline 可在纯 headless context 上运行
   - OfflineFrameSequence 可在无窗口 session 上运行
6. 测试与验证矩阵
```

### 3.2 明确不做（R1.7 边界）

```text
Vulkan / RenderGraph / RenderDevice / IRenderTarget 抽象
Stable Render C Portal
多 context 并发 / 多线程渲染
Windows WGL PBuffer / ANGLE 真 headless（保持 hidden window 路径）
OSMesa（留 v2/按需：若未来需要纯 CPU、直接渲染到用户内存、
完全不依赖 window system 的独立软件 provider，可考虑 OSMesa）
视频编码 / FFmpeg / Audio
```

## 4. 接口草案（双身份模型，Final Fix 后冻结）

```cpp
// R1.7 Final Fix：OpenGL 存在两个所有权域，必须双身份。
using GraphicsContextToken = const void*;
using GraphicsShareGroupToken = const void*;

// include/wisteria/platform/headless_context.hpp（命名先冻结）
struct HeadlessContextOptions
{
    int major = 3;
    int minor = 3;
    // forceSoftware == true：必须使用软件 renderer（llvmpipe/softpipe）。
    // 找不到软件设备时明确失败，绝不偷偷回退硬件。
    bool forceSoftware = false;
};

class IHeadlessContext
{
public:
    virtual ~IHeadlessContext() = default;

    virtual void MakeCurrent() = 0;
    virtual void ReleaseCurrent() = 0;

    virtual GraphicsContextToken ContextToken() const noexcept = 0;
    virtual GraphicsShareGroupToken ShareGroupToken() const noexcept = 0;
    virtual std::string_view ProviderName() const noexcept = 0;
    virtual std::string_view PlatformName() const noexcept = 0;
    virtual std::string_view EglVersion() const noexcept = 0;
    virtual std::string_view EglVendor() const noexcept = 0;
    virtual std::string_view Renderer() const noexcept = 0;
    virtual std::string_view Vendor() const noexcept = 0;
    virtual std::string_view Version() const noexcept = 0;
    virtual bool IsSoftware() const noexcept = 0;
};

// provider factory：EGL 优先，GLFW hidden 回退；forceSoftware 时
// 跳过硬件设备直接选 llvmpipe/softpipe；EGL 不可用/失败返回 nullptr。
std::unique_ptr<IHeadlessContext> CreateHeadlessContext(
    const HeadlessContextOptions& options = {});
```

**对象所有权分域（Final Fix 冻结）：**

```text
Shared（按 ShareGroupToken 判断）：
  Texture / Buffer / Renderbuffer / Shader / Program

Context-local（按 ContextToken 判断）：
  VertexArray / Framebuffer
  后续 Query / TransformFeedback / ProgramPipeline 按规范归类

删除规则：
  shared object      → CurrentShareGroup == OwnerShareGroup 才可删
  context-local      → CurrentContext == OwnerContext 才可删
  context-local 排队项必须记录 owning ContextToken；
  同 share group 的兄弟 context 不得 flush 它。
```

**share-group identity 修正（Phase 0B/0C 迁移，Final Fix 补齐）：**

```text
旧语义（错误）：
  GraphicsDevice::contextToken = first GLFWwindow*
  所有 ResourceKind 统一按 share group 判断
  → 第二个共享窗口 != token → ContextIsCurrent() == false
  → VAO/FBO 在兄弟 context 上被误删（context-local 命名空间不同）

新语义（冻结，双身份）：
  GraphicsDevice owns ShareGroupToken（一个 share group 一个身份）
  SetShareGroupToken(...) / ShareGroupToken()
  SetCurrentContext(...) / CurrentContext()（native context 身份）
  SetCurrentShareGroup(...) / CurrentShareGroup()
  Native Context A/B ─→ ContextToken A/B ─→ ShareGroup #17
  shared：当前 context 属于 #17 → 可删
  context-local：当前 ContextToken == 创建它的 ContextToken → 可删
```

**生命周期事务规则（冻结）：**

```text
MakeCurrent
    ↓
注册 current context token
    ↓
注册 current share-group
    ↓
FlushPendingDeletes（shared 队列 + context-local 队列）
```

"context 成为 current" 与 "处理该身份对应的 pending GPU deletes"
是一个生命周期事务，不允许再依赖"下一帧 RenderAll 恰好帮我 flush"。
销毁 native context 后必须清空 CurrentContextToken 与
CurrentShareGroupToken。

头文件依赖规则（不变）：

```text
rendering/headless_context.hpp    暴露 IHeadlessContext + options
                                  （无 EGL/GLFW 类型）
platform/headless_context.hpp     只暴露 CreateHeadlessContext factory
platform/egl_headless_context.cpp 私有包含 EGL 头（platform 层允许）
core/rendering 不 include EGL / GLFW 类型
```

## 5. 平台矩阵与验收口径

| 环境 | 预期 | 性质 |
|---|---|---|
| 真实 Linux（硬件 GL/EGL） | EGL surfaceless 或硬件 EGL，像素输出正常 | **发布基线** |
| WSLg（Mesa D3D12） | 自动/显式软件回退 llvmpipe，像素输出正常 | 兼容性记录 |
| WSL 无 DISPLAY/WAYLAND_DISPLAY | EGL surfaceless + 软件回退仍可渲染 | 兼容性记录 |
| Windows | hidden GLFW 路径全部回归通过 | 回归基线 |
| Linux NULL 后端 | 编译通过；无 context 时测试 SKIP（保持现状） | 编译基线 |

验收命令（草案）：

```bash
# WSL：无桌面环境 + 软件回退
env -u DISPLAY -u WAYLAND_DISPLAY \
  ./build-linux/wisteria_headless_smoke --software

# WSL：软件回退 + 确定性序列
env -u DISPLAY -u WAYLAND_DISPLAY LIBGL_ALWAYS_SOFTWARE=1 \
  ./build-linux/wisteria_render_tests

# 真实 Linux：硬件 EGL
./build-linux/wisteria_render_tests
```

像素口径：R1.7 不承诺跨平台/跨渲染器 pixel hash 一致；只承诺
**同环境、同 provider 下可复现**，与 R1.6 冻结口径一致。

## 6. 阶段计划

```text
Phase 0A  契约（本文档）——冻结范围与边界                    ✅ CLOSED
Phase 0B  HeadlessContext + EGL Provider（范围见下）         ✅ CLOSED
Phase 0C  GraphicsDevice share-group identity +
          FlushPendingDeletes 修复（范围见下）               ✅ CLOSED
Phase 0D  Headless 渲染 session + OfflineFrameSequence
          无窗口运行 + 测试（范围见下）                      ✅ CLOSED
Phase 0E  四矩阵验证 + Final Closure                         ⏳ PENDING
          （native-Linux 硬件 release gate）
```

### Phase 0B 范围（已批准）

```text
1.  IHeadlessContext
2.  HeadlessContextOptions
3.  GraphicsShareGroupToken 语义冻结（类型冻结，迁移在 0C）
4.  EGL platform discovery
    ├─ EGL_MESA_platform_surfaceless
    └─ EGL_EXT_platform_device
5.  EGL OpenGL 3.3 Core context 创建
6.  software-device selection（forceSoftware 严格语义）
    （Final Fix：创建后必须验证 GL_RENDERER 为 llvmpipe/softpipe/swrast；
      D3D12/Intel/NVIDIA/AMD 一律 FAIL，不得仅凭设备扩展报告成功）
7.  GL function loader（eglGetProcAddress + dlsym fallback）
8.  diagnostics（EGL version/vendor、provider/platform、GL vendor/renderer/version）
9.  wisteria_headless_smoke
    ├─ 不创建 GLFW window
    ├─ 不需要 DISPLAY / WAYLAND_DISPLAY
    ├─ MakeCurrent → GL loader → 最小 FBO → clear → glReadPixels 验证
10. EGL 生命周期测试 Create → Current → Release → Current → Destroy
```

Phase 0B **不做**：

```text
HeadlessRenderSession
OfflineFrameSequence migration
GraphicsDevice 全面改造（share-group 语义/类型在 0B 冻结，迁移在 0C）
Windows WGL
OSMesa
Stable C ABI
Application 重构
渲染级 compatibility probe（0D/0E）
```

### Phase 0C 范围（已批准）

```text
1. GraphicsContextToken + GraphicsShareGroupToken 下沉到
   rendering/graphics_context.hpp
   （platform/headless_context.hpp 复用，依赖方向保持 rendering ← platform）
2. GraphicsDevice API 迁移：
   SetShareGroupToken / ShareGroupToken / HasShareGroupToken /
   RequireShareGroupToken / SetCurrentShareGroup / CurrentShareGroup
   （ContextIsCurrent 语义 = 当前 native context 属于本 share group）
3. Window：
   - 持有 shareGroupToken（WindowManager 统一赋值）
   - 持有非拥有 GraphicsDevice*（仅用于生命周期事务）
   - MakeContextCurrent = MakeCurrent → 注册 current share-group
     → FlushPendingDeletes（单一事务，不再依赖 RenderAll）
4. WindowManager：
   - ShareGroupIdentity 统一 token（所有共享窗口映射同一身份）
   - CreateWindow 后赋 token/device；恢复 previous context 时注册
     share group + flush
   - SetGraphicsDevice 向既有窗口传播 device
5. Application：CreateWindow 注册首个 share group；Shutdown 用
   share-group token 校验 + 释放
6. native read_pixels：移除手动 FlushPendingDeletes（事务已覆盖）
7. EGL provider：ShareGroupToken 改为身份对象（非 native handle）；
   MakeCurrent/ReleaseCurrent 注册/清空 current share group
8. 测试更新：unit GraphicsDevice ownership、render deferred-delete
   queue；Windows + WSL 双平台回归
```

Phase 0C **不做**：

```text
HeadlessRenderSession（0D）
Application 零窗口模式（0D）
OfflineFrameSequence migration（0D）
Windows WGL / OSMesa / Stable C ABI
渲染级 compatibility probe（0D/0E）
```

### Final Fix（2026-08-09 二轮审查后，已实施）

```text
1. 新增 GraphicsContextToken，双身份：CurrentContextToken +
   CurrentShareGroupToken；GraphicsShareGroupToken 不推翻
2. 对象所有权分域：
   shared（Texture/Buffer/Renderbuffer）按 share group 判断
   context-local（VertexArray/Framebuffer）按 owning context 判断
3. context-local deferred delete 记录 owning ContextToken；
   兄弟 context 不得 flush
4. Window::MakeContextCurrent：注册 ContextToken →
   注册 ShareGroupToken → flush 两个队列
5. Window::Release：context-local 资源在 owning context 释放；
   glfwDestroyWindow 后清空两个 tracker
6. ReleaseAll 的 ownership 校验失败 → 跳过 GL teardown（fail-stop）
7. forceSoftware：GL_RENDERER 必须真是软件渲染器，D3D12 FAIL
8. EglHeadlessContext 构造失败释放已创建 EGL 资源；
   ReleaseCurrent 检查 eglMakeCurrent 返回值后再清 tracker
9. 新测试：
   render：双 context 所有权矩阵（共享 Texture 可跨删；
     VAO/FBO 排队、兄弟 context 不 flush、owning context 释放）
   integration：销毁当前 window context 后两个 tracker 为空
10. 0B/0C baseline 修订
```

### Final Micro Fix（2026-08-09 三轮复审后，已实施）

```text
不变量：CreateHeadlessContext 返回后，native EGL context 不得保持
current，CurrentContext / CurrentShareGroup 必须为 nullptr。

实现：Initialize() 完成 strict gate 后主动 eglMakeCurrent(NO_CONTEXT)
并清空两个 tracker；显式 MakeCurrent() 才是进入
"native current → ContextToken → ShareGroupToken" 事务的唯一入口。

smoke 新增断言：
  Create 后 trackers 为 null；
  MakeCurrent 后 CurrentContext == ContextToken、
    CurrentShareGroup == ShareGroupToken；
  ReleaseCurrent 后两个 tracker 回到 null。

文档修正：
  --software + LIBGL_ALWAYS_SOFTWARE=1 实测 platform=device-software
  （不是 surfaceless）；0C 语义验收点 #1 改为 shared/context-local 分域措辞。
```

### Phase 0D 范围（已批准）

```text
1. IHeadlessContext / HeadlessContextOptions 下沉到
   rendering/headless_context.hpp（无 EGL/GLFW 类型）；
   platform/headless_context.hpp 只保留 CreateHeadlessContext factory
2. HeadlessRenderSession（rendering/headless_render_session.hpp/.cpp）：
   composition root = IHeadlessContext + GraphicsDevice +
   ResourceManager + Renderer；不碰 Application
3. Session MakeCurrent 事务：
   provider MakeCurrent → 双身份注册 → FlushPendingDeletes
4. 零窗口 RenderOffline：session.RenderOffline(scene, request)
   复用 ::wisteria::RenderOffline（SceneFramebuffer → RGBA8）
5. 零窗口 OfflineFrameSequence：
   pmx_physics.pmx + Box.glb → RenderRange(0..2) →
   PNG / manifest.jsonl / checkpoint-A/B.bin
6. 渲染级 compatibility probe（WSL 默认软件回退口径）：
   Box.glb + directional light 全链路渲染，RGBA8 非黑且不透明
7. headless_smoke 扩展：
   lifecycle + FBO + session probe + sequence probe
8. Windows / WSL 双平台回归
```

Phase 0D **不做**：

```text
四矩阵/0E Final Closure
Stable Render C ABI
Windows WGL / OSMesa
Application 零窗口模式
多线程渲染
视频编码 / Audio
```

## 7. 已拍板决策（2026-08-09）

| 决策项 | 结论 |
|---|---|
| v1 平台范围 | Linux EGL surfaceless + WSL 软件回退 + GLFW hidden reference |
| Windows WGL PBuffer | 不进 v1，留后续 |
| OSMesa | 不进 v1，留 v2/按需 provider |
| 无窗口入口 | 独立 `HeadlessRenderSession`（Phase 0D），不改 Application |
| 软件回退 | 自动探测 + 显式 `forceSoftware`（严格语义） |
| share-group | `GraphicsShareGroupToken` 替代 native context token（0B 冻结类型，0C 迁移） |
| 双身份 | Final Fix：`GraphicsContextToken` + `GraphicsShareGroupToken` 并存 |
| 严格 software gate | `forceSoftware` 必须验证 GL_RENDERER，D3D12 拒绝 |
| teardown fail-stop | ownership 校验失败后不得继续 GL 删除 |

软件回退严格语义：

```text
forceSoftware == true → 必须软件 renderer（llvmpipe/softpipe）
                      → 找不到软件设备 = 明确失败，绝不回退硬件

forceSoftware == false → EGL surfaceless/default → 失败才走
                         device（硬件优先，软件兜底）
```

注意：WSL 的 Mesa D3D12 问题可能发生在"context 创建成功、渲染正常、
后续 readback/帧异常"阶段，因此 **0B 只保证 context 层自动回退**；
渲染级 compatibility probe 属于 0D/0E 验收（用 RenderOffline + RGBA8
readback 作为真实探针）。

## 8. 成功标准

```text
1. env -u DISPLAY -u WAYLAND_DISPLAY 下能完成 RenderOffline 并得到
   非空、可校验的 RGBA8 帧（WSL 软件回退）
2. 真实 Linux 硬件路径与 hidden GLFW 参考在同环境像素一致（或给出
   已记录的 provider 差异）
3. Windows 全部既有测试零回归
4. GraphicsDevice 在多窗口 + headless 混合生命周期下无 pending delete
   泄漏，ReleaseAll 恒在 owning context 下执行
```
