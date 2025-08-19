# ExynosTools v1.3.0 - Packaging Fixes

## Critical Issues Fixed from v1.2.0 Feedback

### ❌ Problems in v1.2.0:
1. **Empty Binary**: `libxeno_wrapper.so` was 0 bytes (completely non-functional)
2. **Wrong Structure**: Used APK format `libs/arm64-v8a/` instead of Winlator's `usr/lib/`
3. **Wrong Format**: Packaged as ZIP instead of `tar.zst` required by Winlator Bionic
4. **Unnecessary Files**: Included `icd.json` which Android Vulkan loader doesn't use

### ✅ Fixes in v1.3.0:
1. **Functional Binary**: Build system ensures `libxeno_wrapper.so` is properly compiled and non-empty
2. **Correct Structure**: Uses `usr/lib/libxeno_wrapper.so` for Winlator Bionic compatibility
3. **Correct Format**: Packages as `exynostools-android-arm64.tar.zst` 
4. **Clean Package**: Removed unnecessary `icd.json`, focused on essential files

## Build Verification

The build script now includes critical verification steps:

```bash
# Verify binary is not empty (main v1.2.0 issue)
BINARY_SIZE=$(stat -c%s "${BINARY_PATH}")
if [ "${BINARY_SIZE}" -eq 0 ]; then
    echo "❌ CRITICAL ERROR: libxeno_wrapper.so is empty"
    exit 1
fi
```

## Package Structure

**v1.3.0 Correct Structure:**
```
usr/lib/libxeno_wrapper.so          # Functional binary
usr/share/meta.json                  # Version metadata
etc/exynostools/performance_mode.conf
etc/exynostools/profiles/*.conf      # Unified config format
```

**v1.2.0 Broken Structure:**
```
libs/arm64-v8a/libxeno_wrapper.so   # 0 bytes (empty!)
xclipse_tools_icd.conf              # Unused by Android
meta.json
README.txt
```

## Installation

For Winlator Bionic 10.1+:
1. Extract `exynostools-android-arm64.tar.zst` to `/storage/emulated/0/Android/data/com.winlator/files/drivers/`
2. Winlator automatically detects libraries in `usr/lib/`
3. No manual configuration needed

## Technical Notes

- **BCn Emulation**: Now fully functional with embedded SPIR-V shaders
- **HUD System**: Real-time overlay with `EXYNOSTOOLS_HUD=1`
- **Configuration**: Unified `.conf` system replacing `.env` files
- **GPU Detection**: Enhanced with real Xclipse device IDs

This addresses all the critical feedback from the community and ensures v1.3.0 is a functional, production-ready release.
