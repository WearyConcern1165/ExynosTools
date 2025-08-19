# ExynosTools v1.3.0 (Stable) - Release Notes

## 🎉 Production-Ready Release

ExynosTools v1.3.0 represents a complete transformation from v1.2.1, addressing all critical feedback from v1.2.0 and implementing a full suite of production-grade features for Vulkan emulation on Xclipse GPUs.

## 🔧 Critical Fixes from v1.2.0 Feedback

### ❌ v1.2.0 Issues (Now Fixed):
- **Empty Binary**: `libxeno_wrapper.so` was 0 bytes (completely non-functional)
- **Wrong Structure**: Used APK format `libs/arm64-v8a/` instead of Winlator's `usr/lib/`
- **Wrong Format**: Packaged as ZIP instead of `tar.gz` required by Winlator Bionic
- **Unnecessary Files**: Included `icd.json` which Android Vulkan loader doesn't use
- **Broken Configuration**: Mixed `.env` and `.conf` formats causing confusion

### ✅ v1.3.0 Solutions:
- **Functional Binary**: Proper compilation with build verification (non-empty guarantee)
- **Correct Structure**: Uses `usr/lib/libxeno_wrapper.so` for Winlator Bionic compatibility
- **Correct Format**: Packages as `exynostools-android-arm64.tar.gz`
- **Clean Package**: Removed unnecessary files, focused on essential components
- **Unified Configuration**: Single `.conf` format for all profiles and settings

## 🚀 New Features & Enhancements

### BCn Texture Decompression (Maximum Priority)
- **Complete Implementation**: Full BC4 and BC5 decompression using embedded SPIR-V shaders
- **Runtime-Free**: No external shader files needed - everything embedded in binary
- **Vulkan Integration**: Proper pipeline creation, command recording, and memory barriers
- **Performance Optimized**: Compute shader-based decompression for GPU acceleration

### HUD System (Medium Priority)
- **Real-time Overlay**: Non-invasive HUD with embedded 8x8 bitmap font
- **Performance Monitoring**: FPS display and GPU metrics
- **Environment Control**: Activate with `EXYNOSTOOLS_HUD=1`
- **Vulkan Integration**: Renders directly into swapchain presentation

### Enhanced GPU Detection & Profiles
- **Real Device IDs**: Updated with actual Xclipse 920, 940, and variant identifiers
- **Unified Configuration**: Single `.conf` format replacing mixed `.env` files
- **Profile Examples**: Pre-configured profiles for DXVK, VKD3D-Proton, and games
- **Performance Modes**: Optimized settings for different use cases

### Build System & Quality
- **Automated Compilation**: CMake integration with shader compilation pipeline
- **Error Handling**: Robust error checking and cleanup throughout codebase
- **Memory Management**: Proper Vulkan object lifecycle management
- **Build Verification**: Ensures binary is functional before packaging

## 📦 Installation (Winlator Bionic 10.1+)

1. Download `exynostools-android-arm64.tar.gz`
2. Extract to `/storage/emulated/0/Android/data/com.winlator/files/drivers/`
3. Winlator automatically detects libraries in `usr/lib/`
4. No manual configuration needed

## 🔧 Configuration

### Environment Variables
```bash
EXYNOSTOOLS_HUD=1                    # Enable HUD overlay
EXYNOSTOOLS_APP_PROFILE=game.conf    # Load custom profile
EXYNOSTOOLS_LOG_FPS=1               # Enable FPS logging
```

### Profile System
```
etc/exynostools/profiles/
├── dxvk.conf          # DXVK optimizations
├── vkd3d.conf         # VKD3D-Proton settings
└── example_game.conf  # Game-specific tweaks
```

## 🎯 Compatibility

- **Target Platform**: Winlator Bionic (Android 10+)
- **GPU Support**: Samsung Xclipse 920, 940, and variants
- **Vulkan Version**: 1.1+ with compute shader support
- **Applications**: DXVK, VKD3D-Proton, native Vulkan games

## 🔍 Technical Details

### Package Structure
```
usr/lib/libxeno_wrapper.so          # Main Vulkan wrapper library
usr/share/meta.json                  # Version and metadata
etc/exynostools/performance_mode.conf
etc/exynostools/profiles/*.conf      # Application profiles
```

### Key Components
- **BCn Emulation**: `bc_emulate.c` with embedded SPIR-V shaders
- **HUD System**: `hud.c` with bitmap font and Vulkan rendering
- **Profile System**: `app_profile.c` with unified `.conf` parsing
- **GPU Detection**: `detect.c` with real Xclipse device IDs
- **Vulkan Wrapper**: `xeno_wrapper.c` with function interception

## 🚦 Known Limitations

- BCn support currently limited to BC4 and BC5 (BC6H/BC7 planned for v1.4.0)
- HUD rendering is simplified (full overlay rendering in future versions)
- Requires Vulkan 1.1+ capable GPU with compute shader support

## 🔮 Future Roadmap

- **v1.4.0**: BC6H and BC7 texture support
- **Enhanced HUD**: Full overlay rendering with customizable elements
- **Performance Profiling**: Advanced GPU performance analysis
- **Automated Testing**: Device compatibility validation

## 📞 Support

For issues, feedback, or contributions, visit the GitHub repository:
https://github.com/WearyConcern1165/ExynosTools

---

**ExynosTools v1.3.0** - Transforming Xclipse GPU emulation from experimental to production-ready.
