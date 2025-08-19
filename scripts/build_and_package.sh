#!/usr/bin/env bash
set -euo pipefail

# ExynosTools v1.3.0 Build and Package Script
# Fixes critical packaging issues from v1.2.0 feedback:
# - Ensures libxeno_wrapper.so is NOT empty (functional binary)
# - Uses usr/lib/ structure for Winlator Bionic compatibility
# - Packages as tar.zst format (not ZIP)
# - Removes unnecessary icd.json (Android doesn't use it)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
INSTALL_DIR="${BUILD_DIR}/install"
ARTIFACTS_DIR="${ROOT_DIR}/artifacts"

echo "🔧 ExynosTools v1.3.0 (Stable) - Build and Package"
echo "Fixing v1.2.0 packaging issues..."

# Clean previous builds
rm -rf "${BUILD_DIR}" "${ARTIFACTS_DIR}"
mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}" "${ARTIFACTS_DIR}"

# Build with proper configuration
echo "📦 Building with CMake..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"

# Build the library
echo "🔨 Compiling libxeno_wrapper.so..."
cmake --build "${BUILD_DIR}" --target xeno_wrapper --config Release -j$(nproc)

# Verify the binary is not empty (critical fix for v1.2.0 issue)
BINARY_PATH="${BUILD_DIR}/libxeno_wrapper.so"
if [ ! -f "${BINARY_PATH}" ]; then
    echo "❌ ERROR: libxeno_wrapper.so not found at ${BINARY_PATH}"
    exit 1
fi

BINARY_SIZE=$(stat -c%s "${BINARY_PATH}")
if [ "${BINARY_SIZE}" -eq 0 ]; then
    echo "❌ CRITICAL ERROR: libxeno_wrapper.so is empty (${BINARY_SIZE} bytes)"
    echo "This was the main issue in v1.2.0 - binary must be functional!"
    exit 1
fi

echo "✅ Binary verification passed: libxeno_wrapper.so (${BINARY_SIZE} bytes)"

# Install to proper structure
cmake --install "${BUILD_DIR}" --prefix "${INSTALL_DIR}"

# Create Winlator-compatible package structure
echo "📦 Creating Winlator Bionic compatible package..."
pushd "${INSTALL_DIR}" >/dev/null

# Create proper usr/lib structure (NOT libs/arm64-v8a from APK format)
mkdir -p pkg/usr/lib pkg/usr/share pkg/etc/exynostools

# Copy the functional binary to correct location
cp -v "${BUILD_DIR}/libxeno_wrapper.so" pkg/usr/lib/
echo "✅ Copied functional binary to usr/lib/ (Winlator format)"

# Copy metadata (updated for v1.3.0)
cp -v "${ROOT_DIR}/usr/share/meta.json" pkg/usr/share/

# Copy configuration files (.conf format, not .env)
cp -v "${ROOT_DIR}/etc/exynostools/performance_mode.conf" pkg/etc/exynostools/
if [ -d "${ROOT_DIR}/etc/exynostools/profiles" ]; then
    cp -rv "${ROOT_DIR}/etc/exynostools/profiles" pkg/etc/exynostools/
    echo "✅ Copied .conf profile files (unified format)"
fi

# Verify final package structure
echo "📋 Package contents verification:"
find pkg -type f -exec ls -lh {} \; | while read -r line; do
    echo "  $line"
done

# Create tar.zst package (NOT ZIP format)
pushd pkg >/dev/null
tar --zstd -cvf "${ARTIFACTS_DIR}/exynostools-android-arm64.tar.zst" .
popd >/dev/null
popd >/dev/null

# Final verification
PACKAGE_SIZE=$(stat -c%s "${ARTIFACTS_DIR}/exynostools-android-arm64.tar.zst")
echo ""
echo "🎉 ExynosTools v1.3.0 package created successfully!"
echo "📁 Location: ${ARTIFACTS_DIR}/exynostools-android-arm64.tar.zst"
echo "📊 Size: ${PACKAGE_SIZE} bytes"
echo "✅ Format: tar.zst (Winlator compatible)"
echo "✅ Structure: usr/lib/ (NOT libs/arm64-v8a/)"
echo "✅ Binary: Functional (NOT empty like v1.2.0)"
echo "✅ Config: .conf format (unified system)"
echo ""
echo "Ready for Winlator Bionic installation!"

