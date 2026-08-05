# C ABI 安全矩阵

> 逐函数记录 91 个导出函数的异常边界状态。数据由脚本扫描
> `src/native/*.cpp` 整个函数体生成（不只看前几行），是 R1.S
> （ABI Safety）的输入清单。

## 状态说明

- `GUARDED`：函数体包含 `GuardAbi(context, [&]{ ... })`；
- `raw-try`：有裸 `try/catch`，但未统一走 `GuardAbi`；
- `UNGUARDED`：无异常边界。

## 汇总

| 文件 | 总数 | GUARDED | raw-try | UNGUARDED |
| ---- | ---- | ------- | ------- | --------- |
| native_common.cpp | 3 | 0 | 1 | 2 |
| native_model.cpp | 20 | 0 | 3 | 17 |
| native_window.cpp | 20 | 1 | 5 | 14 |
| native_scene.cpp | 48 | 17 | 6 | 25 |
| 合计 | 91 | 18 | 15 | 58 |

## native_common.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_create_context` | raw-try |
| `wisteria_destroy_context` | UNGUARDED |
| `wisteria_last_error_message` | UNGUARDED |

## native_model.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_load_model` | raw-try |
| `wisteria_unload_model` | UNGUARDED |
| `wisteria_load_motion` | raw-try |
| `wisteria_unload_motion` | UNGUARDED |
| `wisteria_play_motion` | UNGUARDED |
| `wisteria_pause_motion` | UNGUARDED |
| `wisteria_resume_motion` | UNGUARDED |
| `wisteria_set_motion_looping` | UNGUARDED |
| `wisteria_set_motion_frame` | UNGUARDED |
| `wisteria_motion_frame` | UNGUARDED |
| `wisteria_motion_max_frame` | UNGUARDED |
| `wisteria_update` | raw-try |
| `wisteria_set_physics_settings` | UNGUARDED |
| `wisteria_vertex_bounds` | UNGUARDED |
| `wisteria_set_mmd_ik_enabled` | UNGUARDED |
| `wisteria_find_bone_index` | UNGUARDED |
| `wisteria_load_camera_motion` | UNGUARDED |
| `wisteria_physics_capabilities` | UNGUARDED |
| `wisteria_set_physics_preset` | UNGUARDED |
| `wisteria_physics_reset` | UNGUARDED |

## native_window.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_window_create` | raw-try |
| `wisteria_window_destroy` | raw-try |
| `wisteria_window_load_demo` | raw-try |
| `wisteria_poll_and_render` | raw-try |
| `wisteria_window_poll_and_render` | UNGUARDED |
| `wisteria_window_should_close` | UNGUARDED |
| `wisteria_window_is_key_down` | UNGUARDED |
| `wisteria_window_was_key_pressed` | UNGUARDED |
| `wisteria_window_was_key_released` | UNGUARDED |
| `wisteria_window_is_mouse_button_down` | UNGUARDED |
| `wisteria_window_cursor_delta` | UNGUARDED |
| `wisteria_window_scroll_delta` | UNGUARDED |
| `wisteria_window_set_cursor_captured` | UNGUARDED |
| `wisteria_window_set_camera` | GUARDED |
| `wisteria_window_camera_pose` | UNGUARDED |
| `wisteria_window_set_camera_speed` | UNGUARDED |
| `wisteria_window_set_render_settings` | UNGUARDED |
| `wisteria_window_framebuffer_size` | UNGUARDED |
| `wisteria_window_read_pixels` | UNGUARDED |
| `wisteria_window_create_hidden` | raw-try |

## native_scene.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_scene_create` | raw-try |
| `wisteria_scene_destroy` | raw-try |
| `wisteria_scene_load_model` | raw-try |
| `wisteria_scene_unload_model` | UNGUARDED |
| `wisteria_scene_instantiate_model` | raw-try |
| `wisteria_entity_set_transform` | GUARDED |
| `wisteria_entity_get_transform` | UNGUARDED |
| `wisteria_entity_get_visible` | UNGUARDED |
| `wisteria_entity_set_part_color` | raw-try |
| `wisteria_entity_set_visible` | UNGUARDED |
| `wisteria_entity_destroy` | UNGUARDED |
| `wisteria_entity_runtime_backend` | UNGUARDED |
| `wisteria_entity_load_motion` | GUARDED |
| `wisteria_entity_unload_motion` | GUARDED |
| `wisteria_entity_restart_motion` | GUARDED |
| `wisteria_entity_pause_motion` | GUARDED |
| `wisteria_entity_resume_motion` | GUARDED |
| `wisteria_entity_set_motion_looping` | GUARDED |
| `wisteria_entity_set_motion_frame` | GUARDED |
| `wisteria_entity_motion_frame` | UNGUARDED |
| `wisteria_entity_motion_max_frame` | UNGUARDED |
| `wisteria_entity_set_mmd_ik_enabled` | GUARDED |
| `wisteria_entity_set_physics_settings` | GUARDED |
| `wisteria_entity_physics_reset` | GUARDED |
| `wisteria_entity_vertex_bounds` | UNGUARDED |
| `wisteria_entity_bone_count` | UNGUARDED |
| `wisteria_entity_bone_name` | UNGUARDED |
| `wisteria_entity_bone_local_matrix` | UNGUARDED |
| `wisteria_scene_add_directional_light` | GUARDED |
| `wisteria_scene_add_point_light` | GUARDED |
| `wisteria_light_destroy` | UNGUARDED |
| `wisteria_directional_light_set` | GUARDED |
| `wisteria_directional_light_get` | UNGUARDED |
| `wisteria_point_light_set` | GUARDED |
| `wisteria_point_light_get` | UNGUARDED |
| `wisteria_scene_add_spot_light` | GUARDED |
| `wisteria_spot_light_set` | GUARDED |
| `wisteria_spot_light_get` | UNGUARDED |
| `wisteria_entity_set_morph_weight` | UNGUARDED |
| `wisteria_entity_get_morph_weight` | UNGUARDED |
| `wisteria_scene_set_environment` | UNGUARDED |
| `wisteria_scene_add_cube` | raw-try |
| `wisteria_scene_add_ground_plane` | raw-try |
| `wisteria_scene_add_sphere` | UNGUARDED |
| `wisteria_scene_add_cylinder` | UNGUARDED |
| `wisteria_scene_add_capsule` | UNGUARDED |
| `wisteria_scene_add_cone` | UNGUARDED |
| `wisteria_scene_add_torus` | UNGUARDED |

## 修正记录

- 初版人工核查只看函数体前 12 行，误判 `wisteria_entity_set_transform`
  与三类 Light set 无护栏；脚本全函数体扫描确认它们均有 `GuardAbi`。
- 本矩阵以脚本扫描结果为准。

## R1.S 计划

- 所有 `UNGUARDED` 与 `raw-try` 统一迁移到 `GuardAbi`；
- 导出函数声明加 `noexcept`；
- 引入 generation handle；
- 明确 Window/Scene/Entity/Model/Light 父子生命周期；
- 增加 destroy-order、double-destroy、stale-handle 测试。
