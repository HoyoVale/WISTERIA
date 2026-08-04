#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
rm -f "${ROOT}/src/native/wisteria_native.cpp"
chmod +x "${ROOT}/build_linux.sh" \
    "${ROOT}/script/verify_render.sh" \
    "${ROOT}/examples/python/native_window_demo.py" \
    "${ROOT}/examples/python/native_multi_context_demo.py"
echo "Patch cleanup complete. Run: ./script/verify_render.sh --backend X11"
