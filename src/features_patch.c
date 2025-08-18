#include "logging.h"
#include "features_patch.h"

#include <string.h>

static int ext_present(const char* name, const VkExtensionProperties* props, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(props[i].extensionName, name) == 0) return 1;
    }
    return 0;
}

void xeno_patch_extensions(VkPhysicalDevice phys,
                           PFN_vkEnumerateDeviceExtensionProperties real_enum_ext,
                           const char* pLayerName,
                           uint32_t* pPropertyCount,
                           VkExtensionProperties* pProperties) {
    (void)phys; (void)pLayerName;
    // Query real
    uint32_t count = 0;
    real_enum_ext(phys, NULL, &count, NULL);
    VkExtensionProperties stackprops[128];
    VkExtensionProperties* props = stackprops;
    if (count > 128) props = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * count);
    real_enum_ext(phys, NULL, &count, props);

    // Candidate virtual extensions
    const char* virt_exts[] = {
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
        // Consider advertising dynamic rendering if DXVK needs it and it is safe to emulate
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };
    VkExtensionProperties added[8];
    uint32_t add_count = 0;
    for (size_t i = 0; i < sizeof(virt_exts)/sizeof(virt_exts[0]); ++i) {
        if (!ext_present(virt_exts[i], props, count)) {
            strncpy(added[add_count].extensionName, virt_exts[i], VK_MAX_EXTENSION_NAME_SIZE);
            added[add_count].specVersion = 1;
            add_count++;
        }
    }

    if (!pProperties) {
        *pPropertyCount = count + add_count;
    } else {
        uint32_t cap = *pPropertyCount;
        uint32_t out = 0;
        for (; out < count && out < cap; ++out) pProperties[out] = props[out];
        for (uint32_t i = 0; i < add_count && out < cap; ++i) pProperties[out++] = added[i];
        if (*pPropertyCount < count + add_count) {
            return; // VK_INCOMPLETE by caller
        }
        *pPropertyCount = count + add_count;
    }

    if (props != stackprops) free(props);
}

void xeno_patch_features2(VkPhysicalDevice phys,
                          PFN_vkGetPhysicalDeviceFeatures2 real_get_features2,
                          VkPhysicalDeviceFeatures2* pFeatures2) {
    real_get_features2(phys, pFeatures2);

    VkBaseOutStructure* p = (VkBaseOutStructure*)pFeatures2->pNext;
    while (p) {
        if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES) {
            VkPhysicalDeviceDescriptorIndexingFeatures* f = (VkPhysicalDeviceDescriptorIndexingFeatures*)p;
            // enable conservative subset by default
            f->shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            f->descriptorBindingPartiallyBound = VK_TRUE;
            f->runtimeDescriptorArray = VK_TRUE;
        } else if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT) {
            VkPhysicalDeviceRobustness2FeaturesEXT* f = (VkPhysicalDeviceRobustness2FeaturesEXT*)p;
            f->robustBufferAccess2 = VK_TRUE;
            f->robustImageAccess2 = VK_TRUE;
        } else if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR) {
            VkPhysicalDeviceFloat16Int8FeaturesKHR* f = (VkPhysicalDeviceFloat16Int8FeaturesKHR*)p;
            f->shaderFloat16 = VK_TRUE;
            f->shaderInt8 = VK_TRUE;
        }
        p = p->pNext;
    }
}

