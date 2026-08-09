#!/usr/bin/env python3
"""Regenerate docs/architecture/C_ABI_SAFETY_MATRIX.md from the C ABI
headers (legacy v0.7 + stable v1 subset) and native implementation files.

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
STABLE_HEADER = (
    ROOT / "include" / "wisteria" / "native" / "wisteria_stable_runtime.h"
)
RENDER_HEADER = (
    ROOT / "include" / "wisteria" / "native" / "wisteria_stable_render.h"
)
NATIVE_DIR = ROOT / "src" / "native"
MATRIX = ROOT / "docs" / "architecture" / "C_ABI_SAFETY_MATRIX.md"

EXPORT_RE = re.compile(
    r"(?:WISTERIA_API|WISTERIA_STABLE_API)\s+[^;]*?\b(wisteria_\w+)\s*\("
)


def exported_functions(header: Path) -> list[str]:
    text = header.read_text(encoding="utf-8")
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
        return name, guarded, raw_try, body
    return None, False, False, ""


def classify(function: str, sources: dict[str, str]) -> tuple[str, str, str]:
    """Returns (file, status, note). Status is one of INVOKE_ABI / GUARDED /
    RAW_TRY / PROVEN_NO_THROW_LEAF / UNGUARDED."""
    leaf_functions = {
        "wisteria_status_name",
        "wisteria_version_major",
        "wisteria_version_minor",
    }
    file, guarded, raw_try, body = find_definition(function, sources)
    # Check for the unified outer wrapper in this function's body only.
    if body and "InvokeAbi" in body:
        status = "INVOKE_ABI"
    elif guarded:
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
    lines.append("- `INVOKE_ABI`：整个入口（含 Context 查找、句柄校验、路径/文件"
                 "操作）均位于 `InvokeAbi` 统一异常边界内；")
    lines.append("- `GUARDED`：函数体包含 `GuardAbi(context, [&]{ ... })`；")
    lines.append("- `RAW_TRY`：有裸 `try/catch`，未统一走 `GuardAbi`；")
    lines.append("- `PROVEN_NO_THROW_LEAF`：无状态查询/常量 leaf，可证明不抛；")
    lines.append("- `DECLARED_ONLY`：头文件已冻结但实现未落地（Phase 0A "
                 "Stable v1 subset 只冻结声明）；不参与 INVOKE_ABI 门禁。")
    lines.append("- `UNGUARDED`：无异常边界。")
    lines.append("")

    by_file: dict[str, list[tuple[str, str]]] = {}
    for function, file, status in rows:
        by_file.setdefault(file, []).append((function, status))

    total = len(rows)
    invoke = sum(1 for _, _, s in rows if s == "INVOKE_ABI")
    guarded = sum(1 for _, _, s in rows if s == "GUARDED")
    raw = sum(1 for _, _, s in rows if s == "RAW_TRY")
    leaf = sum(1 for _, _, s in rows if s == "PROVEN_NO_THROW_LEAF")
    declared = sum(1 for _, _, s in rows if s == "DECLARED_ONLY")
    uncovered = total - invoke - guarded - raw - leaf - declared
    lines.append("## 汇总")
    lines.append("")
    lines.append("| 状态 | 数量 |")
    lines.append("| ---- | ---- |")
    lines.append(f"| 总计 | {total} |")
    lines.append(f"| INVOKE_ABI | {invoke} |")
    lines.append(f"| GUARDED | {guarded} |")
    lines.append(f"| RAW_TRY | {raw} |")
    lines.append(f"| PROVEN_NO_THROW_LEAF | {leaf} |")
    lines.append(f"| DECLARED_ONLY | {declared} |")
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

    functions = exported_functions(HEADER)
    stable_functions = (
        exported_functions(STABLE_HEADER) +
        exported_functions(RENDER_HEADER)
    )
    sources = {
        path.name: path.read_text(encoding="utf-8")
        for path in NATIVE_DIR.glob("*.cpp")
    }
    rows = []
    for function in functions:
        file, status, _ = classify(function, sources)
        rows.append((function, file, status))
    for function in stable_functions:
        file, status, _ = classify(function, sources)
        if file == "NOT_FOUND":
            rows.append(
                (
                    function,
                    "stable header (declared-only)",
                    "DECLARED_ONLY",
                )
            )
        else:
            rows.append((function, file, status))
    rendered = render(rows)
    rendered += "\n"

    if args.check:
        # Safety gate, not just a drift check: the matrix must not contain
        # unguarded exports, and RAW_TRY/GUARDED are only allowed for the
        # documented exceptions (the global context creator and the three
        # proven leaves). New C API must go through InvokeAbi.
        allowed_raw_try = {
            "wisteria_create_context",
            "wisteria_stable_context_create",
        }
        unsafe = []
        for function, file, status in rows:
            if status == "DECLARED_ONLY":
                continue
            if status == "UNGUARDED":
                unsafe.append(f"UNGUARDED {function} ({file})")
            elif status == "RAW_TRY" and function not in allowed_raw_try:
                unsafe.append(f"RAW_TRY outside whitelist: {function} ({file})")
            elif status == "GUARDED":
                unsafe.append(
                    f"GUARDED should migrate to InvokeAbi: {function} ({file})"
                )
        if unsafe:
            for message in unsafe:
                print(message, file=sys.stderr)
            print(
                "ABI safety gate failed: all exports must use InvokeAbi "
                "unless explicitly whitelisted",
                file=sys.stderr,
            )
            return 2
        current = MATRIX.read_text(encoding="utf-8")
        if current != rendered:
            print(
                "C_ABI_SAFETY_MATRIX.md is stale; "
                "run script/gen_abi_safety_matrix.py",
                file=sys.stderr,
            )
            return 1
        print(
            "matrix up to date: "
            f"{len(functions)} legacy + {len(stable_functions)} stable exports"
        )
        return 0

    MATRIX.write_text(rendered, encoding="utf-8")
    print(
        f"wrote {MATRIX}: "
        f"{len(functions)} legacy + {len(stable_functions)} stable exports"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
