#pragma once

#include <vulkan/vulkan.h>

void xeno_patch_extensions(VkPhysicalDevice phys,
                           PFN_vkEnumerateDeviceExtensionProperties real_enum_ext,
                           const char* pLayerName,
                           uint32_t* pPropertyCount,
                           VkExtensionProperties* pProperties);

void xeno_patch_features2(VkPhysicalDevice phys,
                          PFN_vkGetPhysicalDeviceFeatures2 real_get_features2,
                          VkPhysicalDeviceFeatures2* pFeatures2);

