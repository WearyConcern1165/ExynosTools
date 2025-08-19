#!/bin/bash
set -e

# Simple build script for ExynosTools v1.3.0
# Fixes v1.2.0 empty binary issue by compiling directly with gcc

echo "🔧 ExynosTools v1.3.0 - Simple Build"

# Create build directory
mkdir -p build artifacts

# Compile all source files into a shared library
echo "🔨 Compiling libxeno_wrapper.so..."

gcc -shared -fPIC -O2 -DNDEBUG \
    -I./src \
    -o build/libxeno_wrapper.so \
    src/*.c \
    -lvulkan -lm -ldl

# Verify binary is not empty (critical check)
BINARY_SIZE=$(stat -c%s "build/libxeno_wrapper.so")
if [ "${BINARY_SIZE}" -eq 0 ]; then
    echo "❌ CRITICAL ERROR: Binary is empty!"
    exit 1
fi

echo "✅ Binary created: libxeno_wrapper.so (${BINARY_SIZE} bytes)"

# Create package structure
echo "📦 Creating package..."
mkdir -p build/pkg/usr/lib build/pkg/usr/share build/pkg/etc/exynostools

# Copy binary to correct location
cp build/libxeno_wrapper.so build/pkg/usr/lib/

# Copy metadata
cp usr/share/meta.json build/pkg/usr/share/

# Copy config files
cp etc/exynostools/performance_mode.conf build/pkg/etc/exynostools/
cp -r etc/exynostools/profiles build/pkg/etc/exynostools/

# Create tar.zst package
cd build/pkg
tar --zstd -cf ../../artifacts/exynostools-android-arm64.tar.zst .
cd ../..

PACKAGE_SIZE=$(stat -c%s "artifacts/exynostools-android-arm64.tar.zst")
echo ""
echo "🎉 Package created successfully!"
echo "📁 Location: artifacts/exynostools-android-arm64.tar.zst"
echo "📊 Size: ${PACKAGE_SIZE} bytes"
echo "✅ Binary: ${BINARY_SIZE} bytes (NOT empty like v1.2.0)"
