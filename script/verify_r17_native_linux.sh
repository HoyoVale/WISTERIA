#!/usr/bin/env bash
# R1.7 native-Linux release gate.
#
# Runs on a REAL independent Linux machine with hardware GL/EGL (no
# LIBGL_ALWAYS_SOFTWARE). WSL is a compatibility record, not this gate.
#
# Usage:
#   ./script/verify_r17_native_linux.sh            # headless-smoke release gate
#   ./script/verify_r17_native_linux.sh --core     # + CORE ctest
#   ./script/verify_r17_native_linux.sh --full     # + FULL ctest (full assets)
#
# The gate FAILS when the headless provider falls back to a software
# renderer: the R1.7 release baseline is hardware EGL on native Linux.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-linux-r17-gate"
MODE="smoke"

case "${1:-}" in
    "")
        MODE="smoke"
        ;;
    --core)
        MODE="core"
        ;;
    --full)
        MODE="full"
        ;;
    *)
        echo "未知选项: ${1:-}（可用: 无 / --core / --full）" >&2
        exit 2
        ;;
esac

FULL_ASSETS=OFF
if [ "${MODE}" = "full" ]; then
    FULL_ASSETS=ON
fi

echo "==> R1.7 native-Linux release gate (mode=${MODE})"
cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DWISTERIA_LINUX_WINDOW_BACKEND=X11 \
    -DWISTERIA_BUILD_NATIVE=ON \
    -DWISTERIA_TEST_FULL_ASSETS="${FULL_ASSETS}"

TARGETS=(wisteria_headless_smoke)
if [ "${MODE}" != "smoke" ]; then
    TARGETS+=(
        wisteria_unit_tests
        wisteria_runtime_tests
        wisteria_integration_tests
        wisteria_render_tests
        wisteria_abi_c_smoke
        wisteria_checkpoint_wire_cli
        wisteria_stable_checkpoint_cli
    )
fi
cmake --build "${BUILD_DIR}" --target "${TARGETS[@]}" -j "$(nproc)"

echo "==> headless smoke（硬件 EGL，无软件回退）"
OUTPUT="$(env -u DISPLAY -u WAYLAND_DISPLAY "${BUILD_DIR}/wisteria_headless_smoke" 2>&1)"
printf '%s\n' "${OUTPUT}"

echo "${OUTPUT}" | grep -q "session probe PASS" \
    || { echo "FAIL: session probe 未通过" >&2; exit 1; }
echo "${OUTPUT}" | grep -q "sequence probe PASS" \
    || { echo "FAIL: sequence probe 未通过" >&2; exit 1; }
echo "${OUTPUT}" | grep -q "software=no" \
    || { echo "FAIL: 未使用硬件 EGL renderer（software=no 缺失）" >&2; exit 1; }
echo "==> headless release gate PASS（硬件 EGL）"

if [ "${MODE}" != "smoke" ]; then
    echo "==> ctest（${MODE}）"
    (cd "${BUILD_DIR}" && ctest --output-on-failure)
fi

echo "==> R1.7 native-Linux gate ${MODE} PASS"
