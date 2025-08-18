#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
INSTALL_DIR="${BUILD_DIR}/install"
ARTIFACTS_DIR="${ROOT_DIR}/artifacts"

rm -rf "${BUILD_DIR}" "${ARTIFACTS_DIR}"
mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}" "${ARTIFACTS_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target xeno_wrapper --config Release -j
cmake --install "${BUILD_DIR}" --prefix "${INSTALL_DIR}"

# Package to tar.zst with usr/lib layout for Winlator Bionic
pushd "${INSTALL_DIR}" >/dev/null
mkdir -p pkg
cp -r "${INSTALL_DIR}/usr" pkg/
cp -r "${ROOT_DIR}/usr/share" pkg/usr/
mkdir -p pkg/etc/exynostools pkg/profiles/winlator pkg/assets/shaders/decode
cp -v "${ROOT_DIR}/etc/exynostools/performance_mode.conf" pkg/etc/exynostools/
if [ -d "${ROOT_DIR}/etc/exynostools/profiles" ]; then
  cp -rv "${ROOT_DIR}/etc/exynostools/profiles" pkg/etc/exynostools/
fi
cp -v ${ROOT_DIR}/profiles/winlator/*.env pkg/profiles/winlator/
cp -v ${ROOT_DIR}/assets/shaders/decode/*.spv pkg/assets/shaders/decode/
pushd pkg >/dev/null
tar --zstd -cvf "${ARTIFACTS_DIR}/exynostools-android-arm64.tar.zst" .
popd >/dev/null
popd >/dev/null

echo "Created artifact at: ${ARTIFACTS_DIR}/exynostools-android-arm64.tar.zst"

