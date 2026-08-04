#!/usr/bin/env python3
"""M4: WISTERIA native C ABI window demo through Python ctypes.

Opens a REAL desktop window and drives the same Saba MMD demo as
`wisteria.exe`: model + motion + physics + VMD camera, using the pull model
(the script calls the context-wide wisteria_poll_and_render once per frame).

Usage (from the project root):
    python examples/python/native_window_demo.py [--frames 360]
        [--model path] [--motion path] [--scene path]
        [--fps 60] [--physics-fps 120] [--max-substeps 10]

Window shortcuts (handled inside the demo, same as wisteria.exe):
    Space  pause/resume motion
    C      toggle VMD camera follow
    Left/Right  halve/double camera speed
    Esc    close the window
"""

from __future__ import annotations

import argparse
import ctypes
import os
import sys
from pathlib import Path


WISTERIA_OK = 0


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
    else:
        candidates = [project_root / "build-linux" / "libwisteria_native.so"]
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
        description="WISTERIA native C ABI window demo (Python ctypes)"
    )
    project_root = Path(__file__).resolve().parents[2]
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--physics-fps", type=float, default=120.0)
    parser.add_argument("--max-substeps", type=int, default=10)
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
    )
    parser.add_argument(
        "--motion",
        default=str(
            project_root / "assets" / "motions" / "梦的翅膀" / "梦的翅膀motion.vmd"
        ),
    )
    parser.add_argument("--scene", default="")
    args = parser.parse_args()
    if args.frames <= 0:
        parser.error("--frames must be positive")
    if args.fps <= 0.0 or args.physics_fps <= 0.0:
        parser.error("--fps and --physics-fps must be positive")
    if args.max_substeps <= 0:
        parser.error("--max-substeps must be positive")

    library_path = find_library(project_root)
    print(f"[FFI] library={library_path}")
    library = ctypes.CDLL(str(library_path))

    def bind(name, argtypes, restype):
        function = getattr(library, name)
        function.argtypes = argtypes
        function.restype = restype
        return function

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
    status_name = bind(
        "wisteria_status_name",
        [ctypes.c_int],
        ctypes.c_char_p,
    )
    window_create = bind(
        "wisteria_window_create",
        [ctypes.c_uint64, ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
         ctypes.POINTER(ctypes.c_uint64)],
        ctypes.c_int,
    )
    window_destroy = bind(
        "wisteria_window_destroy",
        [ctypes.c_uint64, ctypes.c_uint64],
        ctypes.c_int,
    )
    window_load_demo = bind(
        "wisteria_window_load_demo",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p,
         ctypes.c_char_p, ctypes.c_float, ctypes.c_int32],
        ctypes.c_int,
    )
    poll_and_render = bind(
        "wisteria_poll_and_render",
        [ctypes.c_uint64, ctypes.c_float],
        ctypes.c_int,
    )
    window_should_close = bind(
        "wisteria_window_should_close",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.POINTER(ctypes.c_int32)],
        ctypes.c_int,
    )
    window_camera_pose = bind(
        "wisteria_window_camera_pose",
        [ctypes.c_uint64, ctypes.c_uint64,
         ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
         ctypes.POINTER(ctypes.c_float)],
        ctypes.c_int,
    )
    window_is_key_down = bind(
        "wisteria_window_is_key_down",
        [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_int,
         ctypes.POINTER(ctypes.c_int32)],
        ctypes.c_int,
    )
    window_cursor_delta = bind(
        "wisteria_window_cursor_delta",
        [ctypes.c_uint64, ctypes.c_uint64,
         ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float)],
        ctypes.c_int,
    )

    WISTERIA_KEY_SPACE = 18

    context = ctypes.c_uint64(0)
    window = ctypes.c_uint64(0)
    closed = ctypes.c_int32(0)
    error_buffer = ctypes.create_string_buffer(1024)

    def check(status: int, what: str) -> None:
        if status == WISTERIA_OK:
            return
        last_error_message(context.value, error_buffer, len(error_buffer))
        name = status_name(status).decode("utf-8", "replace")
        raise RuntimeError(
            f"{what} failed: {name}: "
            + error_buffer.value.decode("utf-8", "replace")
        )

    check(create_context(ctypes.byref(context)), "create context")
    try:
        check(
            window_create(
                context.value,
                960,
                720,
                b"WISTERIA M4 - Python ctypes window",
                ctypes.byref(window),
            ),
            "create window",
        )
        print(f"[FFI] window={window.value} opened (960x720)")
        check(
            window_load_demo(
                context.value,
                window.value,
                args.model.encode("utf-8"),
                args.motion.encode("utf-8"),
                args.scene.encode("utf-8") if args.scene else None,
                ctypes.c_float(1.0 / args.physics_fps),
                ctypes.c_int32(args.max_substeps),
            ),
            "load demo",
        )
        print(
            f"[FFI] demo loaded: model={args.model}\n"
            f"[FFI]   motion={args.motion} physics={args.physics_fps:g}Hz "
            f"maxSubSteps={args.max_substeps}\n"
            f"[FFI]   Space=pause  C=camera  Left/Right=speed  Esc=close"
        )

        delta_time = 1.0 / args.fps
        position = (ctypes.c_float * 3)()
        target = (ctypes.c_float * 3)()
        up = (ctypes.c_float * 3)()
        space_down = ctypes.c_int32(0)
        cursor_x = ctypes.c_float(0.0)
        cursor_y = ctypes.c_float(0.0)
        for frame in range(args.frames):
            check(
                poll_and_render(
                    context.value,
                    ctypes.c_float(delta_time),
                ),
                "poll and render",
            )
            check(
                window_should_close(
                    context.value,
                    window.value,
                    ctypes.byref(closed),
                ),
                "query close",
            )
            if closed.value:
                print("[FFI] window close requested by user")
                break
            if frame < 3 or (frame + 1) % 60 == 0:
                check(
                    window_camera_pose(
                        context.value,
                        window.value,
                        position,
                        target,
                        up,
                    ),
                    "query camera",
                )
                check(
                    window_is_key_down(
                        context.value,
                        window.value,
                        WISTERIA_KEY_SPACE,
                        ctypes.byref(space_down),
                    ),
                    "query key",
                )
                check(
                    window_cursor_delta(
                        context.value,
                        window.value,
                        ctypes.byref(cursor_x),
                        ctypes.byref(cursor_y),
                    ),
                    "query cursor",
                )
                print(
                    f"[FFI] frame={frame + 1:4d} "
                    f"cam=({position[0]:.2f},{position[1]:.2f},"
                    f"{position[2]:.2f})->({target[0]:.2f},{target[1]:.2f},"
                    f"{target[2]:.2f}) "
                    f"space={space_down.value} "
                    f"cursor=({cursor_x.value:.1f},{cursor_y.value:.1f})"
                )

        check(window_destroy(context.value, window.value), "destroy window")
        print("[FFI] window destroyed")
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
