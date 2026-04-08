# Changelog

All notable changes to this project are documented here.

## [3.1.0] - 2026-04-08

### Changed

- Reconnected BCn upload interception to the C++ decoder path.
- Switched inline decode integration to record compute work directly into command buffers.
- Added inline support for BC1, BC2, BC3, BC4, BC5, BC6H, and BC7.
- Expanded descriptor pool sizing and staging ring capacity for more headroom under load.
- Updated source and release packaging for cleaner GitHub and driver distribution layouts.

### Fixed

- Fixed `vkGetDeviceProcAddr` device dispatch handling.
- Fixed transform feedback indirect-byte-count emulation to issue a real draw call.
- Fixed VMA allocation tracking so frees go through `vmaFreeMemory`.
- Fixed staging pool timeout behavior so busy buffers are not recycled on `VK_TIMEOUT`.
- Fixed sRGB handling in the inline BCn decode shaders.
- Fixed the removed C BCn dispatcher references that were breaking builds.

### Removed

- Removed the watchdog runtime path that masked `VK_ERROR_DEVICE_LOST`.
- Removed the SPIR-V patcher runtime path.
- Removed the broken micro-VMA allocator.

## [3.0.0] - 2026-04-07

### Added

- Granite-based compute shader integration for BCn decode.
- Initial V3 packaging and Android ARM64 build output.

## [2.5.0] - 2026-03-27

### Added

- Earlier BCn layer packaging and Vulkan interception groundwork.
