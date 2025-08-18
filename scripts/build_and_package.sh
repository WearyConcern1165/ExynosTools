#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
INSTALL_DIR="${BUILD_DIR}/install"
ARTIFACTS_DIR="${ROOT_DIR}/artifacts"

rm -rf "${BUILD_DIR}" "${ARTIFACTS_DIR}"
mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}" "${ARTIFACTS_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target xclipse_wrapper --config Release -j
cmake --install "${BUILD_DIR}" --prefix "${INSTALL_DIR}"

# Package to tar.zst with usr/lib layout for Winlator Bionic
pushd "${INSTALL_DIR}" >/dev/null
tar --zstd -cvf "${ARTIFACTS_DIR}/xclipse_tools_stable_v1.2.0.tar.zst" usr
popd >/dev/null

echo "Created artifact at: ${ARTIFACTS_DIR}/xclipse_tools_stable_v1.2.0.tar.zst"

