# R1.6 Phase 0D — Explicit Presentation Authority 实现基线（2026-08-09）

> 状态：**COMPLETED（四矩阵全绿）**。
> 契约：`docs/architecture/R1_6_OFFLINE_OUTPUT_CONTRACT.md`
> （CONTRACT FROZEN，Phase 0D 方向）。

## 1. 一句话

把“谁来决定 Camera/Light”正式收口为 orchestration 层：
MMD Runtime 只提供 neutral sample，host 通过可复用的
`ApplyMmdCameraFrame / ApplyMmdLightFrame` 决定是否应用；离屏输出
通过显式 `OfflineRenderRequest`（Camera + Projection + 尺寸）驱动
同一渲染链，不依赖 Window。

## 2. 代码改动

### 新增：offline_render

```text
include/wisteria/rendering/offline_render.hpp
src/rendering/offline_render.cpp
CMakeLists.txt：WISTERIA_RENDERING_SOURCES 加入
```

```cpp
struct OfflineRenderRequest
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    Camera camera;
    glm::mat4 projection{1.0f};
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};  // opaque alpha policy
};

Rgba8Frame RenderOffline(
    Scene& scene,
    const OfflineRenderRequest& request,
    Renderer& renderer
);
```

语义：

```text
显式 Camera + Projection；Scene lights 是渲染时权威 presentation state
内部 SceneFramebuffer → Renderer.Render → ReadbackRgba8
pre-Present / pre-FXAA SceneColor
要求 owning GL context current
不涉及 Window / Present / SwapBuffers
```

### 新增：mmd_presentation（orchestration 层 adapter）

```text
include/wisteria/mmd/mmd_presentation.hpp
src/mmd/mmd_presentation.cpp
CMakeLists.txt：WISTERIA_MMD_SOURCES 加入
```

```cpp
bool ApplyMmdCameraFrame(
    const MmdRuntimeModel& runtime, float frame,
    Camera& camera, const CameraParam& fallback
);
bool ApplyMmdLightFrame(
    const MmdRuntimeModel& runtime, float frame,
    DirectionalLight& light, const DirectionalLightData& fallback
);
```

语义：

```text
SampleCameraMotion / SampleLightMotion → 既有 ToCameraParam / ToLightData
→ 应用到 host Camera / DirectionalLight
无轨道/不可采样 → false，host 对象保持不变
Renderer 永远不采样 VMD；demo 不再是唯一能应用 VMD camera/light 的地方
Scene 接线（创建/更新 DirectionalLight）由 host 负责（mmd 不依赖 scene）
```

## 3. 测试

### render_fbo_tests

```text
R1.6 OfflineRenderRequest smoke：
  box.glb + 显式 camera/projection → RenderOffline
  尺寸正确、RGBA 非空（RGB 内容证明）
```

### integration_tests（新增 1 项，CORE）

```text
R1.6 MMD presentation application：
  内存生成 camera+light VMD（temp 文件，测试后删除）
  未加载轨道：Apply* 返回 false 且 host 对象不变
  LoadCameraMotion / LoadLightMotion 后：
    camera position (0,0,8)、target (0,0,7)、FOV 30
    light color (1,0.9,0.8)、direction 按 MMD position 归一化
  Scene-level：更新既有 Scene directional light
```

## 4. 四套矩阵（2026-08-09 实测）

```text
Windows CORE (MSVC Release)          8/8 Passed
Windows FULL (MSVC RelWithDebInfo)   9/9 Passed
Linux CORE (GCC RelWithDebInfo)      8/8 Passed
Linux FULL (GCC RelWithDebInfo)      9/9 Passed
```

Linux 矩阵使用 README 记录的 WSLg 软件渲染退路
（`LIBGL_ALWAYS_SOFTWARE=1`）。

## 5. 边界（Phase 0D 不做）

```text
不冻结 Stable Render C API
不做 PNG / manifest / frame sequence（0E）
不改 Renderer（显式 Camera/Projection 入口已存在）
不做 Headless context provider（R1.7）
不把 Scene 依赖塞进 mmd 模块（Scene 接线由 host 负责）
```

## 6. 下一步

Phase 0E：Deterministic Frame Sequence——Prepare exact frame N →
Apply presentation（本阶段 adapter）→ Render offscreen →
Readback → write frame（raw + PNG + manifest）+ resume/checkpoint
集成。
