/**
 * ═══════════════════════════════════════════════════════════════════════════
 * ExynosTools v3.0 — ICD Proxy Wrapper
 *
 * Architecture: Identical to v2.0/v2.5 (the versions that work)
 *   - Named libvulkan_adreno.so (for emulator compatibility)
 *   - Loads libvulkan_real.so (Samsung's driver) via dlopen + dladdr
 *   - Exports vk_icd* symbols for ICD negotiation
 *   - Transparent proxy: forwards ALL calls to real driver
 *   - Fallback: hardcoded extensions if real driver fails
 *
 * NEW in v3.0:
 *   - Multi-GPU profile detection (Xclipse 920/940/950)
 *   - RDNA 3.5 (Exynos 2500) extra extensions
 *   - Hardware BCn detection (bypass compute if driver supports it)
 *   - Optimal workgroup sizing per GPU generation
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <string.h>
#include <string>
#include <mutex>
#include <algorithm>
#include <android/log.h>
#include "xclipse_extensions.h"

#define TAG "ExynosTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef VK_EXPORT
#define VK_EXPORT __attribute__((visibility("default")))
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Global state
// ═══════════════════════════════════════════════════════════════════════════

static void*                        g_realDriver = nullptr;
static PFN_vkGetInstanceProcAddr    g_real_vkGIPA = nullptr;
static PFN_vkGetDeviceProcAddr      g_real_vkGDPA = nullptr;
static std::once_flag               g_initFlag;
static bcn::XclipseProfile          g_gpuProfile = bcn::XclipseProfile::Unknown;

// ═══════════════════════════════════════════════════════════════════════════
// Driver discovery — same strategy as v2.0/v2.5 but with self-detection
// ═══════════════════════════════════════════════════════════════════════════

static void loadRealDriver() {
    LOGI("═══════════════════════════════════════════════");
    LOGI("ExynosTools v3.0 — Driver Discovery");
    LOGI("═══════════════════════════════════════════════");

    // Step 1: Find our own directory (where libvulkan_adreno.so lives)
    //         libvulkan_real.so should be in the same directory.
    std::string realDriverPath;

    Dl_info selfInfo;
    if (dladdr((void*)loadRealDriver, &selfInfo) && selfInfo.dli_fname) {
        std::string selfPath(selfInfo.dli_fname);
        size_t lastSlash = selfPath.rfind('/');
        if (lastSlash != std::string::npos) {
            realDriverPath = selfPath.substr(0, lastSlash + 1) + "libvulkan_real.so";
            LOGI("Self path: %s", selfPath.c_str());
            LOGI("Trying co-located driver: %s", realDriverPath.c_str());
        }
    }

    // Step 2: Try loading the co-located driver first
    if (!realDriverPath.empty()) {
        g_realDriver = dlopen(realDriverPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    }

    // Step 3: If co-located fails, try system paths
    if (!g_realDriver) {
        const char* fallbackPaths[] = {
            "/vendor/lib64/hw/vulkan.samsung.so",
            "/vendor/lib/hw/vulkan.samsung.so",
            "/vendor/lib64/egl/libGLES_mali.so",
            "libvulkan.so",
        };
        for (const char* path : fallbackPaths) {
            g_realDriver = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            if (g_realDriver) {
                LOGI("Loaded real driver from: %s", path);
                break;
            }
        }
    }

    // Step 4: Get the proc addr functions
    if (g_realDriver) {
        g_real_vkGIPA = (PFN_vkGetInstanceProcAddr)dlsym(g_realDriver, "vkGetInstanceProcAddr");
        g_real_vkGDPA = (PFN_vkGetDeviceProcAddr)dlsym(g_realDriver, "vkGetDeviceProcAddr");

        if (g_real_vkGIPA) {
            LOGI("✅ Real driver loaded successfully!");
        } else {
            LOGW("⚠️ Driver loaded but vkGetInstanceProcAddr not found");
        }
    } else {
        LOGW("⚠️ Could not load real driver — using hardcoded extensions");
    }
}

static inline void ensureInit() {
    std::call_once(g_initFlag, loadRealDriver);
}

// Forward to real driver
static inline PFN_vkVoidFunction getRealProc(VkInstance inst, const char* name) {
    ensureInit();
    if (g_real_vkGIPA) return g_real_vkGIPA(inst, name);
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// GPU Profile Detection — called after vkCreateInstance
// ═══════════════════════════════════════════════════════════════════════════

static void detectGPU(VkInstance instance) {
    if (g_gpuProfile != bcn::XclipseProfile::Unknown) return;

    auto enumPD = (PFN_vkEnumeratePhysicalDevices)getRealProc(instance, "vkEnumeratePhysicalDevices");
    auto getPDP = (PFN_vkGetPhysicalDeviceProperties)getRealProc(instance, "vkGetPhysicalDeviceProperties");
    if (!enumPD || !getPDP) return;

    uint32_t count = 0;
    enumPD(instance, &count, nullptr);
    if (count == 0) return;

    VkPhysicalDevice pd;
    uint32_t one = 1;
    enumPD(instance, &one, &pd);

    VkPhysicalDeviceProperties props;
    getPDP(pd, &props);

    g_gpuProfile = bcn::detectXclipseProfile(props.vendorID, props.deviceID, props.deviceName);

    LOGI("═══════════════════════════════════════════════");
    LOGI("GPU detected: %s", props.deviceName);
    LOGI("Profile: %s", bcn::profileName(g_gpuProfile));
    LOGI("VendorID: 0x%X  DeviceID: 0x%X", props.vendorID, props.deviceID);
    LOGI("Optimal workgroup: %u threads", bcn::getOptimalWorkgroupSize(g_gpuProfile));
    LOGI("Hardware BCn potential: %s", bcn::hasHardwareBcnPotential(g_gpuProfile) ? "YES" : "NO");
    LOGI("═══════════════════════════════════════════════");
}

// ═══════════════════════════════════════════════════════════════════════════
// Intercepted functions
// ═══════════════════════════════════════════════════════════════════════════

// --- vkCreateInstance: forward + detect GPU ---
static VKAPI_ATTR VkResult VKAPI_CALL
wrap_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                      const VkAllocationCallbacks* pAllocator,
                      VkInstance* pInstance)
{
    LOGI("ExynosTools v3.0 — vkCreateInstance");
    auto real = (PFN_vkCreateInstance)getRealProc(VK_NULL_HANDLE, "vkCreateInstance");
    if (!real) {
        LOGE("❌ No real vkCreateInstance available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult res = real(pCreateInfo, pAllocator, pInstance);
    if (res == VK_SUCCESS && pInstance && *pInstance) {
        detectGPU(*pInstance);
    }
    return res;
}

// --- vkEnumerateInstanceExtensionProperties ---
static VKAPI_ATTR VkResult VKAPI_CALL
wrap_vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    // Try real driver first
    auto real = (PFN_vkEnumerateInstanceExtensionProperties)
        getRealProc(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (real) {
        VkResult res = real(pLayerName, pPropertyCount, pProperties);
        if (res == VK_SUCCESS && pPropertyCount && *pPropertyCount > 0) {
            LOGI("Instance extensions from real driver: %u", *pPropertyCount);
            return res;
        }
    }

    // Fallback to hardcoded
    LOGW("Using hardcoded instance extensions (%u)", bcn::XCLIPSE_INSTANCE_EXT_COUNT);
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pProperties) {
        *pPropertyCount = bcn::XCLIPSE_INSTANCE_EXT_COUNT;
        return VK_SUCCESS;
    }
    uint32_t n = std::min(*pPropertyCount, bcn::XCLIPSE_INSTANCE_EXT_COUNT);
    memcpy(pProperties, bcn::XCLIPSE_INSTANCE_EXTENSIONS, n * sizeof(VkExtensionProperties));
    *pPropertyCount = n;
    return (n < bcn::XCLIPSE_INSTANCE_EXT_COUNT) ? VK_INCOMPLETE : VK_SUCCESS;
}

// --- vkEnumerateDeviceExtensionProperties ---
static VKAPI_ATTR VkResult VKAPI_CALL
wrap_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice pd, const char* pLayerName,
    uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    // Try real driver first
    auto real = (PFN_vkEnumerateDeviceExtensionProperties)
        getRealProc(VK_NULL_HANDLE, "vkEnumerateDeviceExtensionProperties");
    if (real) {
        VkResult res = real(pd, pLayerName, pPropertyCount, pProperties);
        if (res == VK_SUCCESS && pPropertyCount && *pPropertyCount > 0) {
            LOGI("Device extensions from real driver: %u", *pPropertyCount);
            return res;
        }
    }

    // Fallback: choose extension set based on detected GPU profile
    uint32_t totalCount = bcn::XCLIPSE_DEVICE_EXT_BASE_COUNT;
    bool addExtra = (g_gpuProfile == bcn::XclipseProfile::Xclipse950_RDNA35);
    if (addExtra) totalCount += bcn::XCLIPSE_950_EXTRA_COUNT;

    LOGW("Using hardcoded device extensions: %u (profile: %s)",
         totalCount, bcn::profileName(g_gpuProfile));

    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    if (!pProperties) {
        *pPropertyCount = totalCount;
        return VK_SUCCESS;
    }

    // Copy base extensions
    uint32_t copied = 0;
    uint32_t baseCopy = std::min(*pPropertyCount, bcn::XCLIPSE_DEVICE_EXT_BASE_COUNT);
    memcpy(pProperties, bcn::XCLIPSE_DEVICE_EXT_BASE, baseCopy * sizeof(VkExtensionProperties));
    copied += baseCopy;

    // Copy RDNA 3.5 extras if applicable
    if (addExtra && copied < *pPropertyCount) {
        uint32_t extraCopy = std::min(*pPropertyCount - copied, bcn::XCLIPSE_950_EXTRA_COUNT);
        memcpy(pProperties + copied, bcn::XCLIPSE_950_EXTRA_EXTENSIONS,
               extraCopy * sizeof(VkExtensionProperties));
        copied += extraCopy;
    }

    *pPropertyCount = copied;
    return (copied < totalCount) ? VK_INCOMPLETE : VK_SUCCESS;
}

// --- vkEnumerateInstanceLayerProperties ---
static VKAPI_ATTR VkResult VKAPI_CALL
wrap_vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                         VkLayerProperties* pProperties)
{
    auto real = (PFN_vkEnumerateInstanceLayerProperties)
        getRealProc(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties");
    if (real) return real(pPropertyCount, pProperties);

    if (pPropertyCount) *pPropertyCount = 0;
    return VK_SUCCESS;
}

// --- vkEnumerateInstanceVersion ---
static VKAPI_ATTR VkResult VKAPI_CALL
wrap_vkEnumerateInstanceVersion(uint32_t* pApiVersion)
{
    auto real = (PFN_vkEnumerateInstanceVersion)
        getRealProc(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (real) return real(pApiVersion);

    if (pApiVersion) *pApiVersion = VK_MAKE_VERSION(1, 3, 0);
    return VK_SUCCESS;
}

// --- vkDestroyInstance ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    auto real = (PFN_vkDestroyInstance)getRealProc(instance, "vkDestroyInstance");
    if (real) real(instance, pAllocator);
}

// --- vkEnumeratePhysicalDevices ---
static VKAPI_ATTR VkResult VKAPI_CALL
wrap_vkEnumeratePhysicalDevices(VkInstance instance,
                                 uint32_t* pPhysicalDeviceCount,
                                 VkPhysicalDevice* pPhysicalDevices)
{
    auto real = (PFN_vkEnumeratePhysicalDevices)
        getRealProc(instance, "vkEnumeratePhysicalDevices");
    if (real) return real(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if (pPhysicalDeviceCount) *pPhysicalDeviceCount = 0;
    return VK_SUCCESS;
}

// --- vkGetPhysicalDeviceProperties ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceProperties(VkPhysicalDevice pd,
                                    VkPhysicalDeviceProperties* pProperties)
{
    auto real = (PFN_vkGetPhysicalDeviceProperties)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceProperties");
    if (real) {
        real(pd, pProperties);
        // Update profile if not detected yet
        if (pProperties && g_gpuProfile == bcn::XclipseProfile::Unknown) {
            g_gpuProfile = bcn::detectXclipseProfile(
                pProperties->vendorID, pProperties->deviceID, pProperties->deviceName);
            LOGI("GPU profile updated: %s", bcn::profileName(g_gpuProfile));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// BCn Format Virtualization — fused from exynostools_layer.c
// Injects native BCn support flags so DXVK/emulators believe hardware BCn
// is available. No separate Vulkan Layer .so needed.
// ═══════════════════════════════════════════════════════════════════════════

static bool isBcnFormat(VkFormat format) {
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
            return true;
        default:
            return false;
    }
}

static const VkFormatFeatureFlags kBcnInjectFlags =
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
    VK_FORMAT_FEATURE_TRANSFER_DST_BIT  |
    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;

// --- vkGetPhysicalDeviceFormatProperties (inject BCn) ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice pd,
                                          VkFormat format,
                                          VkFormatProperties* pProps)
{
    auto real = (PFN_vkGetPhysicalDeviceFormatProperties)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceFormatProperties");
    if (real) real(pd, format, pProps);
    if (pProps && isBcnFormat(format)) {
        pProps->optimalTilingFeatures |= kBcnInjectFlags;
    }
}

// --- vkGetPhysicalDeviceFormatProperties2 (inject BCn) ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice pd,
                                           VkFormat format,
                                           VkFormatProperties2* pProps)
{
    auto real = (PFN_vkGetPhysicalDeviceFormatProperties2)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceFormatProperties2");
    if (real) {
        real(pd, format, pProps);
    } else {
        auto real1 = (PFN_vkGetPhysicalDeviceFormatProperties)
            getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceFormatProperties");
        if (real1 && pProps) {
            pProps->sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
            real1(pd, format, &pProps->formatProperties);
        }
    }
    if (pProps && isBcnFormat(format)) {
        pProps->formatProperties.optimalTilingFeatures |= kBcnInjectFlags;
    }
}

// --- vkGetPhysicalDeviceFormatProperties2KHR ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice pd,
                                              VkFormat format,
                                              VkFormatProperties2* pProps)
{
    auto real = (PFN_vkGetPhysicalDeviceFormatProperties2KHR)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceFormatProperties2KHR");
    if (real) {
        real(pd, format, pProps);
    } else {
        wrap_vkGetPhysicalDeviceFormatProperties2(pd, format, pProps);
        return;
    }
    if (pProps && isBcnFormat(format)) {
        pProps->formatProperties.optimalTilingFeatures |= kBcnInjectFlags;
    }
}

// --- vkGetPhysicalDeviceFeatures (V2.1: full D3D11 FL 11_0 spoof) ---
static inline void exynos_inject_d3d11_features(VkPhysicalDeviceFeatures* f) {
    if (!f) return;
    f->textureCompressionBC   = VK_TRUE;
    f->geometryShader         = VK_TRUE;
    f->tessellationShader     = VK_TRUE;
    f->logicOp                = VK_TRUE;
    f->dualSrcBlend           = VK_TRUE;
    f->depthClamp             = VK_TRUE;
    f->depthBiasClamp         = VK_TRUE;
    f->fillModeNonSolid       = VK_TRUE;
    f->independentBlend       = VK_TRUE;
    f->multiDrawIndirect      = VK_TRUE;
    f->imageCubeArray         = VK_TRUE;
    f->multiViewport          = VK_TRUE;
    f->samplerAnisotropy      = VK_TRUE;
    f->occlusionQueryPrecise  = VK_TRUE;  // Occlusion queries
    f->fragmentStoresAndAtomics = VK_TRUE; // UAV
    f->shaderStorageImageExtendedFormats = VK_TRUE;
    // Missing additions recommended by technical analysis
    f->drawIndirectFirstInstance      = VK_TRUE;
    f->fullDrawIndexUint32            = VK_TRUE;
    f->shaderClipDistance             = VK_TRUE;
    f->shaderCullDistance             = VK_TRUE;
    f->sampleRateShading              = VK_TRUE;
    f->pipelineStatisticsQuery        = VK_TRUE;
    f->vertexPipelineStoresAndAtomics = VK_TRUE;
}

static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceFeatures(VkPhysicalDevice pd,
                                  VkPhysicalDeviceFeatures* pFeatures)
{
    auto real = (PFN_vkGetPhysicalDeviceFeatures)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceFeatures");
    if (real) real(pd, pFeatures);
    exynos_inject_d3d11_features(pFeatures);
}

// --- vkGetPhysicalDeviceFeatures2 ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd,
                                   VkPhysicalDeviceFeatures2* pFeatures)
{
    auto real = (PFN_vkGetPhysicalDeviceFeatures2)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceFeatures2");
    if (real) real(pd, pFeatures);
    if (pFeatures) exynos_inject_d3d11_features(&pFeatures->features);
}

// --- vkGetPhysicalDeviceFeatures2KHR ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice pd,
                                      VkPhysicalDeviceFeatures2* pFeatures)
{
    auto real = (PFN_vkGetPhysicalDeviceFeatures2KHR)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceFeatures2KHR");
    if (real) {
        real(pd, pFeatures);
    } else {
        wrap_vkGetPhysicalDeviceFeatures2(pd, pFeatures);
        return;
    }
    if (pFeatures) exynos_inject_d3d11_features(&pFeatures->features);
}

// --- vkGetPhysicalDeviceQueueFamilyProperties ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice pd, uint32_t* pCount, VkQueueFamilyProperties* pProps)
{
    auto real = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceQueueFamilyProperties");
    if (real) real(pd, pCount, pProps);
    else if (pCount) *pCount = 0;
}

// --- vkGetPhysicalDeviceMemoryProperties ---
static VKAPI_ATTR void VKAPI_CALL
wrap_vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties* pProps)
{
    auto real = (PFN_vkGetPhysicalDeviceMemoryProperties)
        getRealProc(VK_NULL_HANDLE, "vkGetPhysicalDeviceMemoryProperties");
    if (real) real(pd, pProps);
}

// --- vkCreateDevice ---
static VKAPI_ATTR VkResult VKAPI_CALL
wrap_vkCreateDevice(VkPhysicalDevice pd,
                     const VkDeviceCreateInfo* pCreateInfo,
                     const VkAllocationCallbacks* pAllocator,
                     VkDevice* pDevice)
{
    LOGI("ExynosTools — vkCreateDevice (profile: %s)", bcn::profileName(g_gpuProfile));
    auto real = (PFN_vkCreateDevice)getRealProc(VK_NULL_HANDLE, "vkCreateDevice");
    if (real) return real(pd, pCreateInfo, pAllocator, pDevice);
    return VK_ERROR_INITIALIZATION_FAILED;
}

// ═══════════════════════════════════════════════════════════════════════════
// vkGetInstanceProcAddr — THE core dispatch function
// ═══════════════════════════════════════════════════════════════════════════

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
wrap_vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    ensureInit();

    // Intercepted functions (always return ours)
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetInstanceProcAddr;
    if (strcmp(pName, "vkCreateInstance") == 0)
        return (PFN_vkVoidFunction)wrap_vkCreateInstance;
    if (strcmp(pName, "vkDestroyInstance") == 0)
        return (PFN_vkVoidFunction)wrap_vkDestroyInstance;
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
        return (PFN_vkVoidFunction)wrap_vkEnumerateInstanceExtensionProperties;
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0)
        return (PFN_vkVoidFunction)wrap_vkEnumerateDeviceExtensionProperties;
    if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0)
        return (PFN_vkVoidFunction)wrap_vkEnumerateInstanceLayerProperties;
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0)
        return (PFN_vkVoidFunction)wrap_vkEnumerateInstanceVersion;
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0)
        return (PFN_vkVoidFunction)wrap_vkEnumeratePhysicalDevices;
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceFeatures;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceFeatures2;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2KHR") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceFeatures2KHR;
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceFormatProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties2") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceFormatProperties2;
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties2KHR") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceFormatProperties2KHR;
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceQueueFamilyProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetPhysicalDeviceMemoryProperties;
    if (strcmp(pName, "vkCreateDevice") == 0)
        return (PFN_vkVoidFunction)wrap_vkCreateDevice;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)wrap_vkGetInstanceProcAddr; // redirect

    // Everything else → forward to real driver
    if (g_real_vkGIPA)
        return g_real_vkGIPA(instance, pName);

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// ICD Exports — same as v2.0/v2.5
// ═══════════════════════════════════════════════════════════════════════════

extern "C" {

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion) {
    LOGI("ExynosTools v3.0 — ICD Negotiation (version: %u)", pVersion ? *pVersion : 0);
    if (pVersion && *pVersion > 5) *pVersion = 5;
    return VK_SUCCESS;
}

VK_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return wrap_vkGetInstanceProcAddr(instance, pName);
}

VK_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return wrap_vkGetInstanceProcAddr(instance, pName);
}

VK_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    ensureInit();
    if (g_real_vkGDPA) return g_real_vkGDPA(device, pName);
    if (g_real_vkGIPA) return g_real_vkGIPA(VK_NULL_HANDLE, pName);
    return nullptr;
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char* pLayerName,
    uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    return wrap_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return wrap_vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    return wrap_vkEnumerateInstanceVersion(pApiVersion);
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    return wrap_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(VkInstance instance,
    uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    return wrap_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceProperties(VkPhysicalDevice pd,
    VkPhysicalDeviceProperties* pProperties) {
    wrap_vkGetPhysicalDeviceProperties(pd, pProperties);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFeatures(VkPhysicalDevice pd,
    VkPhysicalDeviceFeatures* pFeatures) {
    wrap_vkGetPhysicalDeviceFeatures(pd, pFeatures);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd,
    VkPhysicalDeviceFeatures2* pFeatures) {
    wrap_vkGetPhysicalDeviceFeatures2(pd, pFeatures);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice pd,
    VkPhysicalDeviceFeatures2* pFeatures) {
    wrap_vkGetPhysicalDeviceFeatures2KHR(pd, pFeatures);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice pd,
    VkFormat format, VkFormatProperties* pProps) {
    wrap_vkGetPhysicalDeviceFormatProperties(pd, format, pProps);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice pd,
    VkFormat format, VkFormatProperties2* pProps) {
    wrap_vkGetPhysicalDeviceFormatProperties2(pd, format, pProps);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice pd,
    VkFormat format, VkFormatProperties2* pProps) {
    wrap_vkGetPhysicalDeviceFormatProperties2KHR(pd, format, pProps);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pd,
    uint32_t* pCount, VkQueueFamilyProperties* pProps) {
    wrap_vkGetPhysicalDeviceQueueFamilyProperties(pd, pCount, pProps);
}

VK_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd,
    VkPhysicalDeviceMemoryProperties* pProps) {
    wrap_vkGetPhysicalDeviceMemoryProperties(pd, pProps);
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice pd,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    return wrap_vkCreateDevice(pd, pCreateInfo, pAllocator, pDevice);
}

VK_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char* pLayerName,
    uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    return wrap_vkEnumerateDeviceExtensionProperties(pd, pLayerName, pPropertyCount, pProperties);
}

} // extern "C"
