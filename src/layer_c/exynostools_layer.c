#include "exynostools_layer.h"

#include <android/log.h>
#include <string.h>
#include <stdlib.h>

#define EXYNOS_LAYER_TAG "ExynosToolsLayerC"
#define EXYNOS_LAYER_LOGI(...) __android_log_print(ANDROID_LOG_INFO, EXYNOS_LAYER_TAG, __VA_ARGS__)
#define EXYNOS_LAYER_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, EXYNOS_LAYER_TAG, __VA_ARGS__)

#define VK_EXPORT __attribute__((visibility("default")))

typedef enum VkLayerFunction_ {
    VK_LAYER_LINK_INFO = 0,
} VkLayerFunction;

typedef struct VkLayerInstanceLink_ {
    struct VkLayerInstanceLink_* pNext;
    PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
    PFN_vkVoidFunction pfnNextGetPhysicalDeviceProcAddr;
} VkLayerInstanceLink;

typedef struct VkLayerInstanceCreateInfo_ {
    VkStructureType sType;
    const void* pNext;
    VkLayerFunction function;
    union {
        VkLayerInstanceLink* pLayerInfo;
        void* pLoaderData;
    } u;
} VkLayerInstanceCreateInfo;

typedef struct VkLayerDeviceLink_ {
    struct VkLayerDeviceLink_* pNext;
    PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pfnNextGetDeviceProcAddr;
} VkLayerDeviceLink;

typedef struct VkLayerDeviceCreateInfo_ {
    VkStructureType sType;
    const void* pNext;
    VkLayerFunction function;
    union {
        VkLayerDeviceLink* pLayerInfo;
        void* pLoaderData;
    } u;
} VkLayerDeviceCreateInfo;

typedef enum VkNegotiateLayerStructType {
    LAYER_NEGOTIATE_INTERFACE_STRUCT = 1,
} VkNegotiateLayerStructType;

typedef struct VkNegotiateLayerInterface {
    VkNegotiateLayerStructType sType;
    void* pNext;
    uint32_t loaderLayerInterfaceVersion;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pfnGetDeviceProcAddr;
    PFN_vkVoidFunction pfnGetPhysicalDeviceProcAddr;
} VkNegotiateLayerInterface;

ExynosLayerGlobalState g_exynos_layer = {0};
static const char* EXYNOS_VIRTUAL_BC_EXTENSION = "VK_EXT_texture_compression_bc";
static int exynos_layer_detect_xclipse(VkPhysicalDevice physical_device);
static const char* exynos_layer_gpu_profile(const VkPhysicalDeviceProperties* props);

static const char* exynos_layer_gpu_profile(const VkPhysicalDeviceProperties* props) {
    if (!props) {
        return "unknown";
    }
    if (props->vendorID != 0x144D) {
        return "non-samsung";
    }
    if (strstr(props->deviceName, "Xclipse") == NULL) {
        return "samsung-non-xclipse";
    }
    if (strstr(props->deviceName, "950") != NULL) {
        return "xclipse-950-rdna3.5-exynos2500";
    }
    if (strstr(props->deviceName, "940") != NULL || strstr(props->deviceName, "930") != NULL) {
        return "xclipse-rdna3-exynos2400-class";
    }
    if (strstr(props->deviceName, "920") != NULL) {
        return "xclipse-920-rdna2-exynos2200";
    }
    return "xclipse-unknown-generation";
}

static int exynos_layer_decode_ready(void) {
    return exynos_bcn_dispatch_can_decode(&g_exynos_layer.bcn_dispatch);
}

static int exynos_layer_should_virtualize_bc(VkPhysicalDevice physical_device) {
    if (!exynos_layer_detect_xclipse(physical_device)) {
        return 0;
    }
    if (!exynos_layer_decode_ready()) {
        return 0;
    }
    return 1;
}

static int exynos_layer_detect_xclipse(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceProperties props;
    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(physical_device, &props);
    const char* profile = exynos_layer_gpu_profile(&props);
    return (strncmp(profile, "xclipse-", 8) == 0) ? 1 : 0;
}

int exynos_layer_is_bcn_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return 1;
        default:
            return 0;
    }
}

ExynosBCFormat exynos_layer_to_bcn_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return EXYNOS_BC1;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
            return EXYNOS_BC2;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return EXYNOS_BC3;
        case VK_FORMAT_BC4_UNORM_BLOCK:
            return EXYNOS_BC4;
        case VK_FORMAT_BC5_UNORM_BLOCK:
            return EXYNOS_BC5;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
            return EXYNOS_BC6H;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return EXYNOS_BC7;
        default:
            return EXYNOS_BC7;
    }
}

