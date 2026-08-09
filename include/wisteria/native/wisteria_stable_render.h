#ifndef WISTERIA_NATIVE_WISTERIA_STABLE_RENDER_H_
#define WISTERIA_NATIVE_WISTERIA_STABLE_RENDER_H_

/*
 * R1.9 Phase 0D: Stable Render C ABI.
 *
 * Same WISTERIA library, same Context/error/handle conventions as
 * wisteria_stable_runtime.h. This header is a separate ABI domain so future
 * R2 render changes do not touch the runtime ABI version.
 *
 * Exposed surface (Decision 4):
 *   - offline/headless render session
 *   - single-frame RenderOffline -> RGBA8 (caller buffer, size query)
 *   - OfflineFrameSequence RenderRange / Resume
 * No RenderDevice / RenderGraph / native EGL surface is exposed.
 */

#include "wisteria/native/wisteria_stable_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WISTERIA_STABLE_RENDER_ABI_VERSION 1u

typedef uint64_t WisteriaRenderSession;

/* WISTERIA_RENDER_SESSION_OPTIONS_V1 */
typedef struct WisteriaRenderSessionOptionsV1
{
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t force_software;   /* strict: must be a software renderer */
    uint32_t reserved[4];
} WisteriaRenderSessionOptionsV1;

/* WISTERIA_RENDER_CAMERA_V1 (explicit presentation authority) */
typedef struct WisteriaRenderCameraV1
{
    uint32_t struct_size;
    uint32_t struct_version;
    float position[3];
    float target[3];
    float up[3];
    float vertical_fov_degrees;
    float near_clip;
    float far_clip;
    uint32_t reserved[4];
} WisteriaRenderCameraV1;

/* WISTERIA_SEQUENCE_OPTIONS_V1 */
typedef struct WisteriaSequenceOptionsV1
{
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t start_frame;
    uint64_t end_frame;
    uint32_t width;
    uint32_t height;
    uint32_t overwrite_policy; /* 0=Reject 1=Overwrite 2=VerifySkip */
    uint32_t write_png;
    uint32_t write_raw;
    uint32_t reserved[4];
} WisteriaSequenceOptionsV1;

WISTERIA_STABLE_API uint32_t wisteria_stable_render_session_create(
    WisteriaStableContext context,
    const WisteriaRenderSessionOptionsV1* options,
    WisteriaRenderSession* out_session);

WISTERIA_STABLE_API uint32_t wisteria_stable_render_session_destroy(
    WisteriaStableContext context,
    WisteriaRenderSession session);

/* Single-frame RenderOffline of the entity's EXACT runtime state.
 * size query: rgba == NULL -> required bytes in *in_out_size.
 * fill mode: *in_out_size >= required -> canonical top-left RGBA8. */
WISTERIA_STABLE_API uint32_t wisteria_stable_render_session_render(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    WisteriaEntity entity,
    const WisteriaRenderCameraV1* camera,
    uint32_t width,
    uint32_t height,
    uint8_t* rgba,
    uint64_t* in_out_size);

/* Deterministic frame sequence: from-start [start,end] inclusive. */
WISTERIA_STABLE_API uint32_t wisteria_stable_render_session_sequence_range(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    WisteriaEntity entity,
    const WisteriaRenderCameraV1* camera,
    const char* output_dir_utf8,
    const WisteriaSequenceOptionsV1* options,
    uint64_t* out_last_committed);

/* Deterministic frame sequence: resume to end from committed manifest. */
WISTERIA_STABLE_API uint32_t wisteria_stable_render_session_sequence_resume(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    WisteriaEntity entity,
    const WisteriaRenderCameraV1* camera,
    const char* output_dir_utf8,
    const WisteriaSequenceOptionsV1* options,
    uint64_t* out_last_committed);

WISTERIA_STABLE_API uint32_t
wisteria_stable_render_session_sequence_last_committed(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    uint64_t* out_frame);

WISTERIA_STABLE_API uint32_t
wisteria_stable_render_session_sequence_failed(
    WisteriaStableContext context,
    WisteriaRenderSession session,
    int32_t* out_failed);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WISTERIA_NATIVE_WISTERIA_STABLE_RENDER_H_ */
