#!/usr/bin/env python3
"""R1.4 Phase 0B: Stable C ABI cross-process checkpoint E2E.

Process A (dump) drives the public stable surface: entity -> replay N ->
checkpoint N bytes, then step N+1 -> checkpoint N+1 bytes. Process B (load)
deserializes N, restores it on a fresh entity, re-creates checkpoint N and
continues to N+1.

N wire bytes are always asserted byte-for-byte identical (cross-process
restore evidence). N+1 equality is asserted only with --require-n1; without
it the harness reports N+1 as a diagnostic (see
docs/validation/R1_4_PHASE0B_FINDINGS_20260808.md for the production-VMD
engine-level divergence).
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def run(args: list[str]) -> bool:
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cli", type=Path)
    parser.add_argument("model_id", type=str)
    parser.add_argument("vmd_id", type=str, nargs="?", default="-")
    parser.add_argument("--frame", type=int, default=30)
    parser.add_argument(
        "--require-n1",
        action="store_true",
        help="fail when N+1 wire bytes differ across processes",
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        a_n = tmp_path / "a_n.bin"
        a_n1 = tmp_path / "a_n1.bin"
        b_n = tmp_path / "b_n.bin"
        b_n1 = tmp_path / "b_n1.bin"
        frame = str(args.frame)

        dump_ok = run(
            [
                str(args.cli),
                "dump",
                args.model_id,
                args.vmd_id,
                frame,
                str(a_n),
                str(a_n1),
            ]
        )
        if not dump_ok:
            return 1
        load_ok = run(
            [
                str(args.cli),
                "load",
                args.model_id,
                args.vmd_id,
                frame,
                str(a_n),
                str(b_n),
                str(b_n1),
            ]
        )
        if not load_ok:
            return 2

        wire_a_n = a_n.read_bytes()
        wire_a_n1 = a_n1.read_bytes()
        wire_b_n = b_n.read_bytes()
        wire_b_n1 = b_n1.read_bytes()
        if wire_a_n != wire_b_n:
            print("N wire mismatch across processes", file=sys.stderr)
            return 3
        n1_match = wire_a_n1 == wire_b_n1
        if args.require_n1 and not n1_match:
            print("N+1 wire mismatch across processes", file=sys.stderr)
            return 4
    print(
        f"stable cross-process checkpoint E2E OK "
        f"({args.model_id}, N={args.frame}, bytes={len(wire_a_n)}, "
        f"N+1={'MATCH' if n1_match else 'DIAGNOSTIC_MISMATCH'})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
