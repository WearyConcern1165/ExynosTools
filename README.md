<div align="center">
  <h1>🚀 ExynosTools (Granite Edition)</h1>
  <p><b>The Ultimate Custom Graphics Driver Wrapper for Samsung Xclipse GPUs</b></p>
  
  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
  [![Platform](https://img.shields.io/badge/Platform-Android-green.svg)]()
  [![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)]()
</div>

## 📌 Overview

**ExynosTools** is an open-source Vulkan Intercept Driver (ICD Wrapper) tailored specifically for Samsung smartphones running **Exynos RDNA2, RDNA3, and RDNA4** GPUs.

Modern PC games heavily rely on `BCn` texture compression (BC1 to BC7), which Android hardware organically lacks. Emulators like **Winlator** and **Skyline** suffer extreme CPU overhead attempting to decode these formats in software, leading to crashes, pixelated colors, and low framerates on Samsung devices.

ExynosTools intercepts these Vulkan API calls and injects **High-Performance Granite Engine Compute Shaders** to decode heavy PC textures natively on the GPU's memory.

## 🌟 Key Features

- **Granite Engine Decoding**: Blazing-fast shaders porting Hans-Kristian Arntzen's Granite Engine architecture, utilizing AMD's Local Data Share (LDS).
- **Native Hardware Passthrough**: Bypasses software emulation for BC1, BC2, and BC3 textures, letting the Xclipse fixed-function hardware decode it natively.
- **D24 to D32 Depth Spoofing**: Fixes broken shadows and lighting in emulators by translating depth formats.
- **VRAM Budget Spoofer**: Fakes the system's available memory up to 8GB to prevent out-of-video-memory crashes in AAA games like *GTA V* and *Cyberpunk 2077*.
- **Immortal Mode**: Auto-patches broken SPIR-V shaders injected by emulators that would normally crash RDNA GPUs.

## 🛠️ Installation (For End Users)

1. Download the latest `ExynosTools_V3.0_Emulator_Install.zip` from the [Releases](https://github.com/WearyConcern1165/ExynosTools/releases) page.
2. Unzip it and place `libvulkan_xclipse.so` into your emulator's driver folder (e.g., Winlator Custom Drivers directory).
3. Optionally copy `exynostools_config.ini` to `/sdcard/ExynosTools/` on your phone to configure the Watchdog and VRAM thresholds.

## 💻 Compilation (For Developers)

### Prerequisites
- Windows / Linux
- [CMake 3.20+](https://cmake.org/download/)
- [Ninja Build System](https://ninja-build.org/)
- **Android NDK** (r26d recommended)

### Build Instructions

The project uses a unified script to generate the Android Shared Library (`.so`).
```powershell
# Windows
.\build.bat
```
*(The script automatically uses `glslc` to compile inline shaders to SPIR-V and links everything via Ninja).*

The output will be placed in `build-android/libvulkan_xclipse.so`.

## 📜 License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for more information.

> Crafted by **Marco** for the Exynos Android Emulation Community.
