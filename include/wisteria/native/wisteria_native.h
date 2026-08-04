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
#define WISTERIA_NATIVE_VERSION_MINOR 3

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WISTERIA_NATIVE_H_ */
