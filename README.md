# ExynosTools V3.1 Granite Edition

ExynosTools is a Vulkan wrapper and optional debug layer for Samsung Xclipse GPUs on Android.
Its main goal is to intercept BCn texture uploads from Windows translation layers and emulators,
then decode them safely on the GPU with embedded Granite-based compute shaders.

## What This Version Does

- Intercepts BC1, BC2, BC3, BC4, BC5, BC6H, and BC7 texture uploads.
- Records inline compute decode work through the C++ decoder path.
- Uses AMD VMA for tracked Vulkan memory allocation hooks.
- Preserves Geometry and Tessellation shader support instead of patching shaders.
- Passes through cleanly on non-Xclipse devices.

## What Was Removed

- The watchdog path that masked `VK_ERROR_DEVICE_LOST`.
- The SPIR-V patcher runtime path.
- The broken micro-VMA allocator.
- The old C BCn dispatcher path.

## Current Layout

- Main driver library: `libvulkan_xclipse.so`
- Optional layer manifest: `VkLayer_exynostools.json`
- Driver package manifest: `meta.json`
- Source tree:
  - `src/`
  - `include/`
  - `shaders/`
  - `cmake/`
  - `tests/`

## Build

### Requirements

- CMake 3.18+
- Ninja
- Android NDK r26d or newer

### Windows

```powershell
.\build.bat
```

Expected output:

- `build-arm64/libvulkan_xclipse.so`
- generated SPIR-V headers under `build-arm64/shaders/`

## Runtime Notes

- The wrapper targets Samsung Xclipse GPUs detected through Vulkan properties.
- BCn decode now uses the C++ decoder integration instead of the removed C dispatcher.
- The package in `dist/` is structured as a standard Android driver layout with `lib/arm64-v8a/`.

## Status

This version builds successfully for Android ARM64 and is intended to be tested on real Xclipse devices.
If a game still fails, the next debugging step is runtime validation with `adb logcat`.

## License

MIT. See [LICENSE](LICENSE).
