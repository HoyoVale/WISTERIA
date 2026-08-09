/*
 * WISTERIA Stable Runtime C ABI v1 subset (R1.4 Phase 0A).
 *
 * Frozen contract:
 *   docs/architecture/R1_4_STABLE_RUNTIME_BOUNDARY_CONTRACT.md
 *
 * Only the declarations below are ABI-stable. The legacy v0.7 surface
 * (wisteria_native.h) remains experimental and is NOT covered by this
 * header.
 *
 * Stability rules:
 *   - every extensible public struct carries struct_size + struct_version;
 *   - status / capability / backend / profile identifiers are fixed-width
 *     integers with fixed numeric constants (no C enum ABI size);
 *   - Context is creator-thread-affine: all stable calls for a Context must
 *     run on the thread that created it. This is a caller precondition, not
 *     a runtime-enforced invariant (no owner-thread rejection in v1);
 *   - status code is the authoritative result; last_error is a best-effort
 *     sticky human-readable diagnostic (successful calls do not clear it).
 */
#ifndef WISTERIA_NATIVE_WISTERIA_STABLE_RUNTIME_H_
#define WISTERIA_NATIVE_WISTERIA_STABLE_RUNTIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(WISTERIA_NATIVE_BUILD)
#    define WISTERIA_STABLE_API __declspec(dllexport)
#  else
#    define WISTERIA_STABLE_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define WISTERIA_STABLE_API __attribute__((visibility("default")))
#else
#  define WISTERIA_STABLE_API
#endif

/* --- version layering (contract §2A) -------------------------------- */

#define WISTERIA_STABLE_RUNTIME_ABI_VERSION 1u
#define WISTERIA_CHECKPOINT_WIRE_VERSION 1u
#define WISTERIA_CHECKPOINT_PAYLOAD_KIND_MMD_R12C 1u
#define WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_MMD_R12C 1u
#define WISTERIA_DETERMINISTIC_PROFILE_COLD_STEP_V1 1u

/* R1.9 Phase 0B: Generic R1.8 backend / payload identity (additive) */
#define WISTERIA_BACKEND_ID_WISTERIA_GENERIC 2u
#define WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18 2u
#define WISTERIA_CHECKPOINT_PAYLOAD_SCHEMA_GENERIC_R18 1u
#define WISTERIA_DETERMINISTIC_PROFILE_GENERIC_V1 2u

/* --- opaque handles --------------------------------------------------- */

typedef uint64_t WisteriaStableContext;
typedef uint64_t WisteriaEntity;
typedef uint64_t WisteriaCheckpoint;

/* --- status codes (fixed numeric mapping, authoritative) ------------- */

/* legacy-compatible base statuses */
#define WISTERIA_STATUS_OK 0u
#define WISTERIA_STATUS_INVALID_ARGUMENT 1u
#define WISTERIA_STATUS_NOT_FOUND 2u
#define WISTERIA_STATUS_IO 3u
#define WISTERIA_STATUS_PARSE 4u
#define WISTERIA_STATUS_INITIALIZATION 5u
#define WISTERIA_STATUS_ALREADY_EXISTS 6u
#define WISTERIA_STATUS_INTERNAL 7u

/* deterministic timeline statuses (mapped from TimelineStatus) */
#define WISTERIA_STATUS_INVALID_CHECKPOINT 8u
#define WISTERIA_STATUS_UNSUPPORTED_REPLAY_PROFILE 9u
#define WISTERIA_STATUS_INVALID_STATE 10u
#define WISTERIA_STATUS_NON_SEQUENTIAL_FRAME 11u
#define WISTERIA_STATUS_DETERMINISM_VIOLATION 12u
#define WISTERIA_STATUS_SNAPSHOT_MISMATCH 13u
#define WISTERIA_STATUS_INVALID_SNAPSHOT 14u
#define WISTERIA_STATUS_POISONED 15u
#define WISTERIA_STATUS_NO_PHYSICS 16u
#define WISTERIA_STATUS_UNSUPPORTED 17u

/* --- backend / semantic profile / capability ids --------------------- */

#define WISTERIA_BACKEND_ID_UNKNOWN 0u
#define WISTERIA_BACKEND_ID_SABA_MMD 1u

#define WISTERIA_PROFILE_ID_RAW 1u
#define WISTERIA_PROFILE_ID_COMMUNITY 2u
#define WISTERIA_PROFILE_ID_ADAPTIVE 3u

#define WISTERIA_CAP_SUPPORTS_DETERMINISTIC_EXACT_FRAME (1u << 0)
#define WISTERIA_CAP_SUPPORTS_SNAPSHOT_CAPTURE (1u << 1)
#define WISTERIA_CAP_SUPPORTS_SNAPSHOT_RESTORE (1u << 2)
#define WISTERIA_CAP_SUPPORTS_CHECKPOINT_CAPTURE (1u << 3)
#define WISTERIA_CAP_SUPPORTS_CHECKPOINT_RESTORE (1u << 4)
#define WISTERIA_CAP_SUPPORTS_REPLAY_FROM_CHECKPOINT (1u << 5)
#define WISTERIA_CAP_SUPPORTS_CHECKPOINT_SERIALIZATION (1u << 6)

/* --- versioned structs ----------------------------------------------- */

/* WISTERIA_STABLE_CONTEXT_INFO_V1 */
typedef struct WisteriaStableContextInfoV1
{
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t abi_version;
    uint32_t reserved[8];
} WisteriaStableContextInfoV1;

