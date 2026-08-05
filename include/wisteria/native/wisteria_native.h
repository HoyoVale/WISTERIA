/*
 * WISTERIA native C ABI (design draft v0.1)
 *
 * Stable, platform-neutral facade for frontend/FFI integration. The first
 * milestone wraps the headless Saba runtime (model/motion/physics/frame
 * stepping); rendering and app-level commands land in a later layer.
 *
 * Threading contract: a WisteriaContext is single-threaded. All functions
 * taking the same context must be called from one thread at a time. Callers
 * that need cross-thread control must serialize through their own queue.
 */
#ifndef WISTERIA_NATIVE_H_
#define WISTERIA_NATIVE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WISTERIA_NATIVE_VERSION_MAJOR 0
#define WISTERIA_NATIVE_VERSION_MINOR 6

#if defined(_WIN32)
#  if defined(WISTERIA_NATIVE_BUILD)
#    define WISTERIA_API __declspec(dllexport)
#  else
#    define WISTERIA_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define WISTERIA_API __attribute__((visibility("default")))
#else
#  define WISTERIA_API
#endif

typedef uint64_t WisteriaContext;
typedef uint64_t WisteriaModel;
typedef uint64_t WisteriaMotion;
typedef uint64_t WisteriaWindow;
typedef uint64_t WisteriaScene;
typedef uint64_t WisteriaSceneModel;
typedef uint64_t WisteriaEntity;
typedef uint64_t WisteriaLight;

enum WisteriaStatus
{
    WISTERIA_OK = 0,
    WISTERIA_ERROR_INVALID_ARGUMENT = 1,
    WISTERIA_ERROR_NOT_FOUND = 2,
    WISTERIA_ERROR_IO = 3,
    WISTERIA_ERROR_PARSE = 4,
    WISTERIA_ERROR_INITIALIZATION = 5,
    WISTERIA_ERROR_ALREADY_EXISTS = 6,
    WISTERIA_ERROR_INTERNAL = 7
};

struct WisteriaVertexBounds
{
    int32_t finite;
    float minimum[3];
    float maximum[3];
    float maximumDisplacementFromBind;
    uint64_t vertexCount;
};

enum WisteriaKey
{
    WISTERIA_KEY_W = 0,
    WISTERIA_KEY_A = 1,
    WISTERIA_KEY_S = 2,
    WISTERIA_KEY_D = 3,
    WISTERIA_KEY_Q = 4,
    WISTERIA_KEY_E = 5,
    WISTERIA_KEY_LEFT_SHIFT = 6,
    WISTERIA_KEY_ESCAPE = 7,
    WISTERIA_KEY_R = 8,
    WISTERIA_KEY_P = 9,
    WISTERIA_KEY_B = 10,
    WISTERIA_KEY_L = 11,
    WISTERIA_KEY_V = 12,
    WISTERIA_KEY_M = 13,
    WISTERIA_KEY_C = 14,
    WISTERIA_KEY_G = 15,
    WISTERIA_KEY_H = 16,
    WISTERIA_KEY_F3 = 17,
    WISTERIA_KEY_SPACE = 18,
    WISTERIA_KEY_LEFT = 19,
    WISTERIA_KEY_RIGHT = 20,
    WISTERIA_KEY_COUNT = 21
};

enum WisteriaMouseButton
{
    WISTERIA_MOUSE_LEFT = 0,
    WISTERIA_MOUSE_RIGHT = 1,
    WISTERIA_MOUSE_MIDDLE = 2,
    WISTERIA_MOUSE_COUNT = 3
};

WISTERIA_API const char* wisteria_status_name(
    enum WisteriaStatus status
);

WISTERIA_API uint32_t wisteria_version_major(void);
WISTERIA_API uint32_t wisteria_version_minor(void);

/* --- Context lifecycle ------------------------------------------------ */

WISTERIA_API enum WisteriaStatus wisteria_create_context(
    WisteriaContext* out_context
);

