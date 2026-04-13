# Setup Notes

## 1) Vulkan SDK and shader tools

Install a Vulkan SDK/toolchain that provides:

- Vulkan headers
- `glslc`
- validation layers

For Android, `glslc` can also come from NDK shader tools.

## 2) Dependency bootstrap

Use one strategy and keep it consistent:

- Local `vulkan_repositories.zip` extract (fastest in this workspace), or
- Git submodules, or
- vcpkg in manifest mode.

For the local bundle route, point CMake to:

- `EXYNOS_VULKAN_REPOS_ROOT=<...>/vulkan_repositories_extracted/vulkan_repos`

Note: the current bundle does not include full `Vulkan-Headers`, so Vulkan headers must come from Vulkan SDK or a separate `Vulkan-Headers` checkout.

## 3) Why this architecture

This restart uses a **Vulkan Layer** instead of a custom ICD wrapper to avoid:

- conflicting loader contracts
- duplicated Vulkan bridge implementations
- unstable boot behavior from architecture mixing

## 4) Validation-first workflow

Before game testing:

1. Build layer.
2. Enable validation layers.
3. Run a minimal Vulkan sample.
4. Fix all validation errors before integrating BCn decode work.