/* WISTERIA_RUNTIME_CAPABILITIES_V1 */
typedef struct WisteriaRuntimeCapabilitiesV1
{
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t capability_flags;       /* WISTERIA_CAP_* */
    uint32_t runtime_backend_id;
    uint32_t runtime_backend_version;
    uint32_t deterministic_profile_id;
    uint32_t checkpoint_payload_kind;  /* WISTERIA_CHECKPOINT_PAYLOAD_KIND_* */
    uint64_t structural_frame_limit;      /* guard: UINT64_MAX / 4 */
    uint64_t max_deterministic_motion_frame; /* backend-advertised exact domain */
    uint32_t reserved2[4];
} WisteriaRuntimeCapabilitiesV1;

/* WISTERIA_RUNTIME_CREATION_OPTIONS_V1 */
typedef struct WisteriaRuntimeCreationOptionsV1
{
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t compatibility;          /* WISTERIA_PROFILE_ID_* */
    uint32_t reserved;
    float fixed_time_step;
    int32_t max_sub_steps;
    float gravity[3];
    int32_t physics_enabled;
    uint32_t reserved2[4];
} WisteriaRuntimeCreationOptionsV1;

/* WISTERIA_CHECKPOINT_INFO_V1 */
typedef struct WisteriaCheckpointInfoV1
{
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t wire_version;
    uint32_t payload_schema;
    uint32_t payload_kind;
    uint32_t reserved;
    uint64_t build_compatibility_id; /* engine-owned identity; uint64 */
    uint64_t payload_size;
    uint64_t frame;                  /* MotionFrameIndex */
    uint64_t physics_tick;           /* TimelineTick */
    uint32_t reserved2[2];
} WisteriaCheckpointInfoV1;

/* --- stable function surface (declarations; implementation follows) --- */

WISTERIA_STABLE_API uint32_t wisteria_stable_context_create(
    WisteriaStableContext* out_context);

WISTERIA_STABLE_API uint32_t wisteria_stable_context_destroy(
    WisteriaStableContext context);

WISTERIA_STABLE_API uint32_t wisteria_stable_context_info(
    WisteriaStableContext context,
    WisteriaStableContextInfoV1* info);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_create(
    WisteriaStableContext context,
    const WisteriaRuntimeCreationOptionsV1* options,
    const char* model_path_utf8,
    WisteriaEntity* out_entity);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_destroy(
    WisteriaStableContext context,
    WisteriaEntity entity);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_capabilities(
    WisteriaStableContext context,
    WisteriaEntity entity,
    WisteriaRuntimeCapabilitiesV1* capabilities);

/* R1.9 Phase 0B: persistent morph overrides (backend-neutral) */
WISTERIA_STABLE_API uint32_t wisteria_stable_entity_set_morph_override(
    WisteriaStableContext context,
    WisteriaEntity entity,
    const char* morph_name_utf8,
    float weight);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_clear_morph_override(
    WisteriaStableContext context,
    WisteriaEntity entity,
    const char* morph_name_utf8);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_clear_all_morph_overrides(
    WisteriaStableContext context,
    WisteriaEntity entity);

/* R1.9 Phase 0B: immutable asset identity (ModelAsset fingerprint) */
WISTERIA_STABLE_API uint32_t wisteria_stable_entity_asset_fingerprint(
    WisteriaStableContext context,
    WisteriaEntity entity,
    uint64_t* out_fingerprint);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_load_motion(
    WisteriaStableContext context,
    WisteriaEntity entity,
    const char* vmd_path_utf8);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_unload_motion(
    WisteriaStableContext context,
    WisteriaEntity entity);

/* deterministic timeline (uint64 canonical frames) */
WISTERIA_STABLE_API uint32_t wisteria_stable_entity_prepare_frame_zero(
    WisteriaStableContext context,
    WisteriaEntity entity);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_step_exact(
    WisteriaStableContext context,
    WisteriaEntity entity,
    uint64_t frame);

WISTERIA_STABLE_API uint32_t wisteria_stable_entity_replay_exact(
    WisteriaStableContext context,
    WisteriaEntity entity,
    uint64_t target);

/* interactive preview (double frame; never mixed with exact timeline) */
WISTERIA_STABLE_API uint32_t wisteria_stable_entity_set_preview_frame(
    WisteriaStableContext context,
    WisteriaEntity entity,
    double frame);

/* checkpoint lifecycle: Context-owned opaque value objects */
WISTERIA_STABLE_API uint32_t wisteria_stable_checkpoint_create(
    WisteriaStableContext context,
    WisteriaEntity entity,
    WisteriaCheckpoint* out_checkpoint);

WISTERIA_STABLE_API uint32_t wisteria_stable_checkpoint_restore(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint,
    WisteriaEntity entity);

WISTERIA_STABLE_API uint32_t wisteria_stable_checkpoint_destroy(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint);

WISTERIA_STABLE_API uint32_t wisteria_stable_checkpoint_info(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint,
    WisteriaCheckpointInfoV1* info);

/* checkpoint serialization (portable bytes, build-gated semantics) */
WISTERIA_STABLE_API uint32_t wisteria_stable_checkpoint_serialize(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint,
    uint8_t* bytes,
    uint64_t* in_out_size);

WISTERIA_STABLE_API uint32_t wisteria_stable_checkpoint_deserialize(
    WisteriaStableContext context,
    const uint8_t* bytes,
    uint64_t size,
    WisteriaCheckpoint* out_checkpoint);

/* best-effort sticky human-readable diagnostic; do not parse program logic */
WISTERIA_STABLE_API const char* wisteria_stable_last_error(
    WisteriaStableContext context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WISTERIA_NATIVE_WISTERIA_STABLE_RUNTIME_H_ */