WISTERIA_API enum WisteriaStatus wisteria_destroy_context(
    WisteriaContext context
);

WISTERIA_API enum WisteriaStatus wisteria_last_error_message(
    WisteriaContext context,
    char* buffer,
    size_t buffer_size
);

/* --- Model lifecycle -------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_load_model(
    WisteriaContext context,
    const char* model_path,
    WisteriaModel* out_model
);

WISTERIA_API enum WisteriaStatus wisteria_unload_model(
    WisteriaContext context,
    WisteriaModel model
);

/* --- Motion control --------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_load_motion(
    WisteriaContext context,
    WisteriaModel model,
    const char* vmd_path,
    WisteriaMotion* out_motion
);

WISTERIA_API enum WisteriaStatus wisteria_unload_motion(
    WisteriaContext context,
    WisteriaModel model,
    WisteriaMotion motion
);

WISTERIA_API enum WisteriaStatus wisteria_play_motion(
    WisteriaContext context,
    WisteriaModel model,
    WisteriaMotion motion
);

WISTERIA_API enum WisteriaStatus wisteria_pause_motion(
    WisteriaContext context,
    WisteriaModel model
);

WISTERIA_API enum WisteriaStatus wisteria_resume_motion(
    WisteriaContext context,
    WisteriaModel model
);

WISTERIA_API enum WisteriaStatus wisteria_set_motion_looping(
    WisteriaContext context,
    WisteriaModel model,
    int32_t looping
);

WISTERIA_API enum WisteriaStatus wisteria_set_motion_frame(
    WisteriaContext context,
    WisteriaModel model,
    double frame
);

WISTERIA_API enum WisteriaStatus wisteria_motion_frame(
    WisteriaContext context,
    WisteriaModel model,
    double* out_frame
);

WISTERIA_API enum WisteriaStatus wisteria_motion_max_frame(
    WisteriaContext context,
    WisteriaModel model,
    double* out_max_frame
);

/* --- Frame stepping --------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_update(
    WisteriaContext context,
    WisteriaModel model,
    float delta_time
);

/* --- Physics settings ------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_set_physics_settings(
    WisteriaContext context,
    WisteriaModel model,
    float fixed_time_step,
    int32_t max_sub_steps,
    float gravity_x,
    float gravity_y,
    float gravity_z
);

/* --- Diagnostics ------------------------------------------------------ */

WISTERIA_API enum WisteriaStatus wisteria_vertex_bounds(
    WisteriaContext context,
    WisteriaModel model,
    struct WisteriaVertexBounds* out_bounds
);

/* --- Window (M4): native desktop window driven by the frontend ---------- */

WISTERIA_API enum WisteriaStatus wisteria_window_create(
    WisteriaContext context,
    int width,
    int height,
    const char* title,
    WisteriaWindow* out_window
);

WISTERIA_API enum WisteriaStatus wisteria_window_destroy(
    WisteriaContext context,
    WisteriaWindow window
);

/*
 * physics_fps is a frequency in Hz (for example 120.0f), not a fixed
 * timestep. Pass 0 to use the runtime/environment default.
 */
WISTERIA_API enum WisteriaStatus wisteria_window_load_demo(
    WisteriaContext context,
    WisteriaWindow window,
    const char* model_path,
    const char* motion_path,
    const char* scene_path,
    float physics_fps,
    int32_t max_sub_steps
);

/*
 * Advances input, scene simulation and rendering for every window owned by
 * this context. Call exactly once per frontend frame; delta_time is seconds.
 */
WISTERIA_API enum WisteriaStatus wisteria_poll_and_render(
    WisteriaContext context,
    float delta_time
);

/*
 * Compatibility wrapper retained for ABI v0.2 callers. The window is only
 * validated; the frame step is context-wide, not window-local.
 */
WISTERIA_API enum WisteriaStatus wisteria_window_poll_and_render(
    WisteriaContext context,
    WisteriaWindow window,
    float delta_time
);

