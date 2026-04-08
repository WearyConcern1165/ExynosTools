#pragma once

#include <vulkan/vulkan.h>
#include <pthread.h>

#include "exynostools_hashmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXYNOS_LAYER_NAME "VK_LAYER_EXYNOSTOOLS_bcn_decode"

typedef struct ExynosLayerInstanceDispatch {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2;
    PFN_vkGetPhysicalDeviceFormatProperties2 GetPhysicalDeviceFormatProperties2;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceProperties2KHR GetPhysicalDeviceProperties2KHR;
    PFN_vkGetPhysicalDeviceFeatures2KHR GetPhysicalDeviceFeatures2KHR;
    PFN_vkGetPhysicalDeviceFormatProperties2KHR GetPhysicalDeviceFormatProperties2KHR;
    PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2;
} ExynosLayerInstanceDispatch;

typedef struct ExynosLayerDeviceDispatch {
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkBindImageMemory2 BindImageMemory2;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCmdCopyBufferToImage2 CmdCopyBufferToImage2;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
} ExynosLayerDeviceDispatch;

typedef struct ExynosLayerGlobalState {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    int is_xclipse;
    ExynosLayerInstanceDispatch i_dispatch;
    ExynosLayerDeviceDispatch d_dispatch;
    ExynosImageMap image_map;
    pthread_mutex_t lock;
} ExynosLayerGlobalState;

extern ExynosLayerGlobalState g_exynos_layer;

int exynos_layer_is_bcn_format(VkFormat format);
VkFormat exynos_layer_replacement_format(VkFormat format);

#ifdef __cplusplus
}
#endif
