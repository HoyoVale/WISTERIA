#!/usr/bin/env python3
"""R1.9 Phase 0E: Stable Runtime/Render C ABI Python ctypes acceptance.

Normative acceptance (Decision 5): the stable ABI must be usable from a
pure-standard-library Python ctypes client with no C++ headers and no pip
dependencies. This script drives the frozen surface end-to-end:

  context -> entity(Generic) -> capabilities -> exact step/replay
  -> checkpoint create/serialize/deserialize/restore
  -> render session -> single-frame RGBA8 -> sequence range
  -> entity(Static) render -> status semantics (NOT_FOUND vs UNSUPPORTED)
  -> last_error diagnostics -> teardown

Exit code 0 = acceptance PASS; non-zero = FAIL with the first failed
assertion printed.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import sys
import tempfile
from pathlib import Path

WISTERIA_STATUS_OK = 0
WISTERIA_STATUS_NOT_FOUND = 2
WISTERIA_STATUS_UNSUPPORTED = 17

WISTERIA_BACKEND_ID_GENERIC = 2
WISTERIA_BACKEND_ID_STATIC = 3
WISTERIA_DETERMINISTIC_PROFILE_GENERIC_V1 = 2
WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18 = 2

WISTERIA_CAP_EXACT_FRAME = 1 << 0
WISTERIA_CAP_CHECKPOINT_CAPTURE = 1 << 3
WISTERIA_CAP_CHECKPOINT_RESTORE = 1 << 4
WISTERIA_CAP_REPLAY_FROM_CHECKPOINT = 1 << 5
WISTERIA_CAP_CHECKPOINT_SERIALIZATION = 1 << 6


class WisteriaRuntimeCreationOptionsV1(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("struct_version", ctypes.c_uint32),
        ("compatibility", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("fixed_time_step", ctypes.c_float),
        ("max_sub_steps", ctypes.c_int32),
        ("gravity", ctypes.c_float * 3),
        ("physics_enabled", ctypes.c_int32),
        ("reserved2", ctypes.c_uint32 * 4),
    ]


class WisteriaStableContextInfoV1(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("struct_version", ctypes.c_uint32),
        ("abi_version", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 8),
    ]


class WisteriaRuntimeCapabilitiesV1(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("struct_version", ctypes.c_uint32),
        ("capability_flags", ctypes.c_uint32),
        ("runtime_backend_id", ctypes.c_uint32),
        ("runtime_backend_version", ctypes.c_uint32),
        ("deterministic_profile_id", ctypes.c_uint32),
        ("checkpoint_payload_kind", ctypes.c_uint32),
        ("structural_frame_limit", ctypes.c_uint64),
        ("max_deterministic_motion_frame", ctypes.c_uint64),
        ("reserved2", ctypes.c_uint32 * 4),
    ]


class WisteriaCheckpointInfoV1(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("struct_version", ctypes.c_uint32),
        ("wire_version", ctypes.c_uint32),
        ("payload_schema", ctypes.c_uint32),
        ("payload_kind", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("build_compatibility_id", ctypes.c_uint64),
        ("payload_size", ctypes.c_uint64),
        ("frame", ctypes.c_uint64),
        ("physics_tick", ctypes.c_uint64),
        ("reserved2", ctypes.c_uint32 * 2),
    ]


class WisteriaRenderSessionOptionsV1(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("struct_version", ctypes.c_uint32),
        ("force_software", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 4),
    ]


class WisteriaRenderCameraV1(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("struct_version", ctypes.c_uint32),
        ("position", ctypes.c_float * 3),
        ("target", ctypes.c_float * 3),
        ("up", ctypes.c_float * 3),
        ("vertical_fov_degrees", ctypes.c_float),
        ("near_clip", ctypes.c_float),
        ("far_clip", ctypes.c_float),
        ("reserved", ctypes.c_uint32 * 4),
    ]


class WisteriaSequenceOptionsV1(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("struct_version", ctypes.c_uint32),
        ("start_frame", ctypes.c_uint64),
        ("end_frame", ctypes.c_uint64),
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("overwrite_policy", ctypes.c_uint32),
        ("write_png", ctypes.c_uint32),
        ("write_raw", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 4),
    ]


U64 = ctypes.c_uint64
U64_PTR = ctypes.POINTER(U64)
U8_PTR = ctypes.POINTER(ctypes.c_uint8)


def make_runtime_options() -> WisteriaRuntimeCreationOptionsV1:
    options = WisteriaRuntimeCreationOptionsV1()
    ctypes.memset(ctypes.byref(options), 0, ctypes.sizeof(options))
    options.struct_size = ctypes.sizeof(options)
    options.struct_version = 1
    options.compatibility = 1  # WISTERIA_PROFILE_ID_RAW
    options.fixed_time_step = 1.0 / 120.0
    options.max_sub_steps = 10
    options.gravity[1] = -98.0
    options.physics_enabled = 1
    return options


def make_camera() -> WisteriaRenderCameraV1:
    camera = WisteriaRenderCameraV1()
    ctypes.memset(ctypes.byref(camera), 0, ctypes.sizeof(camera))
    camera.struct_size = ctypes.sizeof(camera)
    camera.struct_version = 1
    camera.position[1] = 3.0
    camera.position[2] = 3.0
    camera.up[1] = 1.0
    camera.vertical_fov_degrees = 45.0
    camera.near_clip = 0.1
    camera.far_clip = 100.0
    return camera


class StableAbi:
    def __init__(self, lib_path: Path):
        if os.name == "nt":
            os.add_dll_directory(str(lib_path.resolve().parent))
        self.lib = ctypes.CDLL(str(lib_path))

        self.lib.wisteria_stable_context_create.argtypes = [U64_PTR]
        self.lib.wisteria_stable_context_create.restype = ctypes.c_uint32
        self.lib.wisteria_stable_context_destroy.argtypes = [U64]
        self.lib.wisteria_stable_context_destroy.restype = ctypes.c_uint32
        self.lib.wisteria_stable_context_info.argtypes = [
            U64,
            ctypes.POINTER(WisteriaStableContextInfoV1),
        ]
        self.lib.wisteria_stable_context_info.restype = ctypes.c_uint32

        self.lib.wisteria_stable_entity_create.argtypes = [
            U64,
            ctypes.POINTER(WisteriaRuntimeCreationOptionsV1),
            ctypes.c_char_p,
            U64_PTR,
        ]
        self.lib.wisteria_stable_entity_create.restype = ctypes.c_uint32
        self.lib.wisteria_stable_entity_destroy.argtypes = [U64, U64]
        self.lib.wisteria_stable_entity_destroy.restype = ctypes.c_uint32
        self.lib.wisteria_stable_entity_capabilities.argtypes = [
            U64,
            U64,
            ctypes.POINTER(WisteriaRuntimeCapabilitiesV1),
        ]
        self.lib.wisteria_stable_entity_capabilities.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_entity_asset_fingerprint.argtypes = [
            U64,
            U64,
            ctypes.POINTER(ctypes.c_uint64),
        ]
        self.lib.wisteria_stable_entity_asset_fingerprint.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_entity_load_motion.argtypes = [
            U64,
            U64,
            ctypes.c_char_p,
        ]
        self.lib.wisteria_stable_entity_load_motion.restype = ctypes.c_uint32
        self.lib.wisteria_stable_entity_prepare_frame_zero.argtypes = [U64, U64]
        self.lib.wisteria_stable_entity_prepare_frame_zero.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_entity_step_exact.argtypes = [
            U64,
            U64,
            ctypes.c_uint64,
        ]
        self.lib.wisteria_stable_entity_step_exact.restype = ctypes.c_uint32
        self.lib.wisteria_stable_entity_replay_exact.argtypes = [
            U64,
            U64,
            ctypes.c_uint64,
        ]
        self.lib.wisteria_stable_entity_replay_exact.restype = ctypes.c_uint32

        self.lib.wisteria_stable_checkpoint_create.argtypes = [
            U64,
            U64,
            U64_PTR,
        ]
        self.lib.wisteria_stable_checkpoint_create.restype = ctypes.c_uint32
        self.lib.wisteria_stable_checkpoint_restore.argtypes = [U64, U64, U64]
        self.lib.wisteria_stable_checkpoint_restore.restype = ctypes.c_uint32
        self.lib.wisteria_stable_checkpoint_destroy.argtypes = [U64, U64]
        self.lib.wisteria_stable_checkpoint_destroy.restype = ctypes.c_uint32
        self.lib.wisteria_stable_checkpoint_info.argtypes = [
            U64,
            U64,
            ctypes.POINTER(WisteriaCheckpointInfoV1),
        ]
        self.lib.wisteria_stable_checkpoint_info.restype = ctypes.c_uint32
        self.lib.wisteria_stable_checkpoint_serialize.argtypes = [
            U64,
            U64,
            U8_PTR,
            U64_PTR,
        ]
        self.lib.wisteria_stable_checkpoint_serialize.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_checkpoint_deserialize.argtypes = [
            U64,
            U8_PTR,
            ctypes.c_uint64,
            U64_PTR,
        ]
        self.lib.wisteria_stable_checkpoint_deserialize.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_last_error.argtypes = [U64]
        self.lib.wisteria_stable_last_error.restype = ctypes.c_char_p

        self.lib.wisteria_stable_render_session_create.argtypes = [
            U64,
            ctypes.POINTER(WisteriaRenderSessionOptionsV1),
            U64_PTR,
        ]
        self.lib.wisteria_stable_render_session_create.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_render_session_destroy.argtypes = [U64, U64]
        self.lib.wisteria_stable_render_session_destroy.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_render_session_render.argtypes = [
            U64,
            U64,
            U64,
            ctypes.POINTER(WisteriaRenderCameraV1),
            ctypes.c_uint32,
            ctypes.c_uint32,
            U8_PTR,
            U64_PTR,
        ]
        self.lib.wisteria_stable_render_session_render.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_render_session_sequence_range.argtypes = [
            U64,
            U64,
            U64,
            ctypes.POINTER(WisteriaRenderCameraV1),
            ctypes.c_char_p,
            ctypes.POINTER(WisteriaSequenceOptionsV1),
            U64_PTR,
        ]
        self.lib.wisteria_stable_render_session_sequence_range.restype = (
            ctypes.c_uint32
        )
        self.lib.wisteria_stable_render_session_sequence_failed.argtypes = [
            U64,
            U64,
            ctypes.POINTER(ctypes.c_int32),
        ]
        self.lib.wisteria_stable_render_session_sequence_failed.restype = (
            ctypes.c_uint32
        )

    def last_error(self, context: int) -> str:
        raw = self.lib.wisteria_stable_last_error(context)
        return raw.decode("utf-8", "replace") if raw else ""


def check(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}", file=sys.stderr)
        sys.exit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lib", type=Path, required=True)
    parser.add_argument(
        "--generic-model",
        type=Path,
        default=Path("tests/data/animated_triangle.gltf"),
    )
    parser.add_argument(
        "--static-model",
        type=Path,
        default=Path("tests/data/pbr_quad.gltf"),
    )
    parser.add_argument("--out-dir", type=Path, default=None)
    args = parser.parse_args()

    check(
        args.lib.is_file(),
        f"native library not found: {args.lib}",
    )
    check(
        args.generic_model.is_file(),
        f"generic model not found: {args.generic_model}",
    )
    check(
        args.static_model.is_file(),
        f"static model not found: {args.static_model}",
    )

    abi = StableAbi(args.lib)

    # 1. Context + info.
    context = U64(0)
    check(
        abi.lib.wisteria_stable_context_create(ctypes.byref(context))
        == WISTERIA_STATUS_OK
        and context.value != 0,
        "context create",
    )
    info = WisteriaStableContextInfoV1()
    ctypes.memset(ctypes.byref(info), 0, ctypes.sizeof(info))
    info.struct_size = ctypes.sizeof(info)
    info.struct_version = 1
    check(
        abi.lib.wisteria_stable_context_info(
            context, ctypes.byref(info)
        )
        == WISTERIA_STATUS_OK
        and info.abi_version == 1,
        "context info abi_version",
    )

    runtime_options = make_runtime_options()

    # 2. Generic entity + capabilities.
    generic_entity = U64(0)
    check(
        abi.lib.wisteria_stable_entity_create(
            context,
            ctypes.byref(runtime_options),
            str(args.generic_model).encode("utf-8"),
            ctypes.byref(generic_entity),
        )
        == WISTERIA_STATUS_OK
        and generic_entity.value != 0,
        "generic entity create",
    )
    capabilities = WisteriaRuntimeCapabilitiesV1()
    ctypes.memset(ctypes.byref(capabilities), 0, ctypes.sizeof(capabilities))
    capabilities.struct_size = ctypes.sizeof(capabilities)
    capabilities.struct_version = 1
    check(
        abi.lib.wisteria_stable_entity_capabilities(
            context,
            generic_entity,
            ctypes.byref(capabilities),
        )
        == WISTERIA_STATUS_OK,
        "generic capabilities",
    )
    check(
        capabilities.runtime_backend_id == WISTERIA_BACKEND_ID_GENERIC
        and capabilities.deterministic_profile_id
        == WISTERIA_DETERMINISTIC_PROFILE_GENERIC_V1
        and capabilities.checkpoint_payload_kind
        == WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18
        and (capabilities.capability_flags & WISTERIA_CAP_EXACT_FRAME) != 0
        and (capabilities.capability_flags & WISTERIA_CAP_CHECKPOINT_CAPTURE)
        != 0
        and (capabilities.capability_flags & WISTERIA_CAP_CHECKPOINT_RESTORE)
        != 0
        and (capabilities.capability_flags & WISTERIA_CAP_REPLAY_FROM_CHECKPOINT)
        != 0
        and (capabilities.capability_flags & WISTERIA_CAP_CHECKPOINT_SERIALIZATION)
        != 0
        and capabilities.max_deterministic_motion_frame == (1 << 20),
        "generic capability identity",
    )

    fingerprint = ctypes.c_uint64(0)
    check(
        abi.lib.wisteria_stable_entity_asset_fingerprint(
            context,
            generic_entity,
            ctypes.byref(fingerprint),
        )
        == WISTERIA_STATUS_OK
        and fingerprint.value != 0,
        "generic asset fingerprint",
    )

    # 3. Exact stepping + replay.
    check(
        abi.lib.wisteria_stable_entity_prepare_frame_zero(
            context, generic_entity
        )
        == WISTERIA_STATUS_OK,
        "generic prepare_frame_zero",
    )
    check(
        abi.lib.wisteria_stable_entity_step_exact(
            context, generic_entity, 1
        )
        == WISTERIA_STATUS_OK
        and abi.lib.wisteria_stable_entity_step_exact(
            context, generic_entity, 2
        )
        == WISTERIA_STATUS_OK,
        "generic step_exact",
    )
    check(
        abi.lib.wisteria_stable_entity_replay_exact(
            context, generic_entity, 3
        )
        == WISTERIA_STATUS_OK,
        "generic replay_exact",
    )

    # 4. Checkpoint create/info/serialize/deserialize/restore.
    checkpoint = U64(0)
    check(
        abi.lib.wisteria_stable_checkpoint_create(
            context,
            generic_entity,
            ctypes.byref(checkpoint),
        )
        == WISTERIA_STATUS_OK
        and checkpoint.value != 0,
        "checkpoint create",
    )
    checkpoint_info = WisteriaCheckpointInfoV1()
    ctypes.memset(
        ctypes.byref(checkpoint_info), 0, ctypes.sizeof(checkpoint_info)
    )
    checkpoint_info.struct_size = ctypes.sizeof(checkpoint_info)
    checkpoint_info.struct_version = 1
    check(
        abi.lib.wisteria_stable_checkpoint_info(
            context,
            checkpoint,
            ctypes.byref(checkpoint_info),
        )
        == WISTERIA_STATUS_OK
        and checkpoint_info.frame == 3
        and checkpoint_info.payload_kind
        == WISTERIA_CHECKPOINT_PAYLOAD_KIND_GENERIC_R18,
        "checkpoint info",
    )
    wire_size = ctypes.c_uint64(0)
    check(
        abi.lib.wisteria_stable_checkpoint_serialize(
            context,
            checkpoint,
            None,
            ctypes.byref(wire_size),
        )
        == WISTERIA_STATUS_OK
        and wire_size.value > 0,
        "checkpoint serialize size query",
    )
    wire = (ctypes.c_uint8 * wire_size.value)()
    check(
        abi.lib.wisteria_stable_checkpoint_serialize(
            context,
            checkpoint,
            wire,
            ctypes.byref(wire_size),
        )
        == WISTERIA_STATUS_OK,
        "checkpoint serialize",
    )
    decoded = U64(0)
    check(
        abi.lib.wisteria_stable_checkpoint_deserialize(
            context,
            wire,
            wire_size.value,
            ctypes.byref(decoded),
        )
        == WISTERIA_STATUS_OK
        and decoded.value != 0,
        "checkpoint deserialize",
    )

    # Fresh process-side entity receives the deserialized checkpoint.
    restored_entity = U64(0)
    check(
        abi.lib.wisteria_stable_entity_create(
            context,
            ctypes.byref(runtime_options),
            str(args.generic_model).encode("utf-8"),
            ctypes.byref(restored_entity),
        )
        == WISTERIA_STATUS_OK,
        "restored entity create",
    )
    check(
        abi.lib.wisteria_stable_checkpoint_restore(
            context,
            decoded,
            restored_entity,
        )
        == WISTERIA_STATUS_OK,
        "checkpoint restore",
    )
    check(
        abi.lib.wisteria_stable_entity_step_exact(
            context, restored_entity, 4
        )
        == WISTERIA_STATUS_OK,
        "continuation after restore",
    )
    check(
        abi.lib.wisteria_stable_checkpoint_destroy(context, decoded)
        == WISTERIA_STATUS_OK
        and abi.lib.wisteria_stable_checkpoint_destroy(context, checkpoint)
        == WISTERIA_STATUS_OK,
        "checkpoint destroy",
    )

    # 5. Render session: single-frame RGBA8 must contain pixels.
    session_options = WisteriaRenderSessionOptionsV1()
    ctypes.memset(ctypes.byref(session_options), 0, ctypes.sizeof(session_options))
    session_options.struct_size = ctypes.sizeof(session_options)
    session_options.struct_version = 1
    render_session = U64(0)
    check(
        abi.lib.wisteria_stable_render_session_create(
            context,
            ctypes.byref(session_options),
            ctypes.byref(render_session),
        )
        == WISTERIA_STATUS_OK
        and render_session.value != 0,
        "render session create",
    )
    camera = make_camera()
    width = 64
    height = 64
    byte_count = width * height * 4
    frame_buffer = (ctypes.c_uint8 * byte_count)()
    in_out_size = ctypes.c_uint64(byte_count)
    check(
        abi.lib.wisteria_stable_render_session_render(
            context,
            render_session,
            generic_entity,
            ctypes.byref(camera),
            width,
            height,
            frame_buffer,
            ctypes.byref(in_out_size),
        )
        == WISTERIA_STATUS_OK
        and in_out_size.value == byte_count,
        "generic single-frame render",
    )
    check(
        any(value != 0 for value in frame_buffer),
        "generic render frame is all zero",
    )

    # 6. Sequence range writes PNG + manifest.
    sequence_options = WisteriaSequenceOptionsV1()
    ctypes.memset(ctypes.byref(sequence_options), 0, ctypes.sizeof(sequence_options))
    sequence_options.struct_size = ctypes.sizeof(sequence_options)
    sequence_options.struct_version = 1
    sequence_options.start_frame = 0
    sequence_options.end_frame = 1
    sequence_options.width = 16
    sequence_options.height = 16
    sequence_options.overwrite_policy = 0
    sequence_options.write_png = 1
    sequence_options.write_raw = 0
    last_committed = ctypes.c_uint64(99)
    output_dir = args.out_dir or Path(
        tempfile.mkdtemp(prefix="wisteria_r19_ctypes_")
    )
    check(
        abi.lib.wisteria_stable_render_session_sequence_range(
            context,
            render_session,
            generic_entity,
            ctypes.byref(camera),
            str(output_dir).encode("utf-8"),
            ctypes.byref(sequence_options),
            ctypes.byref(last_committed),
        )
        == WISTERIA_STATUS_OK
        and last_committed.value == 1,
        "sequence range",
    )
    check(
        (output_dir / "manifest.jsonl").is_file()
        and (output_dir / "00000000.png").is_file()
        and (output_dir / "00000001.png").is_file(),
        "sequence artifacts",
    )

    # 7. Static entity: capabilities + single-frame render.
    static_entity = U64(0)
    check(
        abi.lib.wisteria_stable_entity_create(
            context,
            ctypes.byref(runtime_options),
            str(args.static_model).encode("utf-8"),
            ctypes.byref(static_entity),
        )
        == WISTERIA_STATUS_OK,
        "static entity create",
    )
    static_capabilities = WisteriaRuntimeCapabilitiesV1()
    ctypes.memset(
        ctypes.byref(static_capabilities), 0, ctypes.sizeof(static_capabilities)
    )
    static_capabilities.struct_size = ctypes.sizeof(static_capabilities)
    static_capabilities.struct_version = 1
    check(
        abi.lib.wisteria_stable_entity_capabilities(
            context,
            static_entity,
            ctypes.byref(static_capabilities),
        )
        == WISTERIA_STATUS_OK
        and static_capabilities.runtime_backend_id
        == WISTERIA_BACKEND_ID_STATIC
        and static_capabilities.capability_flags == 0,
        "static capabilities",
    )
    static_frame = (ctypes.c_uint8 * byte_count)()
    static_filled = ctypes.c_uint64(byte_count)
    check(
        abi.lib.wisteria_stable_render_session_render(
            context,
            render_session,
            static_entity,
            ctypes.byref(camera),
            width,
            height,
            static_frame,
            ctypes.byref(static_filled),
        )
        == WISTERIA_STATUS_OK,
        "static single-frame render",
    )
    check(
        any(value != 0 for value in static_frame),
        "static render frame is all zero",
    )

    # 8. Status semantics: NOT_FOUND only for missing handles.
    check(
        abi.lib.wisteria_stable_entity_step_exact(
            context, 0xDEADBEEF, 1
        )
        == WISTERIA_STATUS_NOT_FOUND,
        "garbage entity must be NOT_FOUND",
    )
    check(
        abi.lib.wisteria_stable_entity_step_exact(
            context, static_entity, 1
        )
        == WISTERIA_STATUS_UNSUPPORTED,
        "static step_exact must be UNSUPPORTED",
    )
    check(
        abi.lib.wisteria_stable_entity_load_motion(
            context,
            generic_entity,
            b"does-not-matter.vmd",
        )
        == WISTERIA_STATUS_UNSUPPORTED,
        "generic load_motion must be UNSUPPORTED",
    )
    check(
        len(abi.last_error(context)) > 0,
        "last_error diagnostic after UNSUPPORTED",
    )

    # 9. Teardown in GPU-safe order.
    check(
        abi.lib.wisteria_stable_entity_destroy(context, restored_entity)
        == WISTERIA_STATUS_OK
        and abi.lib.wisteria_stable_entity_destroy(context, static_entity)
        == WISTERIA_STATUS_OK
        and abi.lib.wisteria_stable_entity_destroy(context, generic_entity)
        == WISTERIA_STATUS_OK,
        "entity teardown",
    )
    check(
        abi.lib.wisteria_stable_render_session_destroy(
            context, render_session
        )
        == WISTERIA_STATUS_OK
        and abi.lib.wisteria_stable_context_destroy(context)
        == WISTERIA_STATUS_OK,
        "session/context teardown",
    )

    print(
        "PASS: stable ABI ctypes acceptance "
        f"(generic+static+checkpoint+render, out={output_dir})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
