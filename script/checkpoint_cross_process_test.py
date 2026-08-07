#!/usr/bin/env python3
"""R1.4 Phase 0A Step 8: checkpoint wire cross-process regression.

Process A (dump) serializes a canonical checkpoint to a file. Process B
(load) deserializes the same bytes in a separate address space and restores
them on a fresh runtime. Both processes print the exact physics hash of the
checkpoint payload; this harness asserts the hashes match and both exits are
successful.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HASH_RE = re.compile(r"^HASH=([0-9a-fA-F]+)$", re.MULTILINE)


def run(cli: str, mode: str, model: Path, frame: int, wire: Path) -> int | None:
    result = subprocess.run(
        [cli, mode, str(model), str(frame), str(wire)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return None
    match = HASH_RE.search(result.stdout)
    if match is None:
        print(f"{mode} printed no HASH: {result.stdout}", file=sys.stderr)
        return None
    return int(match.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cli", type=Path, help="checkpoint_wire_cli executable")
    parser.add_argument("model", type=Path, help="pmx-physics fixture path")
    parser.add_argument("--frame", type=int, default=30)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        wire = Path(tmp) / "checkpoint.bin"
        dump_hash = run(
            str(args.cli),
            "dump",
            args.model,
            args.frame,
            wire,
        )
        if dump_hash is None:
            return 1
        load_hash = run(
            str(args.cli),
            "load",
            args.model,
            args.frame,
            wire,
        )
        if load_hash is None:
            return 2
        if dump_hash != load_hash:
            print(
                f"hash mismatch: dump={dump_hash:#x} load={load_hash:#x}",
                file=sys.stderr,
            )
            return 3
    print(f"cross-process checkpoint round trip OK ({dump_hash:#x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
