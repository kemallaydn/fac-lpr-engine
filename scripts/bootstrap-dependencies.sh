#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASELINE_FILE="${ROOT_DIR}/cmake/vcpkg-baseline.txt"
VCPKG_COMMIT="$(tr -d '[:space:]' < "${BASELINE_FILE}")"
VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/.tools/vcpkg}"
TRIPLET="${VCPKG_DEFAULT_TRIPLET:-x64-linux}"

ORT_VERSION="$(tr -d '[:space:]' < "${ROOT_DIR}/cmake/onnxruntime-version.txt")"
ORT_SHA256="$(tr -d '[:space:]' < "${ROOT_DIR}/cmake/onnxruntime-linux-x64.sha256")"
ORT_ASSET="onnxruntime-linux-x64-${ORT_VERSION}.tgz"
ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_ASSET}"
ORT_CACHE_DIR="${ROOT_DIR}/.cache/onnxruntime"
ORT_ARCHIVE="${ORT_CACHE_DIR}/${ORT_ASSET}"
ORT_ROOT="${ROOT_DIR}/build/deps/onnxruntime"

if [[ "${TRIPLET}" != "x64-linux" ]]; then
  echo "Unsupported Linux triplet for prebuilt ONNX Runtime: ${TRIPLET}" >&2
  exit 2
fi

if [[ ! -d "${VCPKG_ROOT}/.git" ]]; then
  mkdir -p "$(dirname "${VCPKG_ROOT}")"
  git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
fi

git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_COMMIT}"
git -C "${VCPKG_ROOT}" checkout --detach "${VCPKG_COMMIT}"
"${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
"${VCPKG_ROOT}/vcpkg" install \
  --x-manifest-root="${ROOT_DIR}" \
  --triplet="${TRIPLET}" \
  --x-install-root="${ROOT_DIR}/build/vcpkg_installed"

if [[ ! -f "${ORT_ROOT}/include/onnxruntime_cxx_api.h" || ! -e "${ORT_ROOT}/lib/libonnxruntime.so" ]]; then
  mkdir -p "${ORT_CACHE_DIR}"
  if [[ -f "${ORT_ARCHIVE}" ]]; then
    ACTUAL_SHA256="$(sha256sum "${ORT_ARCHIVE}" | awk '{print $1}')"
    if [[ "${ACTUAL_SHA256}" != "${ORT_SHA256}" ]]; then
      rm -f "${ORT_ARCHIVE}"
    fi
  fi

  if [[ ! -f "${ORT_ARCHIVE}" ]]; then
    curl --fail --location --retry 3 --retry-delay 2 \
      --output "${ORT_ARCHIVE}.part" "${ORT_URL}"
    mv "${ORT_ARCHIVE}.part" "${ORT_ARCHIVE}"
  fi

  echo "${ORT_SHA256}  ${ORT_ARCHIVE}" | sha256sum --check --status
  TEMP_DIR="$(mktemp -d)"
  trap 'rm -rf "${TEMP_DIR}"' EXIT
  tar -xzf "${ORT_ARCHIVE}" -C "${TEMP_DIR}"
  rm -rf "${ORT_ROOT}"
  mkdir -p "${ORT_ROOT}"
  cp -a "${TEMP_DIR}/onnxruntime-linux-x64-${ORT_VERSION}/." "${ORT_ROOT}/"
  rm -rf "${TEMP_DIR}"
  trap - EXIT
fi

echo "Dependencies restored. FAC_LPR_ONNXRUNTIME_ROOT=${ORT_ROOT}"
