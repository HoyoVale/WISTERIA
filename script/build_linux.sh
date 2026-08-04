#!/usr/bin/env bash
# WSL / Linux build entry for WISTERIA.
#   ./script/build_linux.sh [build_dir] [cmake_generator]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT}/build-linux}"
GENERATOR="${2:-Ninja}"

cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$BUILD_DIR" --target wisteria_native wisteria_tests -j \
    "$(nproc)"

"$BUILD_DIR/wisteria_tests"
