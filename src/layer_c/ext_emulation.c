#include "ext_emulation.h"

#include <string.h>
#include <stdlib.h>
#include <android/log.h>

#define EXT_TAG "ExynosToolsExtEmu"
#define EXT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, EXT_TAG, __VA_ARGS__)
#define EXT_LOGW(...) __android_log_print(ANDROID_LOG_WARN, EXT_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// Injected extension definitions
// ═══════════════════════════════════════════════════════════════════════════

static const char* s_injected_names[EXYNOS_INJECTED_EXT_COUNT] = {
    EXYNOS_EXT_TRANSFORM_FEEDBACK_NAME,
    EXYNOS_EXT_CUSTOM_BORDER_COLOR_NAME,
    EXYNOS_EXT_DEPTH_CLIP_ENABLE_NAME,
    EXYNOS_EXT_PROVOKING_VERTEX_NAME
};

static const VkExtensionProperties s_injected_props[EXYNOS_INJECTED_EXT_COUNT] = {
    { EXYNOS_EXT_TRANSFORM_FEEDBACK_NAME,  1 },
    { EXYNOS_EXT_CUSTOM_BORDER_COLOR_NAME, 1 },
    { EXYNOS_EXT_DEPTH_CLIP_ENABLE_NAME,   1 },
    { EXYNOS_EXT_PROVOKING_VERTEX_NAME,    1 }
};

const char** exynos_ext_get_injected_names(void) {
    return (const char**)s_injected_names;
}

const VkExtensionProperties* exynos_ext_get_injected_props(void) {
    return s_injected_props;
}

int exynos_ext_is_injected(const char* ext_name) {
    if (!ext_name) return 0;
    for (int i = 0; i < EXYNOS_INJECTED_EXT_COUNT; i++) {
        if (strcmp(ext_name, s_injected_names[i]) == 0)
            return 1;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Extension injection into enumeration
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_ext_inject_device_extensions(
    VkResult original_result,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties,
    uint32_t native_count,
    const VkExtensionProperties* native_props) {

    // Count how many of our extensions the driver does NOT already have
    int to_inject = 0;
    int should_inject[EXYNOS_INJECTED_EXT_COUNT];
    memset(should_inject, 0, sizeof(should_inject));

    for (int i = 0; i < EXYNOS_INJECTED_EXT_COUNT; i++) {
        int already_has = 0;
        for (uint32_t j = 0; j < native_count; j++) {
            if (strcmp(native_props[j].extensionName, s_injected_names[i]) == 0) {
                already_has = 1;
                break;
            }
        }
        if (!already_has) {
            should_inject[i] = 1;
            to_inject++;
        }
    }

    uint32_t total = native_count + (uint32_t)to_inject;

    if (!pProperties) {
        *pPropertyCount = total;
        return VK_SUCCESS;
    }

    // Copy native extensions first (already done by caller if pProperties != NULL)
    // Then append our injected ones
    uint32_t write_idx = native_count;
    for (int i = 0; i < EXYNOS_INJECTED_EXT_COUNT && write_idx < *pPropertyCount; i++) {
        if (should_inject[i]) {
            memcpy(&pProperties[write_idx], &s_injected_props[i], sizeof(VkExtensionProperties));
            EXT_LOGI("Injected extension: %s", s_injected_names[i]);
            write_idx++;
        }
    }

    *pPropertyCount = write_idx;
    return (write_idx < total) ? VK_INCOMPLETE : VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature struct patching for emulated extensions
// ═══════════════════════════════════════════════════════════════════════════

// Structure types for injected features (from Vulkan spec)
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT   1000028000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT  1000287000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT    1000102000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT     1000254000

typedef struct VkPhysDevTransformFeedbackFeaturesEXT {
    VkStructureType sType;
    void*           pNext;
    VkBool32        transformFeedback;
    VkBool32        geometryStreams;
} VkPhysDevTransformFeedbackFeaturesEXT;

typedef struct VkPhysDevCustomBorderColorFeaturesEXT {
    VkStructureType sType;
    void*           pNext;
    VkBool32        customBorderColors;
    VkBool32        customBorderColorWithoutFormat;
} VkPhysDevCustomBorderColorFeaturesEXT;

typedef struct VkPhysDevDepthClipEnableFeaturesEXT {
    VkStructureType sType;
    void*           pNext;
    VkBool32        depthClipEnable;
} VkPhysDevDepthClipEnableFeaturesEXT;

typedef struct VkPhysDevProvokingVertexFeaturesEXT {
    VkStructureType sType;
    void*           pNext;
    VkBool32        provokingVertexLast;
    VkBool32        transformFeedbackPreservesProvokingVertex;
} VkPhysDevProvokingVertexFeaturesEXT;

void exynos_ext_patch_features2(VkPhysicalDeviceFeatures2* pFeatures) {
    if (!pFeatures) return;

    // Walk the pNext chain and fill in our emulated feature structs
    void* current = (void*)pFeatures->pNext;
    while (current) {
        VkStructureType* sType = (VkStructureType*)current;

        switch (*sType) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
                VkPhysDevTransformFeedbackFeaturesEXT* tf = (VkPhysDevTransformFeedbackFeaturesEXT*)current;
                tf->transformFeedback = VK_TRUE;
                tf->geometryStreams   = VK_FALSE; // We don't emulate geometry streams
                EXT_LOGI("Patched: transformFeedback = TRUE");
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
                VkPhysDevCustomBorderColorFeaturesEXT* cbc = (VkPhysDevCustomBorderColorFeaturesEXT*)current;
                cbc->customBorderColors              = VK_TRUE;
                cbc->customBorderColorWithoutFormat   = VK_TRUE;
                EXT_LOGI("Patched: customBorderColors = TRUE");
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: {
                VkPhysDevDepthClipEnableFeaturesEXT* dce = (VkPhysDevDepthClipEnableFeaturesEXT*)current;
                dce->depthClipEnable = VK_TRUE;
                EXT_LOGI("Patched: depthClipEnable = TRUE");
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: {
                VkPhysDevProvokingVertexFeaturesEXT* pv = (VkPhysDevProvokingVertexFeaturesEXT*)current;
                pv->provokingVertexLast                          = VK_TRUE;
                pv->transformFeedbackPreservesProvokingVertex    = VK_FALSE;
                EXT_LOGI("Patched: provokingVertexLast = TRUE");
                break;
            }
            default:
                break;
        }

        // Walk to next struct in pNext chain
        void** pNextPtr = (void**)((char*)current + sizeof(VkStructureType));
        current = *pNextPtr;
    }
}