WISTERIA_API enum WisteriaStatus wisteria_window_should_close(
    WisteriaContext context,
    WisteriaWindow window,
    int32_t* out_closed
);

/* --- Window input ------------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_window_is_key_down(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaKey key,
    int32_t* out_down
);

WISTERIA_API enum WisteriaStatus wisteria_window_was_key_pressed(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaKey key,
    int32_t* out_pressed
);

WISTERIA_API enum WisteriaStatus wisteria_window_was_key_released(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaKey key,
    int32_t* out_released
);

WISTERIA_API enum WisteriaStatus wisteria_window_is_mouse_button_down(
    WisteriaContext context,
    WisteriaWindow window,
    enum WisteriaMouseButton button,
    int32_t* out_down
);

WISTERIA_API enum WisteriaStatus wisteria_window_cursor_delta(
    WisteriaContext context,
    WisteriaWindow window,
    float* out_x,
    float* out_y
);

WISTERIA_API enum WisteriaStatus wisteria_window_scroll_delta(
    WisteriaContext context,
    WisteriaWindow window,
    float* out_y
);

WISTERIA_API enum WisteriaStatus wisteria_window_set_cursor_captured(
    WisteriaContext context,
    WisteriaWindow window,
    int32_t captured
);

/* --- Window camera ------------------------------------------------------ */

WISTERIA_API enum WisteriaStatus wisteria_window_set_camera(
    WisteriaContext context,
    WisteriaWindow window,
    const float position[3],
    const float target[3],
    const float up[3]
);

WISTERIA_API enum WisteriaStatus wisteria_window_camera_pose(
    WisteriaContext context,
    WisteriaWindow window,
    float out_position[3],
    float out_target[3],
    float out_up[3]
);

WISTERIA_API enum WisteriaStatus wisteria_window_set_camera_speed(
    WisteriaContext context,
    WisteriaWindow window,
    float move_speed
);

/* --- Window render settings --------------------------------------------- */

/*
 * Per-window renderer configuration. Field semantics follow the "0 / -1
 * keeps the current value" convention so frontends can update one knob
 * without re-sending the whole state:
 *   shadow_map_size      0 or 256..4096
 *   shadow_pcf_radius    0 or 1..3
 *   shadows_enabled      -1 keep, 0 off, 1 on
 *   ground_shadow_enabled -1 keep, 0 off, 1 on
 *   shadow_bias          <0 keep, otherwise >= 0 (MMD CSM depth bias)
 */
struct WisteriaRenderSettings
{
    int32_t shadow_map_size;
    int32_t shadow_pcf_radius;
    int32_t shadows_enabled;
    int32_t ground_shadow_enabled;
    float shadow_bias;
    int32_t reserved[4];
};

WISTERIA_API enum WisteriaStatus wisteria_window_set_render_settings(
    WisteriaContext context,
    WisteriaWindow window,
    const struct WisteriaRenderSettings* settings
);

/* --- MMD control --------------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_set_mmd_ik_enabled(
    WisteriaContext context,
    WisteriaModel model,
    uint32_t bone_index,
    int32_t enabled
);

WISTERIA_API enum WisteriaStatus wisteria_find_bone_index(
    WisteriaContext context,
    WisteriaModel model,
    const char* bone_name,
    uint32_t* out_bone_index
);

WISTERIA_API enum WisteriaStatus wisteria_load_camera_motion(
    WisteriaContext context,
    WisteriaModel model,
    const char* vmd_path
);

/* --- Physics capability query -------------------------------------------- */

/*
 * Reports which physics preset knobs the engine actually implements on the
 * current runtime. Only advertised capabilities may be passed to the
 * physics settings API; mode/damping/CCD/semantic-filter bits are reserved
 * until the community compatibility matrix (#5) defines their semantics.
 */
#define WISTERIA_PHYSICS_CAP_FIXED_STEP (1u << 0)
#define WISTERIA_PHYSICS_CAP_GRAVITY    (1u << 1)
#define WISTERIA_PHYSICS_CAP_ENABLED    (1u << 2)

