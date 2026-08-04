#!/usr/bin/env python3
"""Validate WISTERIA render-smoke BMP captures without external packages.

The old verifier only counted unique file hashes. That allowed a sequence such
as "first frame is valid, every later frame is black" to pass because it still
contained two hashes. This helper checks pixel ranges as well as temporal
variation and exits non-zero for common frozen/black-frame failures.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CaptureInfo:
    path: Path
    sha256: str
    width: int
    height: int
    minimum_rgb: int
    maximum_rgb: int


def read_capture(path: Path) -> CaptureInfo:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"not a supported BMP file: {path}")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"unsupported BMP DIB header ({dib_size}): {path}")

    width = struct.unpack_from("<i", data, 18)[0]
    signed_height = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or signed_height == 0:
        raise ValueError(f"invalid BMP dimensions: {path}")
    if planes != 1 or bits_per_pixel not in (24, 32) or compression != 0:
        raise ValueError(
            "only uncompressed 24/32-bit BMP captures are supported: " + str(path)
        )

    height = abs(signed_height)
    bytes_per_pixel = bits_per_pixel // 8
    row_stride = ((width * bits_per_pixel + 31) // 32) * 4
    required_size = pixel_offset + row_stride * height
    if required_size > len(data):
        raise ValueError(f"truncated BMP pixel data: {path}")

    minimum_rgb = 255
    maximum_rgb = 0
    for row in range(height):
        row_start = pixel_offset + row * row_stride
        for column in range(width):
            pixel_start = row_start + column * bytes_per_pixel
            blue, green, red = data[pixel_start : pixel_start + 3]
            minimum_rgb = min(minimum_rgb, red, green, blue)
            maximum_rgb = max(maximum_rgb, red, green, blue)

    return CaptureInfo(
        path=path,
        sha256=hashlib.sha256(data).hexdigest(),
        width=width,
        height=height,
        minimum_rgb=minimum_rgb,
        maximum_rgb=maximum_rgb,
    )


def validate(directory: Path, minimum_brightness: int) -> int:
    files = sorted(directory.glob("*.bmp"))
    if len(files) < 2:
        print(
            f"[CAPTURE ERROR] expected at least two BMP files in {directory}, "
            f"found {len(files)}",
            file=sys.stderr,
        )
        return 1

    try:
        captures = [read_capture(path) for path in files]
    except (OSError, ValueError) as error:
        print(f"[CAPTURE ERROR] {error}", file=sys.stderr)
        return 1

    for capture in captures:
        print(
            f"[CAPTURE] {capture.sha256[:16]}  {capture.path.name}  "
            f"{capture.width}x{capture.height}  "
            f"rgbRange={capture.minimum_rgb}..{capture.maximum_rgb}"
        )

    errors: list[str] = []
    hashes = [capture.sha256 for capture in captures]
    unique_count = len(set(hashes))
    bright_captures = [
        capture
        for capture in captures
        if capture.maximum_rgb > minimum_brightness
    ]
    black_or_near_black = [
        capture
        for capture in captures
        if capture.maximum_rgb <= minimum_brightness
    ]

    if unique_count <= 1:
        errors.append("all captures are byte-identical; rendering may be frozen")

    required_unique = min(3, len(captures))
    if unique_count < required_unique:
        errors.append(
            f"only {unique_count} unique captures were produced; "
            f"expected at least {required_unique}"
        )

    if len(bright_captures) < 2:
        errors.append(
            f"only {len(bright_captures)} capture(s) contain visible RGB values"
        )

    if black_or_near_black:
        names = ", ".join(capture.path.name for capture in black_or_near_black)
        errors.append(
            f"{len(black_or_near_black)} capture(s) are black/near-black "
            f"(max RGB <= {minimum_brightness}): {names}"
        )

    if len(captures) >= 3 and len(set(hashes[1:])) == 1:
        errors.append(
            "every capture after the first is identical; this matches the "
            "first-frame-then-black/frozen failure pattern"
        )

    if errors:
        for error in errors:
            print(f"[CAPTURE ERROR] {error}", file=sys.stderr)
        return 1

    print(
        f"[CAPTURE PASS] {len(captures)} files, {unique_count} unique hashes, "
        f"{len(bright_captures)} visible frames"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate WISTERIA render-smoke BMP captures"
    )
    parser.add_argument("directory", type=Path)
    parser.add_argument(
        "--minimum-brightness",
        type=int,
        default=2,
        help="maximum RGB <= this value is treated as black (default: 2)",
    )
    args = parser.parse_args()
    if not 0 <= args.minimum_brightness <= 255:
        parser.error("--minimum-brightness must be between 0 and 255")
    if not args.directory.is_dir():
        parser.error(f"capture directory does not exist: {args.directory}")
    return validate(args.directory, args.minimum_brightness)


if __name__ == "__main__":
    sys.exit(main())
