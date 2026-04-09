# ExynosTools Fix 3.0

Stable Vulkan Layer base focused on Samsung Xclipse GPUs (Exynos), with BCn virtualization and compute decode.

## Why this repo exists

This branch was rebuilt to remove fragile wrapper behavior and keep a clean architecture:

- Official Vulkan Layer flow (no mixed fake ICD path).
- Safe dispatch chaining (`vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`).
- BCn format virtualization for Xclipse.
- Compute decode path using Granite-based shaders.
- VMA-based temporary staging allocations.

## Current status

- Android `arm64-v8a` build working.
- Format virtualization active:
  - `vkGetPhysicalDeviceFormatProperties(2)`
  - `vkCreateImage`
  - `vkCreateImageView`
- BCn decode path active in:
  - `vkCmdCopyBufferToImage`
  - `vkCmdCopyBufferToImage2`
  - `vkCmdCopyBufferToImage2KHR`
- Single-file mode ready:
  - SPIR-V decode shaders are embedded inside `libVkLayer_ExynosTools.so`.
  - External runtime `shaders/` folder is not required for the single-file package.

## Repository layout

- `src/layer/layer_entry.cpp`: main layer entrypoints, dispatch, virtualization, decode path.
- `shaders/`: GLSL shader sources used for BCn decode pipelines.
- `cmake/`: dependency checks and build helpers.
- `scripts/`: Android configure and package automation.
- `docs/`: validation and bring-up notes.
- `VkLayer_exynostools.json.in`: layer manifest template.

## Build requirements

- Android NDK (recommended 26.x)
- CMake
- Ninja
- Vulkan SDK with `glslc`
- Java 17 (for Android SDK tooling when needed)

## Build (Android arm64)

```powershell
.\scripts\configure_android_local_repos.ps1
cmake --build .\build-android --config Release -j 8
```

Main outputs:

- `build-android/libVkLayer_ExynosTools.so`
- `build-android/VkLayer_exynostools.json`

## Packaging

Driver package layout:

- `CHANGELOG_V3.0.txt`
- `exynostools_config.ini`
- `meta.json`
- `lib/arm64-v8a/libVkLayer_ExynosTools.so`

For single-file releases, no external shader payload is required.

## Xclipse notes

- Runtime Xclipse detection is enabled.
- Geometry and tessellation capabilities are read from real driver features.
- BCn interception path is activated only for targeted Xclipse runtime conditions.

## Safety and scope

This project is a compatibility layer for research and development workflows on Exynos/Xclipse Vulkan stacks.
Always validate behavior with real app workloads and Vulkan validation layers before publishing a release.
