#!/usr/bin/env python3
"""M3: WISTERIA native C ABI demo through Python ctypes.

Mirrors the windowed demo in headless form:
  load model -> load VMD motion -> configure Saba physics
  (120 Hz / 10 sub-steps / -98 gravity) -> step 720 frames -> vertex bounds.

Usage (from the project root):
    python examples/python/native_mmd_demo.py [--model path] [--motion path]
        [--frames 720] [--fps 60] [--physics-fps 120] [--max-substeps 10]

The shared library is located automatically:
    Windows: build/<config>/wisteria_native.dll
    Linux:   build-linux/libwisteria_native.so
Override with the WISTERIA_NATIVE_LIB environment variable.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import sys
from pathlib import Path


WISTERIA_OK = 0
WISTERIA_ERROR_INVALID_ARGUMENT = 1
WISTERIA_ERROR_NOT_FOUND = 2
WISTERIA_ERROR_IO = 3
WISTERIA_ERROR_PARSE = 4
WISTERIA_ERROR_INITIALIZATION = 5
WISTERIA_ERROR_ALREADY_EXISTS = 6
WISTERIA_ERROR_INTERNAL = 7


class WisteriaVertexBounds(ctypes.Structure):
    _fields_ = [
        ("finite", ctypes.c_int32),
        ("minimum", ctypes.c_float * 3),
        ("maximum", ctypes.c_float * 3),
        ("maximumDisplacementFromBind", ctypes.c_float),
        ("vertexCount", ctypes.c_uint64),
    ]


def find_library(project_root: Path) -> Path:
    override = os.environ.get("WISTERIA_NATIVE_LIB")
    if override:
        path = Path(override)
        if path.is_file():
            return path
        raise FileNotFoundError(
            "WISTERIA_NATIVE_LIB points to a missing file: " + override
        )

    candidates = []
    if sys.platform == "win32":
        candidates = [
            project_root / "build" / "RelWithDebInfo" / "wisteria_native.dll",
            project_root / "build" / "Release" / "wisteria_native.dll",
            project_root / "build" / "Debug" / "wisteria_native.dll",
        ]
    elif sys.platform == "darwin":
        candidates = [
            project_root / "build-linux" / "libwisteria_native.dylib",
        ]
    else:
        candidates = [
            project_root / "build-linux" / "libwisteria_native.so",
        ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "wisteria_native shared library not found. Build it first, or set "
        "WISTERIA_NATIVE_LIB. Searched: "
        + ", ".join(str(path) for path in candidates)
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="WISTERIA native C ABI headless demo (Python ctypes)"
    )
    project_root = Path(__file__).resolve().parents[2]
    parser.add_argument(
        "--model",
        default=str(
            project_root
            / "assets"
            / "models"
            / "mmd"
            / "蕾米埃尔-白"
            / "蕾米埃尔-白.pmx"
        ),
        help="PMX model path",
    )
    parser.add_argument(
        "--motion",
        default=str(
            project_root / "assets" / "motions" / "梦的翅膀" / "梦的翅膀motion.vmd"
        ),
        help="VMD motion path",
    )
    parser.add_argument("--frames", type=int, default=720)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--physics-fps", type=float, default=120.0)
    parser.add_argument("--max-substeps", type=int, default=10)
    args = parser.parse_args()

    library_path = find_library(project_root)
    print(f"[FFI] library={library_path}")

    # Build the binding table manually: ctypes requires explicit argtypes.
    library = ctypes.CDLL(str(library_path))

    def bind(name, argtypes, restype):
        function = getattr(library, name)
        function.argtypes = argtypes
        function.restype = restype
        return function

    status_name = bind(
        "wisteria_status_name",
        [ctypes.c_int],
        ctypes.c_char_p,
    )
    version_major = bind("wisteria_version_major", [], ctypes.c_uint32)
    version_minor = bind("wisteria_version_minor", [], ctypes.c_uint32)
    create_context = bind(
        "wisteria_create_context",
        [ctypes.POINTER(ctypes.c_uint64)],
        ctypes.c_int,
    )
    destroy_context = bind(
        "wisteria_destroy_context",
        [ctypes.c_uint64],
        ctypes.c_int,
    )
    last_error_message = bind(
        "wisteria_last_error_message",
        [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_size_t],
        ctypes.c_int,
    )
    load_model = bind(
        "wisteria_load_model",
        [ctypes.c_uint64, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint64)],
        ctypes.c_int,
    )
    unload_model = bind(
        "wisteria_unload_model",
        [ctypes.c_uint64, ctypes.c_uint64],
        ctypes.c_int,
    )
    load_motion = bind(
        "wisteria_load_motion",
        [
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint64),
        ],
        ctypes.c_int,
    )
    unload_motion = bind(
        "wisteria_unload_motion",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64],
        ctypes.c_int,
    )
    play_motion = bind(
        "wisteria_play_motion",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64],
        ctypes.c_int,
    )
    pause_motion = bind(
        "wisteria_pause_motion",
        [ctypes.c_uint64, ctypes.c_uint64],
        ctypes.c_int,
    )
    resume_motion = bind(
        "wisteria_resume_motion",
        [ctypes.c_uint64, ctypes.c_uint64],
        ctypes.c_int,
    )
    set_motion_looping = bind(
        "wisteria_set_motion_looping",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_int32],
        ctypes.c_int,
    )
    set_motion_frame = bind(
        "wisteria_set_motion_frame",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_double],
        ctypes.c_int,
    )
    motion_frame = bind(
        "wisteria_motion_frame",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.POINTER(ctypes.c_double)],
        ctypes.c_int,
    )
    motion_max_frame = bind(
        "wisteria_motion_max_frame",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.POINTER(ctypes.c_double)],
        ctypes.c_int,
    )
    update = bind(
        "wisteria_update",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_float],
        ctypes.c_int,
    )
    set_physics_settings = bind(
        "wisteria_set_physics_settings",
        [
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.c_float,
            ctypes.c_int32,
            ctypes.c_float,
            ctypes.c_float,
            ctypes.c_float,
        ],
        ctypes.c_int,
    )
    vertex_bounds = bind(
        "wisteria_vertex_bounds",
        [
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.POINTER(WisteriaVertexBounds),
        ],
        ctypes.c_int,
    )

    context = ctypes.c_uint64(0)
    model = ctypes.c_uint64(0)
    motion = ctypes.c_uint64(0)
    frame_value = ctypes.c_double(0.0)
    max_frame_value = ctypes.c_double(0.0)
    bounds = WisteriaVertexBounds()
    error_buffer = ctypes.create_string_buffer(1024)

    def check(status: int, what: str) -> None:
        if status == WISTERIA_OK:
            return
        last_error_message(
            context.value,
            error_buffer,
            len(error_buffer),
        )
        name = status_name(status).decode("utf-8", "replace")
        raise RuntimeError(
            f"{what} failed: {name}: "
            + error_buffer.value.decode("utf-8", "replace")
        )

    print(
        f"[FFI] WISTERIA native v{version_major()}.{version_minor()}"
    )
    check(create_context(ctypes.byref(context)), "create context")
    print(f"[FFI] context={context.value}")
    try:
        model_path = Path(args.model)
        motion_path = Path(args.motion)
        if not model_path.is_file():
            raise FileNotFoundError(f"model not found: {model_path}")
        if not motion_path.is_file():
            raise FileNotFoundError(f"motion not found: {motion_path}")

        check(
            load_model(
                context.value,
                str(model_path).encode("utf-8"),
                ctypes.byref(model),
            ),
            "load model",
        )
        print(f"[FFI] model={model.value} path={model_path}")
        check(
            load_motion(
                context.value,
                model.value,
                str(motion_path).encode("utf-8"),
                ctypes.byref(motion),
            ),
            "load motion",
        )
        print(f"[FFI] motion={motion.value} path={motion_path}")

        check(
            motion_max_frame(
                context.value,
                model.value,
                ctypes.byref(max_frame_value),
            ),
            "query max frame",
        )
        print(f"[FFI] maxFrame={max_frame_value.value:.1f}")

        check(
            set_physics_settings(
                context.value,
                model.value,
                ctypes.c_float(1.0 / args.physics_fps),
                ctypes.c_int32(args.max_substeps),
                ctypes.c_float(0.0),
                ctypes.c_float(-98.0),
                ctypes.c_float(0.0),
            ),
            "set physics settings",
        )
        print(
            f"[FFI] physics={args.physics_fps:g}Hz "
            f"maxSubSteps={args.max_substeps} gravity=(0,-98,0)"
        )
        check(
            set_motion_looping(context.value, model.value, 1),
            "set motion looping",
        )
        check(
            play_motion(context.value, model.value, motion.value),
            "play motion",
        )

        delta_time = 1.0 / args.fps
        for index in range(args.frames):
            check(
                update(context.value, model.value, ctypes.c_float(delta_time)),
                "update",
            )
            if (index + 1) % 60 == 0:
                check(
                    motion_frame(
                        context.value,
                        model.value,
                        ctypes.byref(frame_value),
                    ),
                    "query motion frame",
                )
                check(
                    vertex_bounds(
                        context.value,
                        model.value,
                        ctypes.byref(bounds),
                    ),
                    "query vertex bounds",
                )
                print(
                    f"[FFI] frame={index + 1:4d} "
                    f"motion={frame_value.value:7.2f} "
                    f"finite={bounds.finite} "
                    f"min=({bounds.minimum[0]:.3f},{bounds.minimum[1]:.3f},"
                    f"{bounds.minimum[2]:.3f}) "
                    f"max=({bounds.maximum[0]:.3f},{bounds.maximum[1]:.3f},"
                    f"{bounds.maximum[2]:.3f}) "
                    f"displacement={bounds.maximumDisplacementFromBind:.3f} "
                    f"vertices={bounds.vertexCount}"
                )

        check(
            motion_frame(
                context.value,
                model.value,
                ctypes.byref(frame_value),
            ),
            "query final motion frame",
        )
        check(
            vertex_bounds(
                context.value,
                model.value,
                ctypes.byref(bounds),
            ),
            "query final vertex bounds",
        )
        print(
            f"[FFI] done frames={args.frames} "
            f"motion={frame_value.value:.2f} "
            f"finite={bool(bounds.finite)} "
            f"maxBindDisplacement={bounds.maximumDisplacementFromBind:.3f}"
        )

        # Control-path demo: pause -> update must not advance -> resume.
        check(pause_motion(context.value, model.value), "pause motion")
        paused = ctypes.c_double(0.0)
        check(
            motion_frame(context.value, model.value, ctypes.byref(paused)),
            "query paused frame",
        )
        check(
            update(context.value, model.value, ctypes.c_float(delta_time)),
            "update while paused",
        )
        check(
            motion_frame(context.value, model.value, ctypes.byref(frame_value)),
            "query frame after paused update",
        )
        check(
            resume_motion(context.value, model.value),
            "resume motion",
        )
        print(
            f"[FFI] pause/resume ok: paused={paused.value:.2f} "
            f"afterPausedUpdate={frame_value.value:.2f}"
        )

        check(
            unload_motion(context.value, model.value, motion.value),
            "unload motion",
        )
        check(unload_model(context.value, model.value), "unload model")
    finally:
        check(destroy_context(context.value), "destroy context")
        print("[FFI] context destroyed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001 - CLI entry point
        print(f"[FFI] ERROR: {error}", file=sys.stderr)
        sys.exit(1)
