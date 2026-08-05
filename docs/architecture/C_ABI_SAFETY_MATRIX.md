# C ABI 安全矩阵

> 本文件由 `script/gen_abi_safety_matrix.py` 自动生成，禁止手改。
> R1.S 应将该脚本接入 CI，防止矩阵随 C API 扩展过期。

## 状态说明

- `INVOKE_ABI`：整个入口（含 Context 查找、句柄校验、路径/文件操作）均位于 `InvokeAbi` 统一异常边界内；
- `GUARDED`：函数体包含 `GuardAbi(context, [&]{ ... })`；
- `RAW_TRY`：有裸 `try/catch`，未统一走 `GuardAbi`；
- `PROVEN_NO_THROW_LEAF`：无状态查询/常量 leaf，可证明不抛；
- `UNGUARDED`：无异常边界。

## 汇总

| 状态 | 数量 |
| ---- | ---- |
| 总计 | 94 |
| INVOKE_ABI | 90 |
| GUARDED | 0 |
| RAW_TRY | 1 |
| PROVEN_NO_THROW_LEAF | 3 |
| UNGUARDED | 0 |

## native_common.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_create_context` | RAW_TRY |
| `wisteria_destroy_context` | INVOKE_ABI |
| `wisteria_last_error_message` | INVOKE_ABI |
| `wisteria_status_name` | PROVEN_NO_THROW_LEAF |
| `wisteria_version_major` | PROVEN_NO_THROW_LEAF |
| `wisteria_version_minor` | PROVEN_NO_THROW_LEAF |

`native_common.cpp`：0/6 GUARDED

## native_model.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_find_bone_index` | INVOKE_ABI |
| `wisteria_load_camera_motion` | INVOKE_ABI |
| `wisteria_load_model` | INVOKE_ABI |
| `wisteria_load_motion` | INVOKE_ABI |
| `wisteria_motion_frame` | INVOKE_ABI |
| `wisteria_motion_max_frame` | INVOKE_ABI |
| `wisteria_pause_motion` | INVOKE_ABI |
| `wisteria_physics_capabilities` | INVOKE_ABI |
| `wisteria_physics_reset` | INVOKE_ABI |
| `wisteria_play_motion` | INVOKE_ABI |
| `wisteria_resume_motion` | INVOKE_ABI |
| `wisteria_set_mmd_ik_enabled` | INVOKE_ABI |
| `wisteria_set_motion_frame` | INVOKE_ABI |
| `wisteria_set_motion_looping` | INVOKE_ABI |
| `wisteria_set_physics_preset` | INVOKE_ABI |
| `wisteria_set_physics_settings` | INVOKE_ABI |
| `wisteria_unload_model` | INVOKE_ABI |
| `wisteria_unload_motion` | INVOKE_ABI |
| `wisteria_update` | INVOKE_ABI |
| `wisteria_vertex_bounds` | INVOKE_ABI |

`native_model.cpp`：0/20 GUARDED

