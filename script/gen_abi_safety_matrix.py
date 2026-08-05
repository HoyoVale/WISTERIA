#!/usr/bin/env python3
"""Regenerate docs/architecture/C_ABI_SAFETY_MATRIX.md from the C ABI header
and native implementation files.

Usage:
    python script/gen_abi_safety_matrix.py [--check]

--check exits non-zero if the committed matrix is out of date. R1.S must add
this script to CI so the matrix cannot silently drift when C API grows.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "wisteria" / "native" / "wisteria_native.h"
NATIVE_DIR = ROOT / "src" / "native"
MATRIX = ROOT / "docs" / "architecture" / "C_ABI_SAFETY_MATRIX.md"

EXPORT_RE = re.compile(
    r"WISTERIA_API\s+[^;]*?\b(wisteria_\w+)\s*\("
)


def exported_functions() -> list[str]:
    text = HEADER.read_text(encoding="utf-8")
    return EXPORT_RE.findall(text)


def find_definition(function: str, sources: dict[str, str]):
    pattern = re.compile(
        r"(?:\w[\w:<>*&\s]*?)\b" + function + r"\s*\("
    )
    for name, text in sources.items():
        match = pattern.search(text)
        if not match:
            continue
        start = match.start()
        brace = text.index("{", start)
        depth = 1
        index = brace + 1
        while index < len(text) and depth > 0:
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
            index += 1
        body = text[start:index]
        guarded = "GuardAbi" in body
        raw_try = bool(re.search(r"\btry\s*\{", body))
        return name, guarded, raw_try
    return None, False, False


def classify(function: str, sources: dict[str, str]) -> tuple[str, str, str]:
    """Returns (file, status, note). Status is one of GUARDED / RAW_TRY /
    PROVEN_NO_THROW_LEAF / UNGUARDED."""
    leaf_functions = {
        "wisteria_status_name",
        "wisteria_version_major",
        "wisteria_version_minor",
    }
    file, guarded, raw_try = find_definition(function, sources)
    if guarded:
        status = "GUARDED"
    elif raw_try:
        status = "RAW_TRY"
    elif function in leaf_functions:
        status = "PROVEN_NO_THROW_LEAF"
    else:
        status = "UNGUARDED"
    return file or "NOT_FOUND", status, ""


def render(rows: list[tuple[str, str, str]]) -> str:
    lines: list[str] = []
    lines.append("# C ABI 安全矩阵")
    lines.append("")
    lines.append(
        "> 本文件由 `script/gen_abi_safety_matrix.py` 自动生成，禁止手改。"
    )
    lines.append("> R1.S 应将该脚本接入 CI，防止矩阵随 C API 扩展过期。")
    lines.append("")
    lines.append("## 状态说明")
    lines.append("")
    lines.append("- `GUARDED`：函数体包含 `GuardAbi(context, [&]{ ... })`；")
    lines.append("- `RAW_TRY`：有裸 `try/catch`，未统一走 `GuardAbi`；")
    lines.append("- `PROVEN_NO_THROW_LEAF`：无状态查询/常量 leaf，可证明不抛；")
    lines.append("- `UNGUARDED`：无异常边界。")
    lines.append("")

    by_file: dict[str, list[tuple[str, str]]] = {}
    for function, file, status in rows:
        by_file.setdefault(file, []).append((function, status))

    total = len(rows)
    guarded = sum(1 for _, _, s in rows if s == "GUARDED")
    raw = sum(1 for _, _, s in rows if s == "RAW_TRY")
    leaf = sum(1 for _, _, s in rows if s == "PROVEN_NO_THROW_LEAF")
    uncovered = total - guarded - raw - leaf
    lines.append("## 汇总")
    lines.append("")
    lines.append("| 状态 | 数量 |")
    lines.append("| ---- | ---- |")
    lines.append(f"| 总计 | {total} |")
    lines.append(f"| GUARDED | {guarded} |")
    lines.append(f"| RAW_TRY | {raw} |")
    lines.append(f"| PROVEN_NO_THROW_LEAF | {leaf} |")
    lines.append(f"| UNGUARDED | {uncovered} |")
    lines.append("")

    for file in sorted(by_file):
        entries = sorted(by_file[file])
        guarded_count = sum(1 for _, s in entries if s == "GUARDED")
        lines.append(f"## {file}")
        lines.append("")
        lines.append("| 函数 | 状态 |")
        lines.append("| ---- | ---- |")
        for function, status in entries:
            lines.append(f"| `{function}` | {status} |")
        lines.append("")
        lines.append(f"`{file}`：{guarded_count}/{len(entries)} GUARDED")
        lines.append("")

    lines.append("## 生成")
    lines.append("")
    lines.append("```bash")
    lines.append("python script/gen_abi_safety_matrix.py")
    lines.append("```")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the committed matrix is stale",
    )
    args = parser.parse_args()

    functions = exported_functions()
    sources = {
        path.name: path.read_text(encoding="utf-8")
        for path in NATIVE_DIR.glob("*.cpp")
    }
    rows = []
    for function in functions:
        file, status, _ = classify(function, sources)
        rows.append((function, file, status))
    rendered = render(rows)
    rendered += "\n"

    if args.check:
        current = MATRIX.read_text(encoding="utf-8")
        if current != rendered:
            print(
                "C_ABI_SAFETY_MATRIX.md is stale; "
                "run script/gen_abi_safety_matrix.py",
                file=sys.stderr,
            )
            return 1
        print(f"matrix up to date: {len(functions)} exported functions")
        return 0

    MATRIX.write_text(rendered, encoding="utf-8")
    print(f"wrote {MATRIX}: {len(functions)} exported functions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
