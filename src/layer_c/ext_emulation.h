#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V1.7.0 — DirectX Extension Emulation Layer
// Spoofs and stubs Vulkan extensions that Exynos GPUs lack but DXVK needs.
// ═══════════════════════════════════════════════════════════════════════════

// Extensions we inject into the device extension list
#define EXYNOS_EXT_TRANSFORM_FEEDBACK_NAME   "VK_EXT_transform_feedback"
#define EXYNOS_EXT_CUSTOM_BORDER_COLOR_NAME  "VK_EXT_custom_border_color"
#define EXYNOS_EXT_DEPTH_CLIP_ENABLE_NAME    "VK_EXT_depth_clip_enable"
#define EXYNOS_EXT_PROVOKING_VERTEX_NAME     "VK_EXT_provoking_vertex"

#define EXYNOS_INJECTED_EXT_COUNT 4

/// Get the list of injected extension names.
const char** exynos_ext_get_injected_names(void);

/// Get the list of injected VkExtensionProperties.
const VkExtensionProperties* exynos_ext_get_injected_props(void);

/// Check if an extension name is one of our injected ones.
int exynos_ext_is_injected(const char* ext_name);

/// Inject our extensions into a vkEnumerateDeviceExtensionProperties result.
/// Call this AFTER the real driver has filled in native extensions.
VkResult exynos_ext_inject_device_extensions(
    VkResult original_result,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties,
    uint32_t native_count,
    const VkExtensionProperties* native_props);

/// Patch VkPhysicalDeviceFeatures2 chain to inject feature structs for
/// emulated extensions (transform feedback, custom border color, etc.)
void exynos_ext_patch_features2(VkPhysicalDeviceFeatures2* pFeatures);

#ifdef __cplusplus
}
#endif
