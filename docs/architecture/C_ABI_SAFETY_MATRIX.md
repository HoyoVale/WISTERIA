# C ABI 安全矩阵

> 本文件由 `script/gen_abi_safety_matrix.py` 自动生成，禁止手改。
> R1.S 应将该脚本接入 CI，防止矩阵随 C API 扩展过期。

## 状态说明

- `GUARDED`：函数体包含 `GuardAbi(context, [&]{ ... })`；
- `RAW_TRY`：有裸 `try/catch`，未统一走 `GuardAbi`；
- `PROVEN_NO_THROW_LEAF`：无状态查询/常量 leaf，可证明不抛；
- `UNGUARDED`：无异常边界。

## 汇总

| 状态 | 数量 |
| ---- | ---- |
| 总计 | 94 |
| GUARDED | 18 |
| RAW_TRY | 16 |
| PROVEN_NO_THROW_LEAF | 3 |
| UNGUARDED | 57 |

## native_common.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_create_context` | RAW_TRY |
| `wisteria_destroy_context` | UNGUARDED |
| `wisteria_last_error_message` | UNGUARDED |
| `wisteria_status_name` | PROVEN_NO_THROW_LEAF |
| `wisteria_version_major` | PROVEN_NO_THROW_LEAF |
| `wisteria_version_minor` | PROVEN_NO_THROW_LEAF |

`native_common.cpp`：0/6 GUARDED

## native_model.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_find_bone_index` | UNGUARDED |
| `wisteria_load_camera_motion` | UNGUARDED |
| `wisteria_load_model` | RAW_TRY |
| `wisteria_load_motion` | RAW_TRY |
| `wisteria_motion_frame` | UNGUARDED |
| `wisteria_motion_max_frame` | UNGUARDED |
| `wisteria_pause_motion` | UNGUARDED |
| `wisteria_physics_capabilities` | UNGUARDED |
| `wisteria_physics_reset` | UNGUARDED |
| `wisteria_play_motion` | UNGUARDED |
| `wisteria_resume_motion` | UNGUARDED |
| `wisteria_set_mmd_ik_enabled` | UNGUARDED |
| `wisteria_set_motion_frame` | UNGUARDED |
| `wisteria_set_motion_looping` | UNGUARDED |
| `wisteria_set_physics_preset` | UNGUARDED |
| `wisteria_set_physics_settings` | UNGUARDED |
| `wisteria_unload_model` | UNGUARDED |
| `wisteria_unload_motion` | UNGUARDED |
| `wisteria_update` | RAW_TRY |
| `wisteria_vertex_bounds` | UNGUARDED |

`native_model.cpp`：0/20 GUARDED

## native_scene.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_directional_light_get` | UNGUARDED |
| `wisteria_directional_light_set` | GUARDED |
| `wisteria_entity_bone_count` | UNGUARDED |
| `wisteria_entity_bone_local_matrix` | UNGUARDED |
| `wisteria_entity_bone_name` | UNGUARDED |
| `wisteria_entity_destroy` | UNGUARDED |
| `wisteria_entity_get_morph_weight` | UNGUARDED |
| `wisteria_entity_get_transform` | UNGUARDED |
| `wisteria_entity_get_visible` | UNGUARDED |
| `wisteria_entity_load_motion` | GUARDED |
| `wisteria_entity_motion_frame` | UNGUARDED |
| `wisteria_entity_motion_max_frame` | UNGUARDED |
| `wisteria_entity_pause_motion` | GUARDED |
| `wisteria_entity_physics_reset` | GUARDED |
| `wisteria_entity_restart_motion` | GUARDED |
| `wisteria_entity_resume_motion` | GUARDED |
| `wisteria_entity_runtime_backend` | UNGUARDED |
| `wisteria_entity_set_mmd_ik_enabled` | GUARDED |
| `wisteria_entity_set_morph_weight` | UNGUARDED |
| `wisteria_entity_set_motion_frame` | GUARDED |
| `wisteria_entity_set_motion_looping` | GUARDED |
| `wisteria_entity_set_part_color` | RAW_TRY |
| `wisteria_entity_set_physics_settings` | GUARDED |
| `wisteria_entity_set_transform` | GUARDED |
| `wisteria_entity_set_visible` | UNGUARDED |
| `wisteria_entity_unload_motion` | GUARDED |
| `wisteria_entity_vertex_bounds` | UNGUARDED |
| `wisteria_light_destroy` | UNGUARDED |
| `wisteria_point_light_get` | UNGUARDED |
| `wisteria_point_light_set` | GUARDED |
| `wisteria_scene_add_capsule` | UNGUARDED |
| `wisteria_scene_add_cone` | UNGUARDED |
| `wisteria_scene_add_cube` | RAW_TRY |
| `wisteria_scene_add_cylinder` | UNGUARDED |
| `wisteria_scene_add_directional_light` | GUARDED |
| `wisteria_scene_add_ground_plane` | RAW_TRY |
| `wisteria_scene_add_point_light` | GUARDED |
| `wisteria_scene_add_sphere` | UNGUARDED |
| `wisteria_scene_add_spot_light` | GUARDED |
| `wisteria_scene_add_torus` | UNGUARDED |
| `wisteria_scene_create` | RAW_TRY |
| `wisteria_scene_destroy` | RAW_TRY |
| `wisteria_scene_instantiate_model` | RAW_TRY |
| `wisteria_scene_load_model` | RAW_TRY |
| `wisteria_scene_set_environment` | UNGUARDED |
| `wisteria_scene_unload_model` | UNGUARDED |
| `wisteria_spot_light_get` | UNGUARDED |
| `wisteria_spot_light_set` | GUARDED |

`native_scene.cpp`：17/48 GUARDED

## native_window.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_poll_and_render` | RAW_TRY |
| `wisteria_window_camera_pose` | UNGUARDED |
| `wisteria_window_create` | RAW_TRY |
| `wisteria_window_create_hidden` | RAW_TRY |
| `wisteria_window_cursor_delta` | UNGUARDED |
| `wisteria_window_destroy` | RAW_TRY |
| `wisteria_window_framebuffer_size` | UNGUARDED |
| `wisteria_window_is_key_down` | UNGUARDED |
| `wisteria_window_is_mouse_button_down` | UNGUARDED |
| `wisteria_window_load_demo` | RAW_TRY |
| `wisteria_window_poll_and_render` | UNGUARDED |
| `wisteria_window_read_pixels` | UNGUARDED |
| `wisteria_window_scroll_delta` | UNGUARDED |
| `wisteria_window_set_camera` | GUARDED |
| `wisteria_window_set_camera_speed` | UNGUARDED |
| `wisteria_window_set_cursor_captured` | UNGUARDED |
| `wisteria_window_set_render_settings` | UNGUARDED |
| `wisteria_window_should_close` | UNGUARDED |
| `wisteria_window_was_key_pressed` | UNGUARDED |
| `wisteria_window_was_key_released` | UNGUARDED |

`native_window.cpp`：1/20 GUARDED

## 生成

```bash
python script/gen_abi_safety_matrix.py
```

