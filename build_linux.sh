#!/usr/bin/env bash
# Linux / WSL build entry for WISTERIA (run.ps1 的 Linux 版).
#
#   ./build_linux.sh [action] [options] [-- demo args...]
#
# Actions:
#   configure   只做 CMake 配置（生成 build-linux）
#   build       配置 + 编译启用的目标（wisteria / 可选 wisteria_native / wisteria_tests）
#   compile     同 build（兼容 run.ps1 的习惯叫法）
#   test        配置 + 编译 + 运行 wisteria_tests
#   run         配置 + 编译 + 运行窗口 demo，后面可跟 demo 参数
#   clean       删除 build-linux（默认目录，或 -B 指定的目录）
#   help        显示本帮助
#
# Options:
#   -c, --configuration <Debug|Release|RelWithDebInfo>   默认 RelWithDebInfo
#   -G, --generator <Ninja|Unix Makefiles>               默认 Ninja
#   -B, --build-dir <dir>                                默认 <root>/build-linux
#   --backend <X11|WAYLAND|BOTH|NULL>                     默认 X11
#   --native / --no-native                                构建/跳过实验 C ABI
#   -h, --help                                           显示帮助
#   --                                                  后面全部作为 demo 参数
#
# 默认 action 是 test（配置 + 编译 + 跑测试）。

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ACTION=""
CONFIGURATION="RelWithDebInfo"
GENERATOR="Ninja"
BUILD_DIR="${ROOT}/build-linux"
LINUX_BACKEND="${WISTERIA_LINUX_WINDOW_BACKEND:-X11}"
BUILD_NATIVE="${WISTERIA_BUILD_NATIVE:-OFF}"
APP_ARGS=()

ShowHelp()
{
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print } NR > 1 && !/^#/ { exit }' \
        "${BASH_SOURCE[0]}"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        -c|--configuration)
            CONFIGURATION="$2"
            shift 2
            ;;
        -G|--generator)
            GENERATOR="$2"
            shift 2
            ;;
        -B|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --backend)
            LINUX_BACKEND="${2^^}"
            shift 2
            ;;
        --native)
            BUILD_NATIVE="ON"
            shift
            ;;
        --no-native)
            BUILD_NATIVE="OFF"
            shift
            ;;
        -h|--help)
            ShowHelp
            ;;
        --)
            shift
            APP_ARGS=("$@")
            break
            ;;
        -*)
            if [ -n "${ACTION}" ]; then
                # 动作之后以 - 开头的参数一律透传给 demo（例如 --model）。
                APP_ARGS+=("$1")
                shift
            else
                echo "未知选项: $1（用 --help 查看用法）" >&2
                exit 2
            fi
            ;;
        *)
            if [ -z "${ACTION}" ]; then
                ACTION="$1"
            else
                APP_ARGS+=("$1")
            fi
            shift
            ;;
    esac
done

if [ -z "${ACTION}" ]; then
    ACTION="test"
fi

case "${CONFIGURATION}" in
    Debug|Release|RelWithDebInfo) ;;
    *)
        echo "无效配置: ${CONFIGURATION}（可选 Debug/Release/RelWithDebInfo）" >&2
        exit 2
        ;;
esac

case "${LINUX_BACKEND}" in
    X11|WAYLAND|BOTH|NULL) ;;
    *)
        echo "无效 Linux 后端: ${LINUX_BACKEND}" >&2
        exit 2
        ;;
esac

case "${BUILD_NATIVE}" in
    ON|OFF) ;;
    *)
        echo "WISTERIA_BUILD_NATIVE 只能是 ON 或 OFF" >&2
        exit 2
        ;;
esac

Configure()
{
    echo "==> CMake 配置 [${CONFIGURATION}] (${GENERATOR})"
    cmake -S "${ROOT}" -B "${BUILD_DIR}" -G "${GENERATOR}" \
        -DCMAKE_BUILD_TYPE="${CONFIGURATION}" \
        -DWISTERIA_LINUX_WINDOW_BACKEND="${LINUX_BACKEND}" \
        -DWISTERIA_BUILD_NATIVE="${BUILD_NATIVE}"
}

Build()
{
    Configure
    local targets=(wisteria wisteria_tests)
    if [ "${BUILD_NATIVE}" = "ON" ]; then
        targets+=(wisteria_native)
    fi
    echo "==> 编译 ${targets[*]}（backend=${LINUX_BACKEND}）"
    cmake --build "${BUILD_DIR}" \
        --target "${targets[@]}" \
        -j "$(nproc)"
}

Clean()
{
    # 只允许删除项目内由本脚本创建的构建目录，防止误删用户数据。
    local resolved
    resolved="$(cd "${BUILD_DIR}" 2>/dev/null && pwd)" || resolved="${BUILD_DIR}"
    case "${resolved}/" in
        "${ROOT}/"*) ;;
        *)
            echo "拒绝删除项目外的目录: ${resolved}" >&2
            exit 2
            ;;
    esac
    if [ "${resolved}" = "${ROOT}" ]; then
        echo "拒绝删除项目根目录" >&2
        exit 2
    fi
    echo "==> 删除 ${resolved}"
    rm -rf "${resolved}"
}

case "${ACTION}" in
    configure)
        Configure
        ;;
    build|compile)
        Build
        ;;
    test)
        Build
        echo "==> 运行测试"
        "${BUILD_DIR}/wisteria_tests"
        ;;
    run)
        if [ "${LINUX_BACKEND}" = "NULL" ]; then
            echo "NULL 后端不能运行桌面窗口；请选择 X11 或 WAYLAND" >&2
            exit 2
        fi
        Build
        echo "==> 运行窗口 demo"
        exec "${BUILD_DIR}/wisteria" "${APP_ARGS[@]}"
        ;;
    clean)
        Clean
        ;;
    help)
        ShowHelp
        ;;
    *)
        echo "未知动作: ${ACTION}（可用 configure/build/compile/test/run/clean/help）" >&2
        exit 2
        ;;
esac
