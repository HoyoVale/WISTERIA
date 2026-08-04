#!/usr/bin/env bash
# Build and manually verify WISTERIA desktop rendering on Linux/WSLg.
# Runs the C++ demo, the Python C ABI demo, and a two-context lifetime demo.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIGURATION="RelWithDebInfo"
BACKEND="${WISTERIA_LINUX_WINDOW_BACKEND:-X11}"
GENERATOR="Ninja"
BUILD_DIR="${ROOT}/build-linux-verify"
FRAMES=180
MODEL=""
MOTION=""
SKIP_NATIVE_DEMOS=0
SKIP_BUILD=0
PIXEL_PROBE=0
DISABLE_OIT=0
DISABLE_SKYBOX=0
DISABLE_CAMERA_MOTION=0
CAPTURE_SOURCE="${WISTERIA_SCREENSHOT_SOURCE:-scene}"
OUTPUT_ROOT=""

usage() {
    cat <<'EOF'
Usage: ./script/verify_render.sh [options]

Options:
  --backend X11|WAYLAND|BOTH|NULL   GLFW Linux backend (default X11)
  --configuration <name>           Debug, Release, RelWithDebInfo
  --generator <name>               CMake generator (default Ninja)
  --build-dir <path>               Verification build directory
  --frames <n>                     Frames per demo (default 180)
  --model <pmx>                    Override character PMX
  --motion <vmd>                   Override VMD motion
  --capture-source scene|default|none
                                      Capture engine Scene FBO (default),
                                      platform default framebuffer, or disable
  --skip-native-demos              Only run the C++ desktop demo
  --skip-build                     Reuse an existing configured build
  --pixel-probe                    Log center Scene-FBO pixel after clear/render
  --disable-oit                    Bypass weighted OIT; use normal alpha blend
  --disable-skybox                 Skip skybox drawing (IBL remains enabled)
  --disable-camera-motion          Keep the default/free camera
  -h, --help                       Show this help

NULL only compiles and runs CTest; it cannot open a desktop window.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend) BACKEND="${2^^}"; shift 2 ;;
        --configuration) CONFIGURATION="$2"; shift 2 ;;
        --generator) GENERATOR="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --model) MODEL="$2"; shift 2 ;;
        --motion) MOTION="$2"; shift 2 ;;
        --capture-source) CAPTURE_SOURCE="${2,,}"; shift 2 ;;
        --skip-native-demos) SKIP_NATIVE_DEMOS=1; shift ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --pixel-probe) PIXEL_PROBE=1; shift ;;
        --disable-oit) DISABLE_OIT=1; shift ;;
        --disable-skybox) DISABLE_SKYBOX=1; shift ;;
        --disable-camera-motion) DISABLE_CAMERA_MOTION=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "${BACKEND}" in X11|WAYLAND|BOTH|NULL) ;; *) echo "Invalid backend: ${BACKEND}" >&2; exit 2 ;; esac
case "${CONFIGURATION}" in Debug|Release|RelWithDebInfo) ;; *) echo "Invalid configuration" >&2; exit 2 ;; esac
case "${CAPTURE_SOURCE}" in scene|default|none) ;; *) echo "Invalid capture source: ${CAPTURE_SOURCE}" >&2; exit 2 ;; esac
OUTPUT_ROOT="${ROOT}/artifacts/render-smoke/linux/${BACKEND,,}"
if ! [[ "${FRAMES}" =~ ^[0-9]+$ ]] || (( FRAMES < 60 )); then
    echo "--frames must be an integer >= 60" >&2
    exit 2
fi

command -v cmake >/dev/null || { echo "cmake not found" >&2; exit 2; }
command -v ctest >/dev/null || { echo "ctest not found" >&2; exit 2; }

if (( SKIP_BUILD == 0 )); then
    echo "==> Configure Linux verification build (backend=${BACKEND}, native=ON)"
    cmake -S "${ROOT}" -B "${BUILD_DIR}" -G "${GENERATOR}" \
        -DCMAKE_BUILD_TYPE="${CONFIGURATION}" \
        -DWISTERIA_LINUX_WINDOW_BACKEND="${BACKEND}" \
        -DWISTERIA_BUILD_NATIVE=ON \
        -DBUILD_TESTING=ON

    echo "==> Build wisteria / tests / native"
    cmake --build "${BUILD_DIR}" \
        --target wisteria wisteria_tests wisteria_native \
        -j "$(nproc)"
fi

echo "==> Run CTest"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

if [[ "${BACKEND}" == "NULL" ]]; then
    echo "NULL backend verification complete. Choose X11 or WAYLAND for real rendering."
    exit 0
fi

command -v python3 >/dev/null || {
    echo "python3 is required to validate render captures" >&2
    exit 2
}

if [[ "${BACKEND}" == "X11" || "${BACKEND}" == "BOTH" ]]; then
    if [[ -z "${DISPLAY:-}" ]]; then
        echo "DISPLAY is empty; X11/WSLg window creation will fail." >&2
        exit 2
    fi
