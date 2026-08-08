# R1.6 Phase 0D — Explicit Presentation Authority 契约

> 状态：**CONTRACT FROZEN（2026-08-09，契约级审查闭合）**。
> 基线：R1.6 Phase 0A–0C CLOSED（`a066935`）。
> 说明：工作区已含按用户指示完成的 0D 首次实现（`offline_render` +
> `mmd_presentation`，四矩阵全绿）；本契约按实际源码审计并与实现对齐，
> 审查如需调整则以契约为准改实现。

## 1. 一句话

把 Camera / Light 的最终决定权收口到 orchestration 层：MMD Runtime
只输出 neutral sample，host 通过可复用 adapter 决定是否应用；Renderer
只消费**显式 Camera + Projection + 当前 Scene lights**，离线与窗口
共享同一条输出链但各自持有权威。

## 2. 三个必须先钉死的问题

### 2.1 MMD perspective / orthographic camera → WISTERIA Camera + Projection

源码事实（`src/mmd/mmd_camera_conversion.cpp`、
`include/wisteria/rendering/camera.hpp`）：

```text
ToCameraParam：MMD look-at 语义 → Position/Target/Up；
  perspective == false 时保持 fallback 的投影设置，
  仍应用 look-at pose（现有行为）
WISTERIA Camera 只有 perspective：
  VerticalFovDegrees / NearClip / FarClip；
  SetParam 校验 FOV ∈ [1,179]，非法值抛异常（已定义行为）
CameraTrackSample.perspective 是 optional：
  Saba VMDCameraAnimation 丢弃 VMD perspective flag → nullopt
```

冻结：

```text
v1：MMD 相机只映射到 perspective host camera。
  perspective == true / nullopt：
    Position/Target/Up 来自 look-at；
    sample.viewAngle 成为 host FOV candidate；
    Camera validation（finite 且 1 < FOV < 179）决定是否合法——
    非法 sample 明确失败（Camera::SetParam 抛 invalid_argument），
    不静默 fallback
  perspective == false（orthographic）：
    仍应用 look-at pose；Projection 保持 fallback
    （FOV/clip 由 fallback 决定）
    不伪造 orthographic Camera 类型

Projection authority 永远在渲染调用处：
  Renderer::Render(scene, camera, projection, target)
  / OfflineRenderRequest.projection
  MMD sample 不携带 projection；只影响 Camera 姿态与 FOV 候选。

orthographic host camera 支持不在 R1.6（记 debt）。
```

### 2.2 VMD light → 覆盖哪个 Scene light、坐标如何转换

源码事实（`src/mmd/mmd_light_conversion.cpp`）：

```text
ToLightData：
  Color = clamp(sample.color, [0,1])
  Direction = -normalize(sample.position)
    （sample.position 已含 VMD z-flip）
    near-zero position → (0,-1,0) fallback
  Intensity 等来自 fallback
ApplyMmdLightFrame 应用到 host 提供的 DirectionalLight
Scene 支持 point / directional / spot 三类灯；MMD 只有平行光语义
```

冻结：

```text
v1：VMD light 覆盖一个 host 指定的 DirectionalLight
  （惯例是 Scene 第一个 directional light；host 决定目标，
   adapter 不自动搜索、不自动创建、不管理多灯）
  颜色/方向转换按 ToLightData 冻结；Intensity 来自 fallback
  不把 VMD light 应用到 point / spot

MMD runtime 只输出 LightTrackSample，绝不写 Scene。
```

### 2.3 offline / window presentation authority（三套权威问题）

源码事实：

```text
Renderer::Render(scene, camera, projection, target)：显式参数
WindowManager::RenderWindow：
  window.GetCamera() + window.Projection(aspect)
  （window presentation policy）
RenderOffline / OfflineRenderRequest：显式 camera + projection
  （offline presentation authority）
Scene::ActiveCamera 是现存 legacy/compatibility surface，
但不是 R1.6 presentation authority；0D 不新增、不读取、不依赖它
demo_scene 手工应用 VMD camera/light（历史唯一路径）
```

冻结：

```text
同一 frame 的 presentation authority 只有一层：
  渲染调用处的显式 Camera + Projection + 当前 Scene lights。

  Window 帧 → Window 自己的 camera（window presentation policy）
  Offline 帧 → OfflineRenderRequest（显式）
  VMD 驱动帧 → orchestration 层先 ApplyMmdCameraFrame /
                ApplyMmdLightFrame，再以同一 camera/light 调用
                Render / RenderOffline

不把 Scene::ActiveCamera 当作 0D authority；不引入全局 current camera；
不为 0D 删除 Scene::ActiveCamera（避免无谓重开 Scene API）。
Renderer 不读 VMD、不 dynamic_cast Saba、不推进 timeline。
demo 是 adapter 的既有消费方；新路径与 demo 并存，
不迁移 demo（避免无谓回归）。
```

## 3. 实现映射（工作区首次实现）

```text
include/wisteria/rendering/offline_render.hpp
src/rendering/offline_render.cpp
  OfflineRenderRequest（width/height/Camera/projection/clearColor）
  RenderOffline → SceneFramebuffer → Renderer.Render → ReadbackRgba8
  （pre-Present / pre-FXAA，显式 authority 的离线入口）

include/wisteria/mmd/mmd_presentation.hpp
src/mmd/mmd_presentation.cpp
  ApplyMmdCameraFrame / ApplyMmdLightFrame
  （sample → ToCameraParam/ToLightData → host Camera/DirectionalLight）
```

测试：

```text
render_fbo_tests：OfflineRenderRequest smoke（box.glb）
integration：生成 camera+light VMD →
  未加载轨道返回 false 且对象不变；
  加载后 camera look-at/FOV 与 light 颜色/方向正确；
  Scene directional light 更新
```

四矩阵全绿（Windows CORE/FULL、Linux CORE/FULL，2026-08-09）。

## 4. 明确不做（0D 边界）

```text
orthographic host camera（debt，后续阶段）
Scene::ActiveCamera / 全局 current camera
自动多灯管理 / point-spot 灯光应用
Stable C Render API（输出链全部正确后再议）
PNG / manifest / frame sequence（0E）
Headless context provider（R1.7）
demo 迁移
```

## 5. 开放决策（0A 审查时定）

```text
1. orthographic host camera：记 debt 延后（建议：是）
2. VMD light 无 host 目标时：不应用（建议：host 不提供目标则跳过）
3. 是否需要 PresentationFrame（camera+projection+lights 聚合）作为
   0E 输入（建议：0E 冻结时再定，0D 不引入）
```
