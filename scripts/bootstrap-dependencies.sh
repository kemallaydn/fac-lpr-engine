#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASELINE_FILE="${ROOT_DIR}/cmake/vcpkg-baseline.txt"
VCPKG_COMMIT="$(tr -d '[:space:]' < "${BASELINE_FILE}")"
VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/.tools/vcpkg}"
VCPKG_INSTALLED_DIR="${FAC_LPR_VCPKG_INSTALLED_DIR:-${ROOT_DIR}/build/vcpkg_installed}"

OS_NAME="$(uname -s)"
ARCH_NAME="$(uname -m)"
ORT_VERSION="$(tr -d '[:space:]' < "${ROOT_DIR}/cmake/onnxruntime-version.txt")"
ORT_CACHE_DIR="${ROOT_DIR}/.cache/onnxruntime"
ORT_ROOT="${ROOT_DIR}/build/deps/onnxruntime"

case "${OS_NAME}:${ARCH_NAME}" in
  Linux:x86_64)
    DEFAULT_TRIPLET="x64-linux"
    ORT_PLATFORM="linux-x64"
    ORT_LIBRARY="${ORT_ROOT}/lib/libonnxruntime.so"
    ORT_SHA_FILE="${ROOT_DIR}/cmake/onnxruntime-linux-x64.sha256"
    ;;
  Darwin:arm64)
    DEFAULT_TRIPLET="arm64-osx"
    ORT_PLATFORM="osx-arm64"
    ORT_LIBRARY="${ORT_ROOT}/lib/libonnxruntime.dylib"
    ORT_SHA_FILE="${ROOT_DIR}/cmake/onnxruntime-osx-arm64.sha256"
    ;;
  *)
    echo "Unsupported bootstrap platform: ${OS_NAME}/${ARCH_NAME}" >&2
    exit 2
    ;;
esac

TRIPLET="${VCPKG_DEFAULT_TRIPLET:-${DEFAULT_TRIPLET}}"
if [[ "${TRIPLET}" != "${DEFAULT_TRIPLET}" ]]; then
  echo "Unsupported triplet ${TRIPLET} for ${OS_NAME}/${ARCH_NAME}; expected ${DEFAULT_TRIPLET}" >&2
  exit 2
fi

ORT_SHA256="$(tr -d '[:space:]' < "${ORT_SHA_FILE}")"
ORT_ASSET="onnxruntime-${ORT_PLATFORM}-${ORT_VERSION}.tgz"
ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_ASSET}"
ORT_ARCHIVE="${ORT_CACHE_DIR}/${ORT_ASSET}"
ORT_EXTRACTED_DIR="onnxruntime-${ORT_PLATFORM}-${ORT_VERSION}"

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

if [[ ! -d "${VCPKG_ROOT}/.git" ]]; then
  mkdir -p "$(dirname "${VCPKG_ROOT}")"
  git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
fi

git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_COMMIT}"
git -C "${VCPKG_ROOT}" checkout --detach "${VCPKG_COMMIT}"
"${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
mkdir -p "${VCPKG_INSTALLED_DIR}"
"${VCPKG_ROOT}/vcpkg" install \
  --x-manifest-root="${ROOT_DIR}" \
  --triplet="${TRIPLET}" \
  --x-install-root="${VCPKG_INSTALLED_DIR}"

if [[ ! -f "${ORT_ROOT}/include/onnxruntime_cxx_api.h" || ! -e "${ORT_LIBRARY}" ]]; then
  mkdir -p "${ORT_CACHE_DIR}"
  if [[ -f "${ORT_ARCHIVE}" ]]; then
    ACTUAL_SHA256="$(sha256_file "${ORT_ARCHIVE}")"
    if [[ "${ACTUAL_SHA256}" != "${ORT_SHA256}" ]]; then
      echo "Cached ONNX Runtime checksum mismatch; re-downloading" >&2
      rm -f "${ORT_ARCHIVE}"
    fi
  fi

  if [[ ! -f "${ORT_ARCHIVE}" ]]; then
    curl --fail --location --retry 3 --retry-delay 2 \
      --output "${ORT_ARCHIVE}.part" "${ORT_URL}"
    mv "${ORT_ARCHIVE}.part" "${ORT_ARCHIVE}"
  fi

  ACTUAL_SHA256="$(sha256_file "${ORT_ARCHIVE}")"
  if [[ "${ACTUAL_SHA256}" != "${ORT_SHA256}" ]]; then
    echo "ONNX Runtime checksum mismatch: expected=${ORT_SHA256} actual=${ACTUAL_SHA256}" >&2
    exit 3
  fi

  TEMP_DIR="$(mktemp -d)"
  trap 'rm -rf "${TEMP_DIR}"' EXIT
  tar -xzf "${ORT_ARCHIVE}" -C "${TEMP_DIR}"
  rm -rf "${ORT_ROOT}"
  mkdir -p "${ORT_ROOT}"
  cp -a "${TEMP_DIR}/${ORT_EXTRACTED_DIR}/." "${ORT_ROOT}/"
  rm -rf "${TEMP_DIR}"
  trap - EXIT
fi

echo "Dependencies restored for ${OS_NAME}/${ARCH_NAME}."
echo "VCPKG_DEFAULT_TRIPLET=${TRIPLET}"
echo "FAC_LPR_VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
echo "FAC_LPR_ONNXRUNTIME_ROOT=${ORT_ROOT}"
