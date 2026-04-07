# Changelog

All notable changes to ExynosTools will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.0] - 2026-04-07

### Added
- **Granite Engine Compute Shaders**: Replaced all legacy BCn shaders with high-performance Granite engine ports.
- **Native Hardware Passthrough**: Xclipse GPU now handles BC1, BC2, and BC3 formats via its fixed-function hardware texture unit, bypassing the compute overhead.
- **28-byte GranitePushConstants**: Overhauled the C++ dispatcher and Vulkan interceptor to pack push constants efficiently.
- **Adaptive Block Barriers**: Re-written mipmap chain dispatch in `mipmap.c`.
- **Ninja Build System**: Replaced outdated CMake MinGW generators with parallel Ninja compilation for Android NDK.

### Fixed
- **HD Texture Memory Corruption**: Fixed a critical bug in `registers.offset` block indexing where large textures would misalign.

## [2.5.0] - 2026-03-27

### Added
- **D24 to D32 Depth Translation**: Emulates depth formats for Nintendo Switch emulators (Skyline) to fix shadow rendering in games like Zelda and Pokémon.
- **8GB VRAM Budget Spoofer**: Tricks PC games (GTA V, Cyberpunk 2077) into detecting 8GB of VRAM to prevent out-of-memory crashes in Winlator.
- **Garbage Collector**: Deferred image tracked cleanup.
- **Pipeline Cache Persistence**: Saves compiled shader bytecode to disk to prevent micro-stutters on subsequent launches.
- **Watchdog Anti-Crash**: Automatically intercepts `VK_ERROR_DEVICE_LOST` timeouts.

## [1.1.0] - 2026-03-16

### Added
- **Base Compute Dispatch**: Initial BCn compute decoding implementation.
- **Format Support Table**: Dynamic interrogation of Xclipse GPU native formats.
