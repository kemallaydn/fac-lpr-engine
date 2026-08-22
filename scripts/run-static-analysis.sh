#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build/static-analysis}"

if ! command -v cppcheck >/dev/null 2>&1; then
  echo "cppcheck not found" >&2
  exit 2
fi
if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy not found" >&2
  exit 2
fi
if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "compile_commands.json not found at ${BUILD_DIR}" >&2
  exit 2
fi

mapfile -t SOURCES < <(
  find src tools tests \
    -type f -name '*.cpp' \
    ! -path 'tests/fixtures/*' \
    ! -path '*/build/*' \
    | sort
)

for source in "${SOURCES[@]}"; do
  if grep -Fq "\"$(pwd)/${source}\"" "${BUILD_DIR}/compile_commands.json" || \
     grep -Fq "\"${source}\"" "${BUILD_DIR}/compile_commands.json"; then
    clang-tidy -p "${BUILD_DIR}" --config-file .clang-tidy "${source}"
  fi
done

cppcheck \
  --project="${BUILD_DIR}/compile_commands.json" \
  --enable=warning,performance,portability \
  --error-exitcode=3 \
  --inline-suppr \
  --check-level=exhaustive \
  --suppress=missingIncludeSystem \
  --suppress=unmatchedSuppression \
  -i"$(pwd)/.tools" \
  -i"$(pwd)/build" \
  -i"$(pwd)/models" \
  -i"$(pwd)/tests/fixtures"