WISTERIA_API enum WisteriaStatus wisteria_physics_capabilities(
    WisteriaContext context,
    WisteriaModel model,
    uint32_t* out_capabilities
);

/*
 * saba's real physics surface: fixed step, max substeps, gravity and the
 * per-model activation switch. All fields are required and validated.
 */
struct WisteriaPhysicsPreset
{
    float fixed_time_step;
    int32_t max_sub_steps;
    float gravity[3];
    int32_t physics_enabled;
    int32_t reserved[8];
};

WISTERIA_API enum WisteriaStatus wisteria_set_physics_preset(
    WisteriaContext context,
    WisteriaModel model,
    const struct WisteriaPhysicsPreset* preset
);

WISTERIA_API enum WisteriaStatus wisteria_physics_reset(
    WisteriaContext context,
    WisteriaModel model
);

/* --- Self-built scenes ---------------------------------------------------- */

/*
 * Frontend-controlled scene: create an empty scene bound to a window (this
 * replaces the demo composition), load models (PMX via Saba, or OBJ/glTF via
 * assimp), instantiate entities with transforms, control visibility, add
 * lights, and drive rendering with wisteria_poll_and_render as usual.
 * Euler angles are in degrees (engine Transform convention). All handles are
 * single-threaded per context, like the rest of the ABI.
 */

WISTERIA_API enum WisteriaStatus wisteria_scene_create(
    WisteriaContext context,
    WisteriaWindow window,
    WisteriaScene* out_scene
);

WISTERIA_API enum WisteriaStatus wisteria_scene_destroy(
    WisteriaContext context,
    WisteriaScene scene
);

WISTERIA_API enum WisteriaStatus wisteria_scene_load_model(
    WisteriaContext context,
    WisteriaScene scene,
    const char* model_path,
    WisteriaSceneModel* out_model
);

WISTERIA_API enum WisteriaStatus wisteria_scene_unload_model(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaSceneModel model
);

WISTERIA_API enum WisteriaStatus wisteria_scene_instantiate_model(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaSceneModel model,
    const float position[3],
    const float euler_degrees[3],
    const float scale[3],
    WisteriaEntity* out_entity
);

WISTERIA_API enum WisteriaStatus wisteria_entity_set_transform(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const float position[3],
    const float euler_degrees[3],
    const float scale[3]
);

WISTERIA_API enum WisteriaStatus wisteria_entity_get_transform(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    float out_position[3],
    float out_euler_degrees[3],
    float out_scale[3]
);

WISTERIA_API enum WisteriaStatus wisteria_entity_get_visible(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t* out_visible
);

/*
 * Replaces the material of one render part of an entity with a solid-color
 * PBR material (cached per color). part_index selects which mesh part to
 * swap; use it to re-skin primitives or imported models at runtime.
 */
WISTERIA_API enum WisteriaStatus wisteria_entity_set_part_color(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t part_index,
    const float color[3]
);

WISTERIA_API enum WisteriaStatus wisteria_entity_set_visible(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    int32_t visible
);

