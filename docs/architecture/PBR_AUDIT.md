# PBR 通用材质渲染审计（C4）

> 状态：C4 Phase 0 审计 + 第一项改进（2026-08-25）。
> 目标：为 glTF/GLB 及未来通用格式提供一条自研、可扩展的 PBR 材质线路。

## 1. 现状

### 已实现

| 能力 | 位置 |
| --- | --- |
| PbrMetallicRoughness 材质模型 | `rendering/material.hpp`、`basicTex.frag` |
| Cook-Torrance 直接光 | `basicTex.frag`：`DistributionGgx` / `GeometrySmith` / `FresnelSchlick` |
| 点光 / 平行光 / 聚光灯 | 同一 shader，uniform array |
| IBL diffuse | `irradianceMap`（预卷积 irradiance cubemap） |
| IBL specular | `prefilterMap` + `brdfLut` |
| 环境图生成 | `equirectangular_to_cube` / `irradiance_convolution` / `prefilter_environment` / `brdf_lut` shader |
| normal / metallicRoughness / occlusion / emissive 贴图 | `ImportMaterial` + `basicTex.frag` |
| alphaMode：Opaque / Mask / Blend | `EffectiveAlphaMode` + discard / blend |
| 透明排序 | Weighted OIT：`BeginOitPass` / `CompositeOit` / `oit_composite.frag` |
| 后处理 | FXAA：`present.frag` |
| tone mapping | 原先 Reinhard，已升级为 ACES（见下） |

### 缺口

| 能力 | 状态 |
| --- | --- |
| exposure / tone mapping 参数化 | ✅ 已通过 Renderer / OpenGlGraphExecutor API 暴露 |
| 多散射 BRDF 或 Kulla-Conty 近似 | 未实现，标准单散射 BRDF |
| 平行光阴影在 PBR 路径的软阴影/级联优化 | 已有基础，未专项调优 |
| 金属/粗糙度的 mipmap 细节 | 依赖环境 prefilter，未做材质 mip bias |
| 真实资产级视觉验收语料 | 本地 GLB 只有 baseColor 贴图，需要补充带全贴图的 PBR 测试资产 |

## 2. Tone mapping 参数化

新增 `include/wisteria/rendering/tone_mapping.hpp`：

```cpp
enum class ToneMappingMode : std::uint8_t
{
    Reinhard = 0,
    Aces = 1
};

struct ToneMappingSettings
{
    ToneMappingMode mode = ToneMappingMode::Aces;
    float exposure = 1.0f;
};
```

`Renderer` 与 `OpenGlGraphExecutor` 提供 Set/Get；shader 新增：

```glsl
uniform int toneMappingMode;
uniform float exposure;
```

默认保持 ACES + exposure 1.0，与之前行为一致。

## 3. C4 第一项：ACES tone mapping

原先：

```glsl
color = color / (color + vec3(1.0));   // Reinhard
color = pow(color, vec3(1.0 / 2.2));
```

问题：高光容易发灰，明暗压缩感明显。

现在：

```glsl
vec3 AcesTonemap(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp(color * (a * color + b) /
                 (color * (c * color + d) + e),
                 0.0, 1.0);
}

color = AcesTonemap(color);
color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
```

同时应用到：

```text
assets/shaders/basicTex.frag  通用 PBR
assets/shaders/mmd.frag       MMD（保持两条线路观感一致）
```

## 3. 验收

- `tests/render_fbo_tests.cpp` 中的 `pbr_quad.gltf` 渲染回归继续通过。
- 全量 CTest 通过。
- 视觉对照：`--gltf tests/assets/models/Box.glb` 与真实 GLB 截图。
