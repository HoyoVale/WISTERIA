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
#define WISTERIA_NATIVE_VERSION_MINOR 1

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WISTERIA_NATIVE_H_ */