VkFormat exynos_layer_replacement_format(VkFormat format) {
    ExynosBCFormat bcn = exynos_layer_to_bcn_format(format);
    return exynos_bc_output_format(bcn);
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                              const VkAllocationCallbacks* pAllocator,
                                                              VkInstance* pInstance) {
    VkLayerInstanceCreateInfo* chain = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO)) {
        chain = (VkLayerInstanceCreateInfo*)chain->pNext;
    }
    if (!chain) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateInstance fp_create = (PFN_vkCreateInstance)next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (!fp_create) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult res = fp_create(pCreateInfo, pAllocator, pInstance);
    if (res != VK_SUCCESS) {
        return res;
    }

    memset(&g_exynos_layer, 0, sizeof(g_exynos_layer));
    g_exynos_layer.instance = *pInstance;
    g_exynos_layer.i_dispatch.GetInstanceProcAddr = next_gipa;
    g_exynos_layer.i_dispatch.DestroyInstance = (PFN_vkDestroyInstance)next_gipa(*pInstance, "vkDestroyInstance");
    g_exynos_layer.i_dispatch.EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)next_gipa(*pInstance, "vkEnumeratePhysicalDevices");
    g_exynos_layer.i_dispatch.EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)next_gipa(*pInstance, "vkEnumerateDeviceExtensionProperties");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)next_gipa(*pInstance, "vkGetPhysicalDeviceFeatures");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)next_gipa(*pInstance, "vkGetPhysicalDeviceFeatures2");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties2 = (PFN_vkGetPhysicalDeviceFormatProperties2)next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties2");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)next_gipa(*pInstance, "vkGetPhysicalDeviceProperties2KHR");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR)next_gipa(*pInstance, "vkGetPhysicalDeviceFeatures2KHR");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties2KHR = (PFN_vkGetPhysicalDeviceFormatProperties2KHR)next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties2KHR");
    g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)next_gipa(*pInstance, "vkGetPhysicalDeviceProperties2");
    pthread_mutex_init(&g_exynos_layer.lock, NULL);
    exynos_imap_init(&g_exynos_layer.image_map, 1024);
    EXYNOS_LAYER_LOGI("Layer instance created");
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    exynos_imap_destroy(&g_exynos_layer.image_map);
    pthread_mutex_destroy(&g_exynos_layer.lock);
    if (g_exynos_layer.i_dispatch.DestroyInstance) {
        g_exynos_layer.i_dispatch.DestroyInstance(instance, pAllocator);
    }
    memset(&g_exynos_layer, 0, sizeof(g_exynos_layer));
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pCount, VkPhysicalDevice* pDevices) {
    VkResult res = g_exynos_layer.i_dispatch.EnumeratePhysicalDevices(instance, pCount, pDevices);
    if (res == VK_SUCCESS && pDevices && pCount && *pCount > 0) {
        g_exynos_layer.physical_device = pDevices[0];
        g_exynos_layer.is_xclipse = exynos_layer_detect_xclipse(pDevices[0]);
    }
    return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                                   const char* pLayerName,
                                                                                   uint32_t* pPropertyCount,
                                                                                   VkExtensionProperties* pProperties) {
    if (!g_exynos_layer.i_dispatch.EnumerateDeviceExtensionProperties || !pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pLayerName != NULL && pLayerName[0] != '\0') {
        return g_exynos_layer.i_dispatch.EnumerateDeviceExtensionProperties(
            physicalDevice, pLayerName, pPropertyCount, pProperties);
    }

    int inject_virtual_bc = exynos_layer_should_virtualize_bc(physicalDevice);

    uint32_t base_count = 0;
    VkResult base_res = g_exynos_layer.i_dispatch.EnumerateDeviceExtensionProperties(
        physicalDevice, NULL, &base_count, NULL);
    if (base_res != VK_SUCCESS && base_res != VK_INCOMPLETE) {
        return base_res;
    }

    uint32_t total = base_count + (inject_virtual_bc ? 1u : 0u);
    if (!pProperties) {
        *pPropertyCount = total;
        return VK_SUCCESS;
    }

    uint32_t requested = *pPropertyCount;
    uint32_t copy_base = (requested < base_count) ? requested : base_count;
    VkResult fill_res = VK_SUCCESS;
    if (copy_base > 0) {
        uint32_t tmp_count = copy_base;
        fill_res = g_exynos_layer.i_dispatch.EnumerateDeviceExtensionProperties(
            physicalDevice, NULL, &tmp_count, pProperties);
        if (fill_res != VK_SUCCESS && fill_res != VK_INCOMPLETE) {
            return fill_res;
        }
        copy_base = tmp_count;
    }

    if (inject_virtual_bc && requested > copy_base) {
        VkExtensionProperties virtual_ext;
        memset(&virtual_ext, 0, sizeof(virtual_ext));
        strncpy(virtual_ext.extensionName, EXYNOS_VIRTUAL_BC_EXTENSION, VK_MAX_EXTENSION_NAME_SIZE - 1);
        virtual_ext.specVersion = 1;
        pProperties[copy_base] = virtual_ext;
        *pPropertyCount = copy_base + 1;
        return (requested < total) ? VK_INCOMPLETE : VK_SUCCESS;
    }

    *pPropertyCount = copy_base;
    return VK_INCOMPLETE;
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                                                      VkPhysicalDeviceFeatures* pFeatures) {
    if (g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures(physicalDevice, pFeatures);
    }
    if (pFeatures && exynos_layer_should_virtualize_bc(physicalDevice)) {
        pFeatures->textureCompressionBC = VK_TRUE;
    }
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                                       VkPhysicalDeviceFeatures2* pFeatures) {
    if (g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    }
    if (pFeatures && exynos_layer_should_virtualize_bc(physicalDevice)) {
        pFeatures->features.textureCompressionBC = VK_TRUE;
    }
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice,
                                                                          VkPhysicalDeviceFeatures2* pFeatures) {
    if (g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2KHR) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2KHR(physicalDevice, pFeatures);
    } else if (g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    }
    if (pFeatures && exynos_layer_should_virtualize_bc(physicalDevice)) {
        pFeatures->features.textureCompressionBC = VK_TRUE;
    }
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                                                              VkFormat format,
                                                                              VkFormatProperties* pProperties) {
    g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties(physicalDevice, format, pProperties);
    if (exynos_layer_should_virtualize_bc(physicalDevice) && exynos_layer_is_bcn_format(format) && pProperties) {
        pProperties->optimalTilingFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                              VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                                                               VkFormat format,
                                                                               VkFormatProperties2* pFormatProperties) {
    if (g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties2) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
    } else if (pFormatProperties) {
        pFormatProperties->sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
        pFormatProperties->pNext = NULL;
        g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties(physicalDevice, format, &pFormatProperties->formatProperties);
    }
    if (pFormatProperties &&
        exynos_layer_should_virtualize_bc(physicalDevice) &&
        exynos_layer_is_bcn_format(format)) {
        pFormatProperties->formatProperties.optimalTilingFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                                     VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                                                     VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice,
                                                                                  VkFormat format,
                                                                                  VkFormatProperties2* pFormatProperties) {
    if (g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties2KHR) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceFormatProperties2KHR(physicalDevice, format, pFormatProperties);
    } else {
        exynos_vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
        return;
    }
    if (pFormatProperties &&
        exynos_layer_should_virtualize_bc(physicalDevice) &&
        exynos_layer_is_bcn_format(format)) {
        pFormatProperties->formatProperties.optimalTilingFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                                     VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                                                     VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                                                         VkPhysicalDeviceProperties2* pProperties) {
    if (g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2(physicalDevice, pProperties);
    }
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice,
                                                                            VkPhysicalDeviceProperties2* pProperties) {
    if (g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2KHR) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2KHR(physicalDevice, pProperties);
    } else if (g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2) {
        g_exynos_layer.i_dispatch.GetPhysicalDeviceProperties2(physicalDevice, pProperties);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkCreateDevice(VkPhysicalDevice physicalDevice,
                                                            const VkDeviceCreateInfo* pCreateInfo,
                                                            const VkAllocationCallbacks* pAllocator,
                                                            VkDevice* pDevice) {
    VkLayerDeviceCreateInfo* chain = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO)) {
        chain = (VkLayerDeviceCreateInfo*)chain->pNext;
    }
    if (!chain) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    PFN_vkCreateDevice fp_create_device = (PFN_vkCreateDevice)next_gipa(VK_NULL_HANDLE, "vkCreateDevice");
    VkResult res = fp_create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS) {
        return res;
    }

    g_exynos_layer.device = *pDevice;
    g_exynos_layer.physical_device = physicalDevice;
    g_exynos_layer.d_dispatch.GetDeviceProcAddr = next_gdpa;
    g_exynos_layer.d_dispatch.DestroyDevice = (PFN_vkDestroyDevice)next_gdpa(*pDevice, "vkDestroyDevice");
    g_exynos_layer.d_dispatch.CreateImage = (PFN_vkCreateImage)next_gdpa(*pDevice, "vkCreateImage");
    g_exynos_layer.d_dispatch.DestroyImage = (PFN_vkDestroyImage)next_gdpa(*pDevice, "vkDestroyImage");
    g_exynos_layer.d_dispatch.GetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)next_gdpa(*pDevice, "vkGetImageMemoryRequirements");
    g_exynos_layer.d_dispatch.BindImageMemory = (PFN_vkBindImageMemory)next_gdpa(*pDevice, "vkBindImageMemory");
    g_exynos_layer.d_dispatch.BindImageMemory2 = (PFN_vkBindImageMemory2)next_gdpa(*pDevice, "vkBindImageMemory2");
    g_exynos_layer.d_dispatch.CreateImageView = (PFN_vkCreateImageView)next_gdpa(*pDevice, "vkCreateImageView");
    g_exynos_layer.d_dispatch.CmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)next_gdpa(*pDevice, "vkCmdCopyBufferToImage");
    g_exynos_layer.d_dispatch.CmdCopyBufferToImage2 = (PFN_vkCmdCopyBufferToImage2)next_gdpa(*pDevice, "vkCmdCopyBufferToImage2");
    g_exynos_layer.d_dispatch.CmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)next_gdpa(*pDevice, "vkCmdPipelineBarrier");
    g_exynos_layer.d_dispatch.AllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)next_gdpa(*pDevice, "vkAllocateCommandBuffers");

    VkPhysicalDeviceProperties props;
    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    EXYNOS_LAYER_LOGI("GPU profile: %s | vendor=0x%04x | device=0x%04x | name=%s",
                      exynos_layer_gpu_profile(&props),
                      props.vendorID,
                      props.deviceID,
                      props.deviceName);

    g_exynos_layer.is_xclipse = exynos_layer_detect_xclipse(physicalDevice);
    if (g_exynos_layer.is_xclipse) {
        exynos_bcn_dispatch_init(&g_exynos_layer.bcn_dispatch, *pDevice, physicalDevice);
        EXYNOS_LAYER_LOGI("Xclipse device detected, BCn decode active");
    } else {
        EXYNOS_LAYER_LOGI("Non-Xclipse device, passthrough mode");
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    exynos_bcn_dispatch_destroy(&g_exynos_layer.bcn_dispatch);
    if (g_exynos_layer.d_dispatch.DestroyDevice) {
        g_exynos_layer.d_dispatch.DestroyDevice(device, pAllocator);
    }
    g_exynos_layer.device = VK_NULL_HANDLE;
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkCreateImage(VkDevice device,
                                                           const VkImageCreateInfo* pCreateInfo,
                                                           const VkAllocationCallbacks* pAllocator,
                                                           VkImage* pImage) {
    VkImageCreateInfo patched = *pCreateInfo;
    BCnImageInfo info;
    memset(&info, 0, sizeof(info));
    info.original_format = pCreateInfo->format;
    info.width = pCreateInfo->extent.width;
    info.height = pCreateInfo->extent.height;
    info.mip_levels = pCreateInfo->mipLevels;

    if (g_exynos_layer.is_xclipse &&
        exynos_layer_decode_ready() &&
        exynos_layer_is_bcn_format(pCreateInfo->format)) {
        patched.format = exynos_layer_replacement_format(pCreateInfo->format);
        patched.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        info.replacement_format = patched.format;
        info.is_bcn = 1;
    } else {
        info.replacement_format = pCreateInfo->format;
        info.is_bcn = 0;
    }

    VkResult res = g_exynos_layer.d_dispatch.CreateImage(device, &patched, pAllocator, pImage);
    if (res == VK_SUCCESS && pImage) {
        exynos_imap_put(&g_exynos_layer.image_map, *pImage, &info);
    }
    return res;
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    exynos_imap_remove(&g_exynos_layer.image_map, image);
    g_exynos_layer.d_dispatch.DestroyImage(device, image, pAllocator);
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkGetImageMemoryRequirements(VkDevice device,
                                                                       VkImage image,
                                                                       VkMemoryRequirements* pRequirements) {
    g_exynos_layer.d_dispatch.GetImageMemoryRequirements(device, image, pRequirements);
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkBindImageMemory(VkDevice device,
                                                               VkImage image,
                                                               VkDeviceMemory memory,
                                                               VkDeviceSize memoryOffset) {
    return g_exynos_layer.d_dispatch.BindImageMemory(device, image, memory, memoryOffset);
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkBindImageMemory2(VkDevice device,
                                                                uint32_t bindInfoCount,
                                                                const VkBindImageMemoryInfo* pBindInfos) {
    if (g_exynos_layer.d_dispatch.BindImageMemory2) {
        return g_exynos_layer.d_dispatch.BindImageMemory2(device, bindInfoCount, pBindInfos);
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkCreateImageView(VkDevice device,
                                                               const VkImageViewCreateInfo* pCreateInfo,
                                                               const VkAllocationCallbacks* pAllocator,
                                                               VkImageView* pView) {
    VkImageViewCreateInfo patched = *pCreateInfo;
    BCnImageInfo info;
    if (exynos_imap_get(&g_exynos_layer.image_map, pCreateInfo->image, &info) && info.is_bcn) {
        patched.format = info.replacement_format;
    }
    return g_exynos_layer.d_dispatch.CreateImageView(device, &patched, pAllocator, pView);
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                                                                 VkBuffer srcBuffer,
                                                                 VkImage dstImage,
                                                                 VkImageLayout dstImageLayout,
                                                                 uint32_t regionCount,
                                                                 const VkBufferImageCopy* pRegions) {
    BCnImageInfo info;
    if (g_exynos_layer.is_xclipse &&
        exynos_imap_get(&g_exynos_layer.image_map, dstImage, &info) &&
        info.is_bcn) {
        ExynosBCFormat fmt = exynos_layer_to_bcn_format(info.original_format);
        VkResult decode_res = exynos_bcn_dispatch_decode(&g_exynos_layer.bcn_dispatch,
                                                         commandBuffer,
                                                         srcBuffer,
                                                         dstImage,
                                                         dstImageLayout,
                                                         pRegions,
                                                         regionCount,
                                                         fmt,
                                                         info.width,
                                                         info.height,
                                                         info.mip_levels);
        if (decode_res == VK_SUCCESS) {
            return;
        }
    }

    g_exynos_layer.d_dispatch.CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                                                                  const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    if (!pCopyBufferToImageInfo) {
        return;
    }
    if (pCopyBufferToImageInfo->regionCount == 0 || !pCopyBufferToImageInfo->pRegions) {
        if (g_exynos_layer.d_dispatch.CmdCopyBufferToImage2) {
            g_exynos_layer.d_dispatch.CmdCopyBufferToImage2(commandBuffer, pCopyBufferToImageInfo);
        }
        return;
    }

    VkBufferImageCopy* regions = (VkBufferImageCopy*)calloc(
        pCopyBufferToImageInfo->regionCount,
        sizeof(VkBufferImageCopy));
    if (!regions) {
        if (g_exynos_layer.d_dispatch.CmdCopyBufferToImage2) {
            g_exynos_layer.d_dispatch.CmdCopyBufferToImage2(commandBuffer, pCopyBufferToImageInfo);
        }
        return;
    }
    for (uint32_t i = 0; i < pCopyBufferToImageInfo->regionCount; ++i) {
        regions[i].bufferOffset = pCopyBufferToImageInfo->pRegions[i].bufferOffset;
        regions[i].bufferRowLength = pCopyBufferToImageInfo->pRegions[i].bufferRowLength;
        regions[i].bufferImageHeight = pCopyBufferToImageInfo->pRegions[i].bufferImageHeight;
        regions[i].imageSubresource = pCopyBufferToImageInfo->pRegions[i].imageSubresource;
        regions[i].imageOffset = pCopyBufferToImageInfo->pRegions[i].imageOffset;
        regions[i].imageExtent = pCopyBufferToImageInfo->pRegions[i].imageExtent;
    }

    exynos_vkCmdCopyBufferToImage(commandBuffer,
                                  pCopyBufferToImageInfo->srcBuffer,
                                  pCopyBufferToImageInfo->dstImage,
                                  pCopyBufferToImageInfo->dstImageLayout,
                                  pCopyBufferToImageInfo->regionCount,
                                  regions);
    free(regions);
}

static VKAPI_ATTR void VKAPI_CALL exynos_vkCmdPipelineBarrier(VkCommandBuffer commandBuffer,
                                                               VkPipelineStageFlags srcStageMask,
                                                               VkPipelineStageFlags dstStageMask,
                                                               VkDependencyFlags dependencyFlags,
                                                               uint32_t memoryBarrierCount,
                                                               const VkMemoryBarrier* pMemoryBarriers,
                                                               uint32_t bufferMemoryBarrierCount,
                                                               const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                                               uint32_t imageMemoryBarrierCount,
                                                               const VkImageMemoryBarrier* pImageMemoryBarriers) {
    g_exynos_layer.d_dispatch.CmdPipelineBarrier(commandBuffer,
                                                 srcStageMask,
                                                 dstStageMask,
                                                 dependencyFlags,
                                                 memoryBarrierCount,
                                                 pMemoryBarriers,
                                                 bufferMemoryBarrierCount,
                                                 pBufferMemoryBarriers,
                                                 imageMemoryBarrierCount,
                                                 pImageMemoryBarriers);
}

static VKAPI_ATTR VkResult VKAPI_CALL exynos_vkAllocateCommandBuffers(VkDevice device,
                                                                      const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                                      VkCommandBuffer* pCommandBuffers) {
    return g_exynos_layer.d_dispatch.AllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL exynos_vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!pName) return NULL;
    if (strcmp(pName, "vkDestroyDevice") == 0) return (PFN_vkVoidFunction)exynos_vkDestroyDevice;
    if (strcmp(pName, "vkCreateImage") == 0) return (PFN_vkVoidFunction)exynos_vkCreateImage;
    if (strcmp(pName, "vkDestroyImage") == 0) return (PFN_vkVoidFunction)exynos_vkDestroyImage;
    if (strcmp(pName, "vkGetImageMemoryRequirements") == 0) return (PFN_vkVoidFunction)exynos_vkGetImageMemoryRequirements;
    if (strcmp(pName, "vkBindImageMemory") == 0) return (PFN_vkVoidFunction)exynos_vkBindImageMemory;
    if (strcmp(pName, "vkBindImageMemory2") == 0) return (PFN_vkVoidFunction)exynos_vkBindImageMemory2;
    if (strcmp(pName, "vkCreateImageView") == 0) return (PFN_vkVoidFunction)exynos_vkCreateImageView;
    if (strcmp(pName, "vkCmdCopyBufferToImage") == 0) return (PFN_vkVoidFunction)exynos_vkCmdCopyBufferToImage;
    if (strcmp(pName, "vkCmdCopyBufferToImage2") == 0) return (PFN_vkVoidFunction)exynos_vkCmdCopyBufferToImage2;
    if (strcmp(pName, "vkCmdPipelineBarrier") == 0) return (PFN_vkVoidFunction)exynos_vkCmdPipelineBarrier;
    if (strcmp(pName, "vkAllocateCommandBuffers") == 0) return (PFN_vkVoidFunction)exynos_vkAllocateCommandBuffers;
    if (g_exynos_layer.d_dispatch.GetDeviceProcAddr) {
        return g_exynos_layer.d_dispatch.GetDeviceProcAddr(device, pName);
    }
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL exynos_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!pName) return NULL;
    if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)exynos_vkCreateInstance;
    if (strcmp(pName, "vkDestroyInstance") == 0) return (PFN_vkVoidFunction)exynos_vkDestroyInstance;
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return (PFN_vkVoidFunction)exynos_vkEnumeratePhysicalDevices;
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return (PFN_vkVoidFunction)exynos_vkEnumerateDeviceExtensionProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceFeatures;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceFeatures2;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2KHR") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceFeatures2KHR;
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceFormatProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties2") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceFormatProperties2;
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties2KHR") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceFormatProperties2KHR;
    if (strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceProperties2;
    if (strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) return (PFN_vkVoidFunction)exynos_vkGetPhysicalDeviceProperties2KHR;
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)exynos_vkCreateDevice;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)exynos_vkGetDeviceProcAddr;
    if (g_exynos_layer.i_dispatch.GetInstanceProcAddr) {
        return g_exynos_layer.i_dispatch.GetInstanceProcAddr(instance, pName);
    }
    return NULL;
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pVersionStruct->loaderLayerInterfaceVersion < 2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetInstanceProcAddr = exynos_vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = exynos_vkGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}

VK_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return exynos_vkGetInstanceProcAddr(instance, pName);
}

VK_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    return exynos_vkGetDeviceProcAddr(device, pName);
}