## native_scene.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_directional_light_get` | INVOKE_ABI |
| `wisteria_directional_light_set` | INVOKE_ABI |
| `wisteria_entity_bone_count` | INVOKE_ABI |
| `wisteria_entity_bone_local_matrix` | INVOKE_ABI |
| `wisteria_entity_bone_name` | INVOKE_ABI |
| `wisteria_entity_destroy` | INVOKE_ABI |
| `wisteria_entity_get_morph_weight` | INVOKE_ABI |
| `wisteria_entity_get_transform` | INVOKE_ABI |
| `wisteria_entity_get_visible` | INVOKE_ABI |
| `wisteria_entity_load_motion` | INVOKE_ABI |
| `wisteria_entity_motion_frame` | INVOKE_ABI |
| `wisteria_entity_motion_max_frame` | INVOKE_ABI |
| `wisteria_entity_pause_motion` | INVOKE_ABI |
| `wisteria_entity_physics_reset` | INVOKE_ABI |
| `wisteria_entity_restart_motion` | INVOKE_ABI |
| `wisteria_entity_resume_motion` | INVOKE_ABI |
| `wisteria_entity_runtime_backend` | INVOKE_ABI |
| `wisteria_entity_set_mmd_ik_enabled` | INVOKE_ABI |
| `wisteria_entity_set_morph_weight` | INVOKE_ABI |
| `wisteria_entity_set_motion_frame` | INVOKE_ABI |
| `wisteria_entity_set_motion_looping` | INVOKE_ABI |
| `wisteria_entity_set_part_color` | INVOKE_ABI |
| `wisteria_entity_set_physics_settings` | INVOKE_ABI |
| `wisteria_entity_set_transform` | INVOKE_ABI |
| `wisteria_entity_set_visible` | INVOKE_ABI |
| `wisteria_entity_unload_motion` | INVOKE_ABI |
| `wisteria_entity_vertex_bounds` | INVOKE_ABI |
| `wisteria_light_destroy` | INVOKE_ABI |
| `wisteria_point_light_get` | INVOKE_ABI |
| `wisteria_point_light_set` | INVOKE_ABI |
| `wisteria_scene_add_capsule` | INVOKE_ABI |
| `wisteria_scene_add_cone` | INVOKE_ABI |
| `wisteria_scene_add_cube` | INVOKE_ABI |
| `wisteria_scene_add_cylinder` | INVOKE_ABI |
| `wisteria_scene_add_directional_light` | INVOKE_ABI |
| `wisteria_scene_add_ground_plane` | INVOKE_ABI |
| `wisteria_scene_add_point_light` | INVOKE_ABI |
| `wisteria_scene_add_sphere` | INVOKE_ABI |
| `wisteria_scene_add_spot_light` | INVOKE_ABI |
| `wisteria_scene_add_torus` | INVOKE_ABI |
| `wisteria_scene_create` | INVOKE_ABI |
| `wisteria_scene_destroy` | INVOKE_ABI |
| `wisteria_scene_instantiate_model` | INVOKE_ABI |
| `wisteria_scene_load_model` | INVOKE_ABI |
| `wisteria_scene_set_environment` | INVOKE_ABI |
| `wisteria_scene_unload_model` | INVOKE_ABI |
| `wisteria_spot_light_get` | INVOKE_ABI |
| `wisteria_spot_light_set` | INVOKE_ABI |

`native_scene.cpp`：0/48 GUARDED

## native_window.cpp

| 函数 | 状态 |
| ---- | ---- |
| `wisteria_poll_and_render` | INVOKE_ABI |
| `wisteria_window_camera_pose` | INVOKE_ABI |
| `wisteria_window_create` | INVOKE_ABI |
| `wisteria_window_create_hidden` | INVOKE_ABI |
| `wisteria_window_cursor_delta` | INVOKE_ABI |
| `wisteria_window_destroy` | INVOKE_ABI |
| `wisteria_window_framebuffer_size` | INVOKE_ABI |
| `wisteria_window_is_key_down` | INVOKE_ABI |
| `wisteria_window_is_mouse_button_down` | INVOKE_ABI |
| `wisteria_window_load_demo` | INVOKE_ABI |
| `wisteria_window_poll_and_render` | INVOKE_ABI |
| `wisteria_window_read_pixels` | INVOKE_ABI |
| `wisteria_window_scroll_delta` | INVOKE_ABI |
| `wisteria_window_set_camera` | INVOKE_ABI |
| `wisteria_window_set_camera_speed` | INVOKE_ABI |
| `wisteria_window_set_cursor_captured` | INVOKE_ABI |
| `wisteria_window_set_render_settings` | INVOKE_ABI |
| `wisteria_window_should_close` | INVOKE_ABI |
| `wisteria_window_was_key_pressed` | INVOKE_ABI |
| `wisteria_window_was_key_released` | INVOKE_ABI |

`native_window.cpp`：0/20 GUARDED

## 生成

```bash
python script/gen_abi_safety_matrix.py
```

