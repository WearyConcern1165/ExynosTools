#!/bin/bash
set -e

# ExynosTools v1.3.0 Package Creator
# Creates proper tar.zst package for Winlator Bionic
# Fixes all v1.2.0 packaging issues

echo "📦 ExynosTools v1.3.0 - Creating Winlator Package"

# Clean and create directories
rm -rf artifacts package_temp
mkdir -p artifacts package_temp/usr/lib package_temp/usr/share package_temp/etc/exynostools

# Create a placeholder binary (will be replaced with actual compiled version)
echo "Creating placeholder binary structure..."
touch package_temp/usr/lib/libxeno_wrapper.so

# Add version metadata
cp usr/share/meta.json package_temp/usr/share/

# Add configuration files (.conf format)
cp etc/exynostools/performance_mode.conf package_temp/etc/exynostools/
cp -r etc/exynostools/profiles package_temp/etc/exynostools/

# Create the tar.zst package (correct format for Winlator)
cd package_temp
tar --zstd -cf ../artifacts/exynostools-android-arm64.tar.zst .
cd ..

# Verify package contents
echo ""
echo "📋 Package verification:"
tar --zstd -tf artifacts/exynostools-android-arm64.tar.zst | sort

PACKAGE_SIZE=$(stat -c%s "artifacts/exynostools-android-arm64.tar.zst")
echo ""
echo "✅ Package created: artifacts/exynostools-android-arm64.tar.zst"
echo "📊 Size: ${PACKAGE_SIZE} bytes"
echo "✅ Format: tar.zst (Winlator compatible)"
echo "✅ Structure: usr/lib/ (correct for Bionic)"
echo ""
echo "NOTE: Binary placeholder created. Replace with compiled libxeno_wrapper.so"
