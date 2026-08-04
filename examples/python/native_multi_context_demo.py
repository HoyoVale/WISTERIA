#!/usr/bin/env python3
"""Regression demo for independent WISTERIA native desktop contexts.

The test keeps two C ABI Context/Application/OpenGL share groups alive at the
same time. Halfway through the run it destroys context A and continues driving
context B. This directly exercises two R0 fixes:

* shader programs must never be reused across unrelated OpenGL contexts;
* destroying one Context must not call glfwTerminate while another is alive.

Run from the project root after building with WISTERIA_BUILD_NATIVE=ON.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import sys
from pathlib import Path

from native_window_demo import WISTERIA_OK, find_library


def main() -> int:
    project_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description="WISTERIA two-context OpenGL/GLFW regression demo"
    )
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--destroy-first-at", type=int, default=120)
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
    args = parser.parse_args()

    if args.frames <= 0:
        parser.error("--frames must be positive")
    if not 0 < args.destroy_first_at < args.frames:
        parser.error("--destroy-first-at must be between 1 and frames-1")
    if args.fps <= 0.0 or args.physics_fps <= 0.0:
        parser.error("--fps and --physics-fps must be positive")
    if args.max_substeps <= 0:
        parser.error("--max-substeps must be positive")

    library_path = find_library(project_root)
    print(f"[MULTI] library={library_path}")
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
    status_name = bind("wisteria_status_name", [ctypes.c_int], ctypes.c_char_p)
    window_create = bind(
        "wisteria_window_create",
        [
            ctypes.c_uint64,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint64),
        ],
        ctypes.c_int,
    )
    window_destroy = bind(
        "wisteria_window_destroy",
        [ctypes.c_uint64, ctypes.c_uint64],
        ctypes.c_int,
    )
    window_load_demo = bind(
        "wisteria_window_load_demo",
        [
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_float,
            ctypes.c_int32,
        ],
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

    contexts = {"A": ctypes.c_uint64(0), "B": ctypes.c_uint64(0)}
    windows = {"A": ctypes.c_uint64(0), "B": ctypes.c_uint64(0)}
    alive = {"A": False, "B": False}
    error_buffer = ctypes.create_string_buffer(1024)

    def check(status: int, what: str, context_value: int = 0) -> None:
        if status == WISTERIA_OK:
            return
        message = ""
        if context_value:
            last_error_message(context_value, error_buffer, len(error_buffer))
            message = error_buffer.value.decode("utf-8", "replace")
        name = status_name(status).decode("utf-8", "replace")
        raise RuntimeError(f"{what} failed: {name}: {message}")

    def create(label: str) -> None:
        check(create_context(ctypes.byref(contexts[label])), f"create context {label}")
        alive[label] = True
        context = contexts[label].value
        check(
            window_create(
                context,
                720,
                540,
                f"WISTERIA R0 MULTI CONTEXT {label}".encode("utf-8"),
                ctypes.byref(windows[label]),
            ),
            f"create window {label}",
            context,
        )
        check(
            window_load_demo(
                context,
                windows[label].value,
                args.model.encode("utf-8"),
                args.motion.encode("utf-8"),
                None,
                ctypes.c_float(args.physics_fps),
                ctypes.c_int32(args.max_substeps),
            ),
            f"load demo {label}",
            context,
        )
        print(
            f"[MULTI] {label}: context={context} window={windows[label].value} loaded"
        )

    def destroy(label: str) -> None:
        if not alive[label]:
            return
        context = contexts[label].value
        if windows[label].value:
            check(
                window_destroy(context, windows[label].value),
                f"destroy window {label}",
                context,
            )
            windows[label].value = 0
        check(destroy_context(context), f"destroy context {label}", context)
        contexts[label].value = 0
        alive[label] = False
        print(f"[MULTI] {label}: destroyed")

    try:
        create("A")
        create("B")
        delta_time = ctypes.c_float(1.0 / args.fps)
        closed = ctypes.c_int32(0)

        for frame in range(1, args.frames + 1):
            if alive["A"]:
                check(
                    poll_and_render(contexts["A"].value, delta_time),
                    "render context A",
                    contexts["A"].value,
                )
            check(
                poll_and_render(contexts["B"].value, delta_time),
                "render context B",
                contexts["B"].value,
            )
            check(
                window_should_close(
                    contexts["B"].value,
                    windows["B"].value,
                    ctypes.byref(closed),
                ),
                "query window B",
                contexts["B"].value,
            )
            if closed.value:
                print("[MULTI] window B close requested by user")
                break

            if frame == args.destroy_first_at:
                print(
                    "[MULTI] destroying context A; context B must keep rendering"
                )
                destroy("A")

            if frame <= 3 or frame % 60 == 0 or frame == args.destroy_first_at + 1:
                print(
                    f"[MULTI] frame={frame} A={'alive' if alive['A'] else 'dead'} "
                    "B=alive"
                )

        if not alive["B"]:
            raise RuntimeError("context B died before the end of the demo")
        print("[MULTI] PASS: context B survived context A destruction")
    finally:
        for label in ("A", "B"):
            try:
                destroy(label)
            except Exception as error:  # noqa: BLE001 - cleanup must continue
                print(f"[MULTI] cleanup {label} failed: {error}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001 - CLI entry point
        print(f"[MULTI] ERROR: {error}", file=sys.stderr)
        sys.exit(1)
