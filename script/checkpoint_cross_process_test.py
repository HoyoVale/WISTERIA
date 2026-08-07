#!/usr/bin/env python3
"""R1.4 Phase 0A Step 8: checkpoint wire cross-process regression.

Process A (dump) serializes a canonical checkpoint at frame N and also
captures the exact physics hash after stepping to N+1. Process B (load)
deserializes the same bytes in a separate address space, restores them on a
fresh runtime, re-creates the checkpoint at N, then steps to N+1 and creates
another checkpoint. This harness asserts both the restored N hash and the
deterministic-continuation N+1 hash match across the two processes.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HASH_N_RE = re.compile(r"^HASH_N=([0-9a-fA-F]+)$", re.MULTILINE)
HASH_N1_RE = re.compile(r"^HASH_N1=([0-9a-fA-F]+)$", re.MULTILINE)


def run(
    cli: str,
    mode: str,
    model: Path,
    frame: int,
    wire: Path,
) -> tuple[int, int] | None:
    result = subprocess.run(
        [cli, mode, str(model), str(frame), str(wire)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return None
    match_n = HASH_N_RE.search(result.stdout)
    match_n1 = HASH_N1_RE.search(result.stdout)
    if match_n is None or match_n1 is None:
        print(
            f"{mode} printed no HASH_N/HASH_N1: {result.stdout}",
            file=sys.stderr,
        )
        return None
    return int(match_n.group(1), 16), int(match_n1.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cli", type=Path, help="checkpoint_wire_cli executable")
    parser.add_argument("model", type=Path, help="pmx-physics fixture path")
    parser.add_argument("--frame", type=int, default=30)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        wire = Path(tmp) / "checkpoint.bin"
        dump_hashes = run(
            str(args.cli),
            "dump",
            args.model,
            args.frame,
            wire,
        )
        if dump_hashes is None:
            return 1
        load_hashes = run(
            str(args.cli),
            "load",
            args.model,
            args.frame,
            wire,
        )
        if load_hashes is None:
            return 2
        if dump_hashes != load_hashes:
            print(
                f"hash mismatch: dump_N={dump_hashes[0]:#x} "
                f"load_N={load_hashes[0]:#x} "
                f"dump_N1={dump_hashes[1]:#x} "
                f"load_N1={load_hashes[1]:#x}",
                file=sys.stderr,
            )
            return 3
    print(
        f"cross-process checkpoint round trip OK "
        f"(N={dump_hashes[0]:#x}, N+1={dump_hashes[1]:#x})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
