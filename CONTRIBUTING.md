# Contributing to ExynosTools

Welcome! We are excited to have you contribute to ExynosTools, the advanced Exynos Vulkan Driver Wrapper for Winlator and Android Emulators.

## How to Contribute

1. **Fork the Repository**: Start by forking the project to your own GitHub account.
2. **Clone the Project**:
   ```bash
   git clone https://github.com/WearyConcern1165/ExynosTools.git
   cd ExynosTools
   ```
3. **Branch Out**: Create a new branch for your feature or bug fix.
   ```bash
   git checkout -b feature/your-awesome-feature
   ```
4. **Compile and Test**: Make sure everything builds with Android NDK using our Ninja setup.
   ```bash
   build.bat
   ```
5. **Commit and Push**:
   ```bash
   git commit -m "Add some awesome feature"
   git push origin feature/your-awesome-feature
   ```
6. **Submit a Pull Request**: Go to the original repository and open a Pull Request. Describe the changes you made in detail.

## Coding Standards

- **Language Engine**: We use C++17 and C11.
- **Vulkan Layers**: All Vulkan intercepts reside in `vulkan_wrapper.cpp` and `src/layer_c`.
- **Shaders**: Any new shader logic must be added to `shaders/` and written in GLSL with `glslc` SPIR-V targets in mind. Please optimize for RDNA LDS (Local Data Share).

## Bug Reports

If you find a bug, please open an Issue with:
- Device (e.g., S24, S22).
- Emulator (e.g., Winlator 6.0, Skyline).
- A detailed logcat dump: `adb logcat -s ExynosTools`.
