#!/usr/bin/env bash
set -euo pipefail

project_root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
include_builds="${2:-}"

echo "Project root: ${project_root}"
find "${project_root}" -depth \( -name '.DS_Store' -o -name '__MACOSX' -o -name '._*' \) \
  -print -exec rm -rf {} +

if [[ "${include_builds}" == "--include-build-folders" ]]; then
  find "${project_root}" -maxdepth 1 -type d \( -name 'build' -o -name 'build-*' \) \
    -print -exec rm -rf {} +
fi

echo 'R1 cleanup complete.'
