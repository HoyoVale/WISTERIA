# WISTERIA 原生 C ABI 导出面（v0.6）

> 状态：与 `include/wisteria/native/wisteria_native.h` 同步。v0.x 不承诺
> 二进制稳定；本文是当前导出面的权威清单。

## 版本与句柄

- `WISTERIA_NATIVE_VERSION_MAJOR/MINOR`：0.6
- 句柄：`WisteriaContext / Model / Motion / Window / Scene / SceneModel /
  Entity / Light`（均为 `uint64_t` 不透明编号）
- 线程契约：一个 Context 单线程；错误统一 `WisteriaStatus` +
  `wisteria_last_error_message`。

## 分组清单

### 上下文

- `wisteria_create_context` / `wisteria_destroy_context`
- `wisteria_last_error_message`
- `wisteria_version_major` / `wisteria_version_minor`

### Headless 模型（Saba runtime）

- 模型：`wisteria_load_model` / `wisteria_unload_model`
- 动作：`load_motion / unload_motion / play / pause / resume /
  set_motion_looping / set_motion_frame / motion_frame / motion_max_frame`
- 步进：`wisteria_update`
- 物理：`wisteria_set_physics_settings` / `wisteria_set_physics_preset` /
  `wisteria_physics_capabilities`（自研预设：mode 0 标准 / 1 恢复 / 2 关闭，
  阻尼缩放，CCD）
- MMD 控制：`wisteria_set_mmd_ik_enabled` / `wisteria_find_bone_index` /
  `wisteria_load_camera_motion`
- 诊断：`wisteria_vertex_bounds`

### 窗口

- 生命周期：`wisteria_window_create` / `wisteria_window_destroy` /
  `wisteria_window_load_demo` / `wisteria_window_should_close`
- 无头渲染：`wisteria_window_create_hidden`（离屏渲染目标，配合
  `wisteria_scene_create` + `wisteria_poll_and_render` +
  `wisteria_window_read_pixels` 使用）
- 帧循环：`wisteria_poll_and_render`（旧符号为兼容包装）
- 输入：键按下/按下沿/释放沿、鼠标按钮、光标 delta、滚轮、光标捕获
- 相机：`wisteria_window_set_camera` / `wisteria_window_camera_pose` /
  `wisteria_window_set_camera_speed`
- 渲染配置：`wisteria_window_set_render_settings`（阴影分辨率/PCF/开关/
  地面影/深度 bias）
- 回读：`wisteria_window_framebuffer_size` / `wisteria_window_read_pixels`

### 自建场景（v0.5+）

- 场景：`wisteria_scene_create` / `wisteria_scene_destroy`
- 场景模型：`wisteria_scene_load_model`（PMX via Saba，OBJ/glTF via
  assimp）/ `wisteria_scene_unload_model`
- 实体：`wisteria_scene_instantiate_model` / `wisteria_entity_set_transform` /
  `wisteria_entity_get_transform` / `wisteria_entity_set_visible` /
  `wisteria_entity_get_visible` / `wisteria_entity_set_part_color` /
  `wisteria_entity_destroy`
- Morph：`wisteria_entity_set_morph_weight` / `wisteria_entity_get_morph_weight`
- 灯光：`wisteria_scene_add_directional_light` / `wisteria_scene_add_point_light`
  / `wisteria_scene_add_spot_light` / `wisteria_directional_light_set` /
  `wisteria_directional_light_get` / `wisteria_point_light_set` /
  `wisteria_point_light_get` / `wisteria_spot_light_set` /
  `wisteria_spot_light_get` / `wisteria_light_destroy`
- 环境：`wisteria_scene_set_environment`（天空盒开关 + 强度）
- 图元：`wisteria_scene_add_cube` / `wisteria_scene_add_ground_plane` /
  `wisteria_scene_add_sphere` / `wisteria_scene_add_cylinder` /
  `wisteria_scene_add_capsule` / `wisteria_scene_add_cone` /
  `wisteria_scene_add_torus`（程序化生成，core `primitives/procedural`）

## 二进制符号清单生成

```bash
# Windows
dumpbin /exports build/RelWithDebInfo/wisteria_native.dll
# Linux
nm -D --defined-only build-linux/libwisteria_native.so | grep " T "
```

新导出函数需引擎级用例 + 回归测试（integration 的 `Native ABI *` 测试），
并在本清单登记。