fi
if [[ "${BACKEND}" == "WAYLAND" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "WAYLAND_DISPLAY is empty; Wayland window creation will fail." >&2
    exit 2
fi

if command -v glxinfo >/dev/null && [[ -n "${DISPLAY:-}" ]]; then
    echo "==> OpenGL summary"
    glxinfo -B || true
elif command -v eglinfo >/dev/null; then
    echo "==> EGL summary"
    eglinfo -B 2>/dev/null || true
fi

WISTERIA_EXE="${BUILD_DIR}/wisteria"
NATIVE_LIB="${BUILD_DIR}/libwisteria_native.so"
[[ -x "${WISTERIA_EXE}" ]] || { echo "Missing ${WISTERIA_EXE}" >&2; exit 2; }
[[ -f "${NATIVE_LIB}" ]] || { echo "Missing ${NATIVE_LIB}" >&2; exit 2; }

export WISTERIA_ASSET_ROOT="${ROOT}/assets"
export WISTERIA_FRAME_PROFILE=1
export WISTERIA_GL_DIAGNOSTICS=1
export WISTERIA_SCREENSHOT_INTERVAL=30
if (( PIXEL_PROBE == 1 )); then export WISTERIA_GL_PIXEL_PROBE=1; else unset WISTERIA_GL_PIXEL_PROBE || true; fi
if (( DISABLE_OIT == 1 )); then export WISTERIA_DISABLE_OIT=1; else unset WISTERIA_DISABLE_OIT || true; fi
if (( DISABLE_SKYBOX == 1 )); then export WISTERIA_DISABLE_SKYBOX=1; else unset WISTERIA_DISABLE_SKYBOX || true; fi
if (( DISABLE_CAMERA_MOTION == 1 )); then export WISTERIA_DISABLE_CAMERA_MOTION=1; else unset WISTERIA_DISABLE_CAMERA_MOTION || true; fi
export WISTERIA_SCREENSHOT_SOURCE="${CAPTURE_SOURCE}"
export WISTERIA_NATIVE_LIB="${NATIVE_LIB}"
export LD_LIBRARY_PATH="${BUILD_DIR}:${LD_LIBRARY_PATH:-}"

reset_directory() {
    rm -rf "$1"
    mkdir -p "$1"
}

show_capture_summary() {
    local directory="$1"
    python3 "${ROOT}/script/analyze_render_captures.py" "${directory}"
}

common_demo_args=(--frames "${FRAMES}" --fixed-dt 0.016666667)
python_demo_args=(--frames "${FRAMES}")
if [[ -n "${MODEL}" ]]; then
    common_demo_args+=(--model "${MODEL}")
    python_demo_args+=(--model "${MODEL}")
fi
if [[ -n "${MOTION}" ]]; then
    common_demo_args+=(--motion "${MOTION}")
    python_demo_args+=(--motion "${MOTION}")
fi

DESKTOP_OUTPUT="${OUTPUT_ROOT}/desktop"
reset_directory "${DESKTOP_OUTPUT}"
if [[ "${CAPTURE_SOURCE}" == "none" ]]; then
    unset WISTERIA_SCREENSHOT_DIR
else
    export WISTERIA_SCREENSHOT_DIR="${DESKTOP_OUTPUT}"
fi
echo "==> C++ desktop demo: ${FRAMES} fixed frames (capture=${CAPTURE_SOURCE})"
(
    cd "${ROOT}"
    "${WISTERIA_EXE}" "${common_demo_args[@]}" 2>&1 | tee "${DESKTOP_OUTPUT}/run.log"
)
if [[ "${CAPTURE_SOURCE}" != "none" ]]; then
    show_capture_summary "${DESKTOP_OUTPUT}"
fi

if (( SKIP_NATIVE_DEMOS == 0 )); then
    NATIVE_OUTPUT="${OUTPUT_ROOT}/native-single"
    reset_directory "${NATIVE_OUTPUT}"
    if [[ "${CAPTURE_SOURCE}" == "none" ]]; then
        unset WISTERIA_SCREENSHOT_DIR
    else
        export WISTERIA_SCREENSHOT_DIR="${NATIVE_OUTPUT}"
    fi
    echo "==> Python ctypes single-context demo"
    (
        cd "${ROOT}"
        python3 examples/python/native_window_demo.py "${python_demo_args[@]}" \
            2>&1 | tee "${NATIVE_OUTPUT}/run.log"
    )
    if [[ "${CAPTURE_SOURCE}" != "none" ]]; then
        show_capture_summary "${NATIVE_OUTPUT}"
    fi

    MULTI_OUTPUT="${OUTPUT_ROOT}/native-multi"
    reset_directory "${MULTI_OUTPUT}"
    if [[ "${CAPTURE_SOURCE}" == "none" ]]; then
        unset WISTERIA_SCREENSHOT_DIR
    else
        export WISTERIA_SCREENSHOT_DIR="${MULTI_OUTPUT}"
    fi
    DESTROY_AT=$(( FRAMES / 2 ))
    (( DESTROY_AT < 30 )) && DESTROY_AT=30
    multi_args=(--frames "${FRAMES}" --destroy-first-at "${DESTROY_AT}")
    [[ -n "${MODEL}" ]] && multi_args+=(--model "${MODEL}")
    [[ -n "${MOTION}" ]] && multi_args+=(--motion "${MOTION}")
    echo "==> Python ctypes two-context lifetime demo"
    (
        cd "${ROOT}"
        python3 examples/python/native_multi_context_demo.py "${multi_args[@]}" \
            2>&1 | tee "${MULTI_OUTPUT}/run.log"
    )
    if [[ "${CAPTURE_SOURCE}" != "none" ]]; then
        show_capture_summary "${MULTI_OUTPUT}"
    fi
fi

echo "Verification complete: ${OUTPUT_ROOT}"