WISTERIA_API enum WisteriaStatus wisteria_entity_destroy(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_directional_light(
    WisteriaContext context,
    WisteriaScene scene,
    const float direction[3],
    const float color[3],
    float intensity,
    WisteriaLight* out_light
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_point_light(
    WisteriaContext context,
    WisteriaScene scene,
    const float position[3],
    const float color[3],
    float intensity,
    float range,
    WisteriaLight* out_light
);

/* --- Light control --------------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_light_destroy(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light
);

WISTERIA_API enum WisteriaStatus wisteria_directional_light_set(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    const float direction[3],
    const float color[3],
    float intensity
);

WISTERIA_API enum WisteriaStatus wisteria_directional_light_get(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    float out_direction[3],
    float out_color[3],
    float* out_intensity
);

WISTERIA_API enum WisteriaStatus wisteria_point_light_set(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    const float position[3],
    const float color[3],
    float intensity,
    float range
);

WISTERIA_API enum WisteriaStatus wisteria_point_light_get(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    float out_position[3],
    float out_color[3],
    float* out_intensity,
    float* out_range
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_spot_light(
    WisteriaContext context,
    WisteriaScene scene,
    const float position[3],
    const float direction[3],
    const float color[3],
    float intensity,
    float range,
    float inner_cutoff_degrees,
    float outer_cutoff_degrees,
    WisteriaLight* out_light
);

WISTERIA_API enum WisteriaStatus wisteria_spot_light_set(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    const float position[3],
    const float direction[3],
    const float color[3],
    float intensity,
    float range,
    float inner_cutoff_degrees,
    float outer_cutoff_degrees
);

WISTERIA_API enum WisteriaStatus wisteria_spot_light_get(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaLight light,
    float out_position[3],
    float out_direction[3],
    float out_color[3],
    float* out_intensity,
    float* out_range,
    float* out_inner_cutoff_degrees,
    float* out_outer_cutoff_degrees
);

/* --- Entity morphs --------------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_entity_set_morph_weight(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const char* morph_name,
    float weight
);

WISTERIA_API enum WisteriaStatus wisteria_entity_get_morph_weight(
    WisteriaContext context,
    WisteriaScene scene,
    WisteriaEntity entity,
    const char* morph_name,
    float* out_weight
);

/* --- Scene environment ----------------------------------------------------- */

/*
 * Enables/disables the procedural skybox and optionally sets its intensity
 * (pass intensity < 0 to keep the current value).
 */
WISTERIA_API enum WisteriaStatus wisteria_scene_set_environment(
    WisteriaContext context,
    WisteriaScene scene,
    int32_t skybox_enabled,
    float intensity
);

/* --- Scene primitives ------------------------------------------------------ */

WISTERIA_API enum WisteriaStatus wisteria_scene_add_cube(
    WisteriaContext context,
    WisteriaScene scene,
    float size,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_ground_plane(
    WisteriaContext context,
    WisteriaScene scene,
    float size,
    const float position[3],
    WisteriaEntity* out_entity
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_sphere(
    WisteriaContext context,
    WisteriaScene scene,
    float radius,
    int32_t stacks,
    int32_t slices,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_cylinder(
    WisteriaContext context,
    WisteriaScene scene,
    float radius,
    float height,
    int32_t segments,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_capsule(
    WisteriaContext context,
    WisteriaScene scene,
    float radius,
    float height,
    int32_t segments,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_cone(
    WisteriaContext context,
    WisteriaScene scene,
    float radius,
    float height,
    int32_t segments,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
);

WISTERIA_API enum WisteriaStatus wisteria_scene_add_torus(
    WisteriaContext context,
    WisteriaScene scene,
    float major_radius,
    float minor_radius,
    int32_t major_segments,
    int32_t minor_segments,
    const float color[3],
    const float position[3],
    WisteriaEntity* out_entity
);

/* --- Render readback ------------------------------------------------------- */

WISTERIA_API enum WisteriaStatus wisteria_window_framebuffer_size(
    WisteriaContext context,
    WisteriaWindow window,
    int32_t* out_width,
    int32_t* out_height
);

/*
 * Copies the last rendered scene framebuffer into rgba (RGBA8, row-major,
 * bottom-up, width*height*4 bytes). buffer_size must be at least
 * width*height*4; use wisteria_window_framebuffer_size to size it.
 */
WISTERIA_API enum WisteriaStatus wisteria_window_read_pixels(
    WisteriaContext context,
    WisteriaWindow window,
    unsigned char* rgba,
    size_t buffer_size
);

WISTERIA_API enum WisteriaStatus wisteria_window_create_hidden(
    WisteriaContext context,
    int width,
    int height,
    WisteriaWindow* out_window
);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WISTERIA_NATIVE_H_ */
