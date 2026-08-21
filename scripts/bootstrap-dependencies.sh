#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASELINE_FILE="${ROOT_DIR}/cmake/vcpkg-baseline.txt"
VCPKG_COMMIT="$(tr -d '[:space:]' < "${BASELINE_FILE}")"
VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/.tools/vcpkg}"
TRIPLET="${VCPKG_DEFAULT_TRIPLET:-x64-linux}"

if [[ ! -d "${VCPKG_ROOT}/.git" ]]; then
  mkdir -p "$(dirname "${VCPKG_ROOT}")"
  git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
fi

git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_COMMIT}"
git -C "${VCPKG_ROOT}" checkout --detach "${VCPKG_COMMIT}"

"${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
"${VCPKG_ROOT}/vcpkg" install \
  --x-manifest-root="${ROOT_DIR}" \
  --triplet="${TRIPLET}" \
  --x-install-root="${ROOT_DIR}/build/vcpkg_installed"
