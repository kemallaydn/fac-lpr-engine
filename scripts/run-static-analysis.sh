#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build/static-analysis}"

if ! command -v cppcheck >/dev/null 2>&1; then
  echo "cppcheck not found" >&2
  exit 2
fi
if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "compile_commands.json not found at ${BUILD_DIR}" >&2
  exit 2
fi

cppcheck \
  --project="${BUILD_DIR}/compile_commands.json" \
  --enable=warning,performance,portability \
  --error-exitcode=3 \
  -i"$(pwd)/.tools" \
  -i"$(pwd)/build" \
  -i"$(pwd)/models" \
  -i"$(pwd)/tests/fixtures"
