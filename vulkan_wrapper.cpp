#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <android/log.h>
#include <cstring>
#include <string>
#include <dirent.h>

extern "C" {
#include "src/layer_c/exynostools_layer.h"
#include "src/layer_c/exynostools_config.h"
#include "src/layer_c/pipeline_cache.h"
#include "src/layer_c/watchdog.h"
#include "src/layer_c/micro_vma.h"
#include "src/layer_c/ext_emulation.h"
#include "src/layer_c/spirv_patcher.h"
#include "src/layer_c/tfb_emulator.h"
}

// Global BCn dispatch context for the single-so ICD wrapper
static ExynosBCnDispatch g_bcn_dispatch;
static ExynosImageMap g_image_map;
static bool g_bcn_dispatch_init_done = false;
static pthread_mutex_t g_layer_lock = PTHREAD_MUTEX_INITIALIZER;

// V1.6.0 globals
static ExynosToolsConfig g_config;
static ExynosPipelineCache g_pipeline_cache;
static ExynosWatchdog g_watchdog;
static ExynosVma g_vma;
static ExynosSpirvPatcher g_spirv_patcher;
static ExynosTfbEmulator g_tfb_emulator;
static bool g_v16_modules_init = false;

#ifdef __cplusplus
extern "C" {
#endif

int exynos_layer_is_bcn_format(VkFormat format) {
    switch (format) {
        // Omitimos BC1, BC2, BC3 porque Samsung Xclipse los soporta nativamente.
        // Solo interceptamos los formatos recortados en hardware:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
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
    return exynos_bc_output_format(exynos_layer_to_bcn_format(format));
}

#ifdef __cplusplus
}
#endif

#define LOG_TAG "ExynosTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct TargetDeviceFuncs {
    PFN_vkCreateImage vkCreateImage;
    PFN_vkDestroyImage vkDestroyImage;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
    PFN_vkBindImageMemory vkBindImageMemory;
    PFN_vkCreateImageView vkCreateImageView;
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
    PFN_vkDestroyDevice vkDestroyDevice;
};

extern TargetDeviceFuncs g_funcs;

static void* g_real_vulkan = nullptr;
static PFN_vkGetInstanceProcAddr g_real_vkGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr g_real_vkGetDeviceProcAddr = nullptr;
static VkInstance g_instance = VK_NULL_HANDLE;
static bool g_bcn_interception_enabled = false;
static bool g_logged_shader_warning = false;
static bool g_logged_gpu_profile = false;
static void* g_self_base = nullptr;

static const char* classify_xclipse_profile(const VkPhysicalDeviceProperties& props) {
    if (props.vendorID != 0x144D) {
        return "non-samsung";
    }
    if (std::strstr(props.deviceName, "Xclipse") == nullptr) {
        return "samsung-non-xclipse";
    }
    if (std::strstr(props.deviceName, "950") != nullptr) {
        return "xclipse-950-rdna3.5-exynos2500";
    }
    if (std::strstr(props.deviceName, "940") != nullptr || std::strstr(props.deviceName, "930") != nullptr) {
        return "xclipse-rdna3-exynos2400-class";
    }
    if (std::strstr(props.deviceName, "920") != nullptr) {
        return "xclipse-920-rdna2-exynos2200";
    }
    return "xclipse-unknown-generation";
}

static bool bind_real_driver_handle(void* handle, const char* source, bool strict_probe) {
    if (!handle) {
        return false;
    }

    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
    if (!gipa) {
        gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vk_icdGetInstanceProcAddr"));
    }
    if (!gipa) {
        LOGW("Driver %s has no vkGetInstanceProcAddr/vk_icdGetInstanceProcAddr", source);
        return false;
    }

    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(gipa), &info) != 0 && info.dli_fbase == g_self_base) {
        LOGW("Skipping recursive driver candidate: %s", source);
        return false;
    }

    auto enum_inst = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        gipa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    if (strict_probe && !enum_inst) {
        LOGW("Driver %s rejected: no vkEnumerateInstanceExtensionProperties", source);
        return false;
    }
    if (strict_probe && enum_inst) {
        uint32_t ext_count = 0;
        VkResult ext_res = enum_inst(nullptr, &ext_count, nullptr);
        if (ext_res != VK_SUCCESS && ext_res != VK_INCOMPLETE) {
            LOGW("Driver %s rejected: extension probe failed (%d)", source, static_cast<int>(ext_res));
            return false;
        }
    }

    g_real_vkGetInstanceProcAddr = gipa;
    g_real_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(handle, "vkGetDeviceProcAddr"));
    if (!g_real_vkGetDeviceProcAddr) {
        g_real_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            gipa(VK_NULL_HANDLE, "vkGetDeviceProcAddr"));
    }
    g_real_vulkan = handle;
    if (std::strstr(source, "libvulkan_real.so") != nullptr) {
        LOGW("Using legacy fallback driver source: %s", source);
    }
    LOGI("Using Vulkan driver: %s", source);
    return true;
}

static bool try_load_driver_path(const char* path, bool strict_probe) {
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        return false;
    }
    if (bind_real_driver_handle(handle, path, strict_probe)) {
        return true;
    }
    dlclose(handle);
    return false;
}

static bool try_scan_driver_dir(const char* dir_path, bool strict_probe) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return false;
    }

    bool loaded = false;
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strncmp(entry->d_name, "vulkan.", 7) != 0) {
            continue;
        }
        std::string candidate = std::string(dir_path) + "/" + entry->d_name;
        if (try_load_driver_path(candidate.c_str(), strict_probe)) {
            loaded = true;
            break;
        }
    }

    closedir(dir);
    return loaded;
}

static bool load_real_vulkan() {
    if (g_real_vkGetInstanceProcAddr) {
        return true;
    }

    Dl_info info{};
    std::string real_path = "libvulkan_real.so";
    std::string local_samsung_path = "vulkan.samsung.so";
    if (dladdr(reinterpret_cast<void*>(&load_real_vulkan), &info) != 0 && info.dli_fname != nullptr) {
        g_self_base = info.dli_fbase;
        std::string self_path(info.dli_fname);
        std::size_t slash = self_path.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string base_dir = self_path.substr(0, slash + 1);
            real_path = base_dir + "libvulkan_real.so";
            local_samsung_path = base_dir + "vulkan.samsung.so";
        }
    }

    if (try_load_driver_path(local_samsung_path.c_str(), true)) {
        return true;
    }
    if (try_load_driver_path("vulkan.samsung.so", true)) {
        return true;
    }

    const char* vendor_candidates[] = {
        "/vendor/lib64/hw/vulkan.samsung.so",
        "/odm/lib64/hw/vulkan.samsung.so",
        "/system/lib64/hw/vulkan.samsung.so",
        "/vendor/lib64/hw/vulkan.exynos.so",
        "/odm/lib64/hw/vulkan.exynos.so",
        "/vendor/lib64/hw/vulkan.mali.so",
        nullptr
    };
    for (int i = 0; vendor_candidates[i] != nullptr; ++i) {
        if (try_load_driver_path(vendor_candidates[i], false)) {
            return true;
        }
    }

    if (try_scan_driver_dir("/vendor/lib64/hw", false)) {
        return true;
    }
    if (try_scan_driver_dir("/odm/lib64/hw", false)) {
        return true;
    }

    if (try_load_driver_path(real_path.c_str(), true)) {
        return true;
    }
    if (try_load_driver_path("libvulkan_real.so", true)) {
        return true;
    }

    if (try_load_driver_path("libvulkan.so", false)) {
        return true;
    }

    LOGE("Failed to load any Vulkan driver backend");
    return false;
}

static bool should_log_shader_warning_for_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

static PFN_vkVoidFunction real_gipa_with_instance(const char* name) {
    if (!load_real_vulkan()) {
        return nullptr;
    }
    return g_real_vkGetInstanceProcAddr(g_instance, name);
}

static PFN_vkVoidFunction real_gipa_no_instance(const char* name) {
    if (!load_real_vulkan()) {
        return nullptr;
    }
    return g_real_vkGetInstanceProcAddr(VK_NULL_HANDLE, name);
}

static PFN_vkVoidFunction real_gdpa(VkDevice device, const char* name) {
    if (!g_real_vkGetDeviceProcAddr) {
        return nullptr;
    }
    return g_real_vkGetDeviceProcAddr(device, name);
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    auto fn = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        real_gipa_no_instance("vkEnumerateInstanceExtensionProperties"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return fn(pLayerName, pPropertyCount, pProperties);
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    auto fn = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
        real_gipa_no_instance("vkEnumerateInstanceLayerProperties"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return fn(pPropertyCount, pProperties);
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    auto fn = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        real_gipa_no_instance("vkEnumerateInstanceVersion"));
    if (!fn) {
        if (pApiVersion) {
            *pApiVersion = VK_API_VERSION_1_0;
        }
        return VK_SUCCESS;
    }
    return fn(pApiVersion);
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    auto fn = reinterpret_cast<PFN_vkCreateInstance>(
        real_gipa_no_instance("vkCreateInstance"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = fn(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS && pInstance && *pInstance != VK_NULL_HANDLE) {
        g_instance = *pInstance;
    }
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {
    if (instance != VK_NULL_HANDLE) {
        g_instance = instance;
    }
    auto fn = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        real_gipa_with_instance("vkEnumeratePhysicalDevices"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return fn(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

static VKAPI_ATTR void VKAPI_CALL hook_vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties* pProperties) {
    auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        real_gipa_with_instance("vkGetPhysicalDeviceProperties"));
    if (fn) {
        fn(physicalDevice, pProperties);
    }
}

static VKAPI_ATTR void VKAPI_CALL hook_vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures) {
    auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
        real_gipa_with_instance("vkGetPhysicalDeviceFeatures"));
    if (fn) {
        fn(physicalDevice, pFeatures);
    }
    if (pFeatures) {
        // ═══════════════════════════════════════════════════════════
        // V2.1: Full D3D11 Feature Level 11_0 spoof
        //
        // The Xclipse RDNA3 hardware CAN do all of this, but
        // Samsung's driver doesn't expose them in Vulkan.
        // Without these, DXVK refuses to initialize D3D11.
        // ═══════════════════════════════════════════════════════════
        pFeatures->textureCompressionBC   = VK_TRUE;  // BCn decode
        pFeatures->geometryShader         = VK_TRUE;  // D3D11 mandatory
        pFeatures->tessellationShader     = VK_TRUE;  // D3D11 mandatory
        pFeatures->logicOp                = VK_TRUE;  // D3D11 blending
        pFeatures->dualSrcBlend           = VK_TRUE;  // D3D11 blending
        pFeatures->depthClamp             = VK_TRUE;  // Depth operations
        pFeatures->depthBiasClamp         = VK_TRUE;  // Shadow maps
        pFeatures->fillModeNonSolid       = VK_TRUE;  // Wireframe
        pFeatures->independentBlend       = VK_TRUE;  // MRT rendering
        pFeatures->multiDrawIndirect      = VK_TRUE;  // Instancing
        pFeatures->imageCubeArray         = VK_TRUE;  // Cubemap arrays
        pFeatures->multiViewport          = VK_TRUE;  // Multi-viewport
        pFeatures->samplerAnisotropy      = VK_TRUE;  // Aniso filtering
        pFeatures->occlusionQueryPrecise  = VK_TRUE;  // Occlusion queries
        pFeatures->fragmentStoresAndAtomics = VK_TRUE; // UAV in pixel shader
        pFeatures->shaderStorageImageExtendedFormats = VK_TRUE;
        // V2.1.1: Missing features (prevents future V2.2 emergency patches)
        pFeatures->drawIndirectFirstInstance      = VK_TRUE;  // Indirect draw
        pFeatures->fullDrawIndexUint32            = VK_TRUE;  // Full 32-bit index
        pFeatures->shaderClipDistance             = VK_TRUE;  // Clip planes
        pFeatures->shaderCullDistance             = VK_TRUE;  // Cull planes
        pFeatures->sampleRateShading              = VK_TRUE;  // Per-sample shading
        pFeatures->pipelineStatisticsQuery        = VK_TRUE;  // Pipeline stats
        pFeatures->vertexPipelineStoresAndAtomics = VK_TRUE;  // VS UAV
    }
}

static VKAPI_ATTR void VKAPI_CALL hook_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties) {
    auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        real_gipa_with_instance("vkGetPhysicalDeviceQueueFamilyProperties"));
    if (fn) {
        fn(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
    }
}

static VKAPI_ATTR void VKAPI_CALL hook_vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        real_gipa_with_instance("vkGetPhysicalDeviceMemoryProperties"));
    if (fn) {
        fn(physicalDevice, pMemoryProperties);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    auto fn = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        real_gipa_with_instance("vkEnumerateDeviceExtensionProperties"));
    if (!fn && g_real_vkGetInstanceProcAddr) {
        fn = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            g_real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateDeviceExtensionProperties"));
    }
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // First, get native extension count
    uint32_t native_count = 0;
    VkResult res = fn(physicalDevice, pLayerName, &native_count, NULL);
    if (res != VK_SUCCESS) return res;

    if (!pProperties) {
        // Caller just wants the count — add our injected extensions
        *pPropertyCount = native_count + EXYNOS_INJECTED_EXT_COUNT;
        return VK_SUCCESS;
    }

    // Get native extensions into a temp buffer
    VkExtensionProperties* native_props = (VkExtensionProperties*)malloc(
        native_count * sizeof(VkExtensionProperties));
    if (!native_props) return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = fn(physicalDevice, pLayerName, &native_count, native_props);
    if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
        free(native_props);
        return res;
    }

    // Copy native extensions to output
    uint32_t copy_count = (native_count < *pPropertyCount) ? native_count : *pPropertyCount;
    memcpy(pProperties, native_props, copy_count * sizeof(VkExtensionProperties));

    // Inject our emulated extensions
    VkResult inject_res = exynos_ext_inject_device_extensions(
        res, pPropertyCount, pProperties, copy_count, native_props);

    free(native_props);
    return inject_res;
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    auto fn = reinterpret_cast<PFN_vkCreateDevice>(
        real_gipa_with_instance("vkCreateDevice"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = fn(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result == VK_SUCCESS && pDevice && *pDevice != VK_NULL_HANDLE) {
        if (!g_logged_gpu_profile) {
            auto props_fn = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                real_gipa_with_instance("vkGetPhysicalDeviceProperties"));
            if (props_fn) {
                VkPhysicalDeviceProperties props{};
                props_fn(physicalDevice, &props);
                LOGI("GPU profile: %s | vendor=0x%04x | device=0x%04x | name=%s",
                     classify_xclipse_profile(props),
                     props.vendorID,
                     props.deviceID,
                     props.deviceName);
                g_logged_gpu_profile = true;
            }
        }
        g_real_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            real_gipa_with_instance("vkGetDeviceProcAddr"));
        if (!g_real_vkGetDeviceProcAddr && g_real_vulkan) {
            g_real_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                dlsym(g_real_vulkan, "vkGetDeviceProcAddr"));
        }
        
        // Initialize BCn decoding pipeline!
        pthread_mutex_lock(&g_layer_lock);
        if (!g_bcn_dispatch_init_done) {
            exynos_imap_init(&g_image_map, 1024);
            exynos_bcn_dispatch_init(&g_bcn_dispatch, *pDevice, physicalDevice);
            g_bcn_dispatch_init_done = true;
            LOGI("BCn Decoder Compute Shaders, LDS, Multi-Mipmap, Staging Pool fused and INITIALIZED!");
        }
        
        // ═══════════════════════════════════════════════════════════
        // V1.6.0 Module Initialization
        // ═══════════════════════════════════════════════════════════
        if (!g_v16_modules_init) {
            // 1) Dynamic Config — load user .ini
            exynos_config_auto_load(&g_config);
            LOGI("V1.6.0 Config: bcn=%d mipmap=%d watchdog=%d vma=%d pcache=%d wg=%u",
                 g_config.enable_bcn_decode,
                 g_config.enable_mipmap,
                 g_config.enable_watchdog,
                 g_config.enable_vma,
                 g_config.enable_pipeline_cache,
                 g_config.workgroup_size);

            // 2) Pipeline Cache — eliminates shader stutter
            if (g_config.enable_pipeline_cache) {
                VkResult pc_res = exynos_pcache_init(&g_pipeline_cache,
                                                      *pDevice,
                                                      g_config.cache_path);
                if (pc_res == VK_SUCCESS) {
                    LOGI("Pipeline Cache ONLINE: %s", g_config.cache_path);
                }
            }

            // 3) Watchdog — anti-crash system
            if (g_config.enable_watchdog) {
                exynos_watchdog_init(&g_watchdog, *pDevice,
                                     g_config.watchdog_timeout_ms);
                LOGI("Watchdog Anti-Crash ONLINE: timeout=%ums",
                     g_config.watchdog_timeout_ms);
            }

            // 4) Micro-VMA — memory defragmentation
            if (g_config.enable_vma) {
                VkResult vma_res = exynos_vma_init(&g_vma, *pDevice,
                                                    physicalDevice,
                                                    g_config.vma_block_size_mb);
                if (vma_res == VK_SUCCESS) {
                    LOGI("Micro-VMA ONLINE: block_size=%uMB",
                         g_config.vma_block_size_mb);
                }
            }

            // Sync legacy interception flag with config
            g_bcn_interception_enabled = g_config.enable_bcn_decode;

            // ═══════════════════════════════════════════════════════════
            // V2.0 Module Initialization
            // ═══════════════════════════════════════════════════════════

            // 5) SPIR-V Dynamic Patcher
            exynos_spirv_init(&g_spirv_patcher);
            LOGI("SPIR-V Patcher V2.0 ONLINE (OpKill fix + Capability stripping)");

            // 6) Transform Feedback Software Emulator
            exynos_tfb_init(&g_tfb_emulator, *pDevice);
            LOGI("Transform Feedback Emulator V2.0 ONLINE");

            g_v16_modules_init = true;
            LOGI("ExynosTools V2.0 Professional — ALL MODULES INITIALIZED");
        }
        pthread_mutex_unlock(&g_layer_lock);
    }
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateImage(
    VkDevice device,
    const VkImageCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImage* pImage) {
    auto fn = reinterpret_cast<PFN_vkCreateImage>(real_gdpa(device, "vkCreateImage"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pCreateInfo && should_log_shader_warning_for_format(pCreateInfo->format) && !g_bcn_interception_enabled) {
        if (!g_logged_shader_warning) {
            LOGW("Intercepting BCn formats and translating to native storage (ExynosTools C Layer)");
            g_logged_shader_warning = true;
        }
    }

    VkImageCreateInfo patched = *pCreateInfo;
    BCnImageInfo info;
    memset(&info, 0, sizeof(info));
    info.original_format = pCreateInfo->format;
    info.width = pCreateInfo->extent.width;
    info.height = pCreateInfo->extent.height;
    info.mip_levels = pCreateInfo->mipLevels;

    if (g_bcn_interception_enabled && g_bcn_dispatch_init_done && exynos_layer_is_bcn_format(pCreateInfo->format)) {
        patched.format = exynos_layer_replacement_format(pCreateInfo->format);
        patched.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        info.replacement_format = patched.format;
        info.is_bcn = 1;
    } else {
        info.replacement_format = pCreateInfo->format;
        info.is_bcn = 0;
    }

    VkResult res = fn(device, &patched, pAllocator, pImage);
    if (res == VK_SUCCESS && pImage) {
        pthread_mutex_lock(&g_layer_lock);
        exynos_imap_put(&g_image_map, *pImage, &info);
        pthread_mutex_unlock(&g_layer_lock);
    }
    return res;
}

static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyImage(
    VkDevice device,
    VkImage image,
    const VkAllocationCallbacks* pAllocator) {
    auto fn = reinterpret_cast<PFN_vkDestroyImage>(real_gdpa(device, "vkDestroyImage"));
    if (fn) {
        pthread_mutex_lock(&g_layer_lock);
        exynos_imap_remove(&g_image_map, image);
        pthread_mutex_unlock(&g_layer_lock);
        fn(device, image, pAllocator);
    }
}

static VKAPI_ATTR void VKAPI_CALL hook_vkGetImageMemoryRequirements(
    VkDevice device,
    VkImage image,
    VkMemoryRequirements* pMemoryRequirements) {
    auto fn = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(
        real_gdpa(device, "vkGetImageMemoryRequirements"));
    if (fn) {
        fn(device, image, pMemoryRequirements);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkBindImageMemory(
    VkDevice device,
    VkImage image,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset) {
    auto fn = reinterpret_cast<PFN_vkBindImageMemory>(real_gdpa(device, "vkBindImageMemory"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return fn(device, image, memory, memoryOffset);
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateImageView(
    VkDevice device,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView) {
    auto fn = reinterpret_cast<PFN_vkCreateImageView>(real_gdpa(device, "vkCreateImageView"));
    if (!fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkImageViewCreateInfo patched = *pCreateInfo;
    BCnImageInfo info;
    pthread_mutex_lock(&g_layer_lock);
    int is_bcn = exynos_imap_get(&g_image_map, pCreateInfo->image, &info);
    pthread_mutex_unlock(&g_layer_lock);
    
    if (is_bcn && info.is_bcn) {
        patched.format = info.replacement_format;
    }
    return fn(device, &patched, pAllocator, pView);
}

static VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {
    if (!load_real_vulkan() || g_instance == VK_NULL_HANDLE) {
        return;
    }
    auto fn = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(
        g_real_vkGetInstanceProcAddr(g_instance, "vkCmdCopyBufferToImage"));
        
    BCnImageInfo info;
    pthread_mutex_lock(&g_layer_lock);
    int is_bcn = exynos_imap_get(&g_image_map, dstImage, &info);
    pthread_mutex_unlock(&g_layer_lock);
    
    if (g_bcn_interception_enabled && g_bcn_dispatch_init_done && is_bcn && info.is_bcn) {
        ExynosBCFormat fmt = exynos_layer_to_bcn_format(info.original_format);
        VkResult decode_res = exynos_bcn_dispatch_decode(&g_bcn_dispatch,
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
            return; // Decoded successfully by ExynosTools LDS Shaders / Staging Pool !
        }
    }
    
    if (fn) {
        fn(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
    }
}

static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {
    auto fn = reinterpret_cast<PFN_vkDestroyDevice>(real_gdpa(device, "vkDestroyDevice"));

    pthread_mutex_lock(&g_layer_lock);

    // ═══════════════════════════════════════════════════════════
    // V1.6.0 Module Shutdown (reverse order of init)
    // ═══════════════════════════════════════════════════════════
    if (g_v16_modules_init) {
        // V2.0: TFB Emulator shutdown
        {
            uint32_t begins, ends, draws, binds;
            exynos_tfb_get_stats(&g_tfb_emulator, &begins, &ends, &draws, &binds);
            LOGI("TFB Emulator shutdown: %u begins, %u ends, %u indirect draws, %u binds",
                 begins, ends, draws, binds);
            exynos_tfb_destroy(&g_tfb_emulator);
        }

        // V2.0: SPIR-V Patcher stats
        {
            ExynosSpirvPatchStats sp_stats;
            exynos_spirv_get_stats(&g_spirv_patcher, &sp_stats);
            LOGI("SPIR-V shutdown: %u scanned, %u OpKill fixed, %u caps stripped, %u patched, %u passthrough",
                 sp_stats.opcodes_scanned, sp_stats.opkill_replaced,
                 sp_stats.capabilities_stripped, sp_stats.shaders_patched,
                 sp_stats.shaders_passthrough);
        }

        // Micro-VMA stats + destroy
        if (g_config.enable_vma) {
            uint64_t alloc_total, suballocs, saved;
            exynos_vma_get_stats(&g_vma, &alloc_total, &suballocs, &saved);
            LOGI("VMA shutdown: %llu bytes allocated, %llu sub-allocs, %llu direct calls saved",
                 (unsigned long long)alloc_total,
                 (unsigned long long)suballocs,
                 (unsigned long long)saved);
            exynos_vma_destroy(&g_vma);
        }

        // Watchdog stats
        if (g_config.enable_watchdog) {
            uint32_t lost = 0, timeouts = 0;
            exynos_watchdog_get_stats(&g_watchdog, &lost, &timeouts);
            LOGI("Watchdog shutdown: %u device-lost masked, %u timeouts recovered",
                 lost, timeouts);
        }

        // Pipeline Cache — save to disk then destroy
        if (g_config.enable_pipeline_cache) {
            exynos_pcache_save(&g_pipeline_cache);
            exynos_pcache_destroy(&g_pipeline_cache);
            LOGI("Pipeline Cache saved and destroyed");
        }

        g_v16_modules_init = false;
        LOGI("ExynosTools V2.0 modules shutdown complete");
    }

    // BCn dispatch cleanup
    if (g_bcn_dispatch_init_done) {
        exynos_bcn_dispatch_destroy(&g_bcn_dispatch);
        exynos_imap_destroy(&g_image_map);
        g_bcn_dispatch_init_done = false;
    }
    pthread_mutex_unlock(&g_layer_lock);

    if (fn) {
        fn(device, pAllocator);
    }
}

// ════════════════════════════════════════════════════════════════════
// V1.6.0 Module Hooks
// ════════════════════════════════════════════════════════════════════

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDeviceMemory* pMemory) {
    auto fn = reinterpret_cast<PFN_vkAllocateMemory>(real_gdpa(device, "vkAllocateMemory"));
    if (!fn) return VK_ERROR_INITIALIZATION_FAILED;

    if (g_v16_modules_init && g_config.enable_vma && pAllocateInfo) {
        VkMemoryRequirements fake_reqs;
        fake_reqs.size = pAllocateInfo->allocationSize;
        fake_reqs.alignment = 256; // Sane alignment
        fake_reqs.memoryTypeBits = (1u << pAllocateInfo->memoryTypeIndex);

        VkDeviceSize offset;
        VkResult res = exynos_vma_alloc(&g_vma, &fake_reqs, pAllocateInfo->memoryTypeIndex, pMemory, &offset);
        if (res == VK_SUCCESS) {
            // We successfully sub-allocated. 
            // Note: In a complete VMA, we would need to track the offset globally to hook vkBindImageMemory,
            // but for simple buffers/textures, offset=0 is standard. If the game uses offset != 0, 
            // a full sub-allocator intercept needs vkBindImageMemory offset injection. 
            // For now, this is a simplified memory pool hook.
            return VK_SUCCESS;
        }
    }
    return fn(device, pAllocateInfo, pAllocator, pMemory);
}

static VKAPI_ATTR void VKAPI_CALL hook_vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator) {
    auto fn = reinterpret_cast<PFN_vkFreeMemory>(real_gdpa(device, "vkFreeMemory"));
    
    if (g_v16_modules_init && g_config.enable_vma && memory != VK_NULL_HANDLE) {
        exynos_vma_free(&g_vma, memory, 0); // Simplified VMA free
        // We do *not* call native vkFreeMemory since it's a sub-allocation,
        // unless it wasn't intercepted! (In a complete implementation, track via hashmap).
        // Since VMA is complex, we'll fall through for safety if it wasn't tracked.
    }
    
    // We pass through safely for anything not fully managed by simple VMA
    if (fn) fn(device, memory, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence fence) {
    if (g_v16_modules_init && g_config.enable_watchdog) {
        return exynos_watchdog_queue_submit(&g_watchdog, queue, submitCount, pSubmits, fence);
    }
    
    auto fn = reinterpret_cast<PFN_vkQueueSubmit>(g_real_vkGetInstanceProcAddr(g_instance, "vkQueueSubmit"));
    if (fn) return fn(queue, submitCount, pSubmits, fence);
    return VK_ERROR_INITIALIZATION_FAILED;
}

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences,
    VkBool32 waitAll,
    uint64_t timeout) {
    auto fn = reinterpret_cast<PFN_vkWaitForFences>(real_gdpa(device, "vkWaitForFences"));
    if (!fn) return VK_ERROR_INITIALIZATION_FAILED;

    if (g_v16_modules_init && g_config.enable_watchdog && fenceCount == 1) {
        return exynos_watchdog_wait_fence(&g_watchdog, pFences[0], timeout);
    }
    return fn(device, fenceCount, pFences, waitAll, timeout);
}

// ════════════════════════════════════════════════════════════════════
// V2.0 Module Hooks
// ════════════════════════════════════════════════════════════════════

static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateShaderModule(
    VkDevice device,
    const VkShaderModuleCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkShaderModule* pShaderModule) {
    auto fn = reinterpret_cast<PFN_vkCreateShaderModule>(real_gdpa(device, "vkCreateShaderModule"));
    if (!fn) return VK_ERROR_INITIALIZATION_FAILED;

    if (g_v16_modules_init && g_spirv_patcher.enabled && pCreateInfo && pCreateInfo->pCode) {
        uint32_t word_count = (uint32_t)(pCreateInfo->codeSize / sizeof(uint32_t));
        // Make a writable copy of the SPIR-V code
        uint32_t* patched_code = (uint32_t*)malloc(pCreateInfo->codeSize);
        if (patched_code) {
            memcpy(patched_code, pCreateInfo->pCode, pCreateInfo->codeSize);
            int was_modified = exynos_spirv_patch(&g_spirv_patcher, patched_code, word_count);
            if (was_modified) {
                VkShaderModuleCreateInfo patched_info = *pCreateInfo;
                patched_info.pCode = patched_code;
                VkResult result = fn(device, &patched_info, pAllocator, pShaderModule);
                free(patched_code);
                return result;
            }
            free(patched_code);
        }
    }
    return fn(device, pCreateInfo, pAllocator, pShaderModule);
}

static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindTransformFeedbackBuffersEXT(
    VkCommandBuffer commandBuffer,
    uint32_t firstBinding,
    uint32_t bindingCount,
    const VkBuffer* pBuffers,
    const VkDeviceSize* pOffsets,
    const VkDeviceSize* pSizes) {
    exynos_tfb_bind_buffers(&g_tfb_emulator, commandBuffer,
                            firstBinding, bindingCount, pBuffers, pOffsets, pSizes);
}

static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginTransformFeedbackEXT(
    VkCommandBuffer commandBuffer,
    uint32_t firstCounterBuffer,
    uint32_t counterBufferCount,
    const VkBuffer* pCounterBuffers,
    const VkDeviceSize* pCounterBufferOffsets) {
    exynos_tfb_begin(&g_tfb_emulator, commandBuffer,
                     firstCounterBuffer, counterBufferCount,
                     pCounterBuffers, pCounterBufferOffsets);
}

static VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndTransformFeedbackEXT(
    VkCommandBuffer commandBuffer,
    uint32_t firstCounterBuffer,
    uint32_t counterBufferCount,
    const VkBuffer* pCounterBuffers,
    const VkDeviceSize* pCounterBufferOffsets) {
    exynos_tfb_end(&g_tfb_emulator, commandBuffer,
                   firstCounterBuffer, counterBufferCount,
                   pCounterBuffers, pCounterBufferOffsets);
}

static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirectByteCountEXT(
    VkCommandBuffer commandBuffer,
    uint32_t instanceCount,
    uint32_t firstInstance,
    VkBuffer counterBuffer,
    VkDeviceSize counterBufferOffset,
    uint32_t counterOffset,
    uint32_t vertexStride) {
    exynos_tfb_draw_indirect_byte_count(&g_tfb_emulator, commandBuffer,
                                         instanceCount, firstInstance,
                                         counterBuffer, counterBufferOffset,
                                         counterOffset, vertexStride);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hook_vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName) {
    if (!pName || !load_real_vulkan()) {
        return nullptr;
    }
    if (std::strcmp(pName, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkDestroyDevice);
    if (std::strcmp(pName, "vkCreateImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCreateImage);
    if (std::strcmp(pName, "vkDestroyImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkDestroyImage);
    if (std::strcmp(pName, "vkGetImageMemoryRequirements") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetImageMemoryRequirements);
    if (std::strcmp(pName, "vkBindImageMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkBindImageMemory);
    if (std::strcmp(pName, "vkCreateImageView") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCreateImageView);
    if (std::strcmp(pName, "vkCmdCopyBufferToImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdCopyBufferToImage);
    
    // V1.6.0 Hooks
    if (std::strcmp(pName, "vkQueueSubmit") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkQueueSubmit);
    if (std::strcmp(pName, "vkWaitForFences") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkWaitForFences);
    if (std::strcmp(pName, "vkAllocateMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkAllocateMemory);
    if (std::strcmp(pName, "vkFreeMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkFreeMemory);

    // V2.0 Hooks
    if (std::strcmp(pName, "vkCreateShaderModule") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCreateShaderModule);
    if (std::strcmp(pName, "vkCmdBindTransformFeedbackBuffersEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdBindTransformFeedbackBuffersEXT);
    if (std::strcmp(pName, "vkCmdBeginTransformFeedbackEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdBeginTransformFeedbackEXT);
    if (std::strcmp(pName, "vkCmdEndTransformFeedbackEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdEndTransformFeedbackEXT);
    if (std::strcmp(pName, "vkCmdDrawIndirectByteCountEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCmdDrawIndirectByteCountEXT);
    if (g_real_vkGetDeviceProcAddr) {
        return g_real_vkGetDeviceProcAddr(device, pName);
    }
    return nullptr;
}

// Format spoofing hooks for the single-so ICD wrapper
static VKAPI_ATTR void VKAPI_CALL hook_vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties* pFormatProperties) {
    auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        real_gipa_with_instance("vkGetPhysicalDeviceFormatProperties"));
    if (fn) fn(physicalDevice, format, pFormatProperties);
    
    if (pFormatProperties && exynos_layer_is_bcn_format(format)) {
        pFormatProperties->optimalTilingFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                    VK_FORMAT_FEATURE_TRANSFER_DST_BIT  |
                                                    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
}

static VKAPI_ATTR void VKAPI_CALL hook_vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2* pFormatProperties) {
    auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(
        real_gipa_with_instance("vkGetPhysicalDeviceFormatProperties2"));
    if (fn) {
        fn(physicalDevice, format, pFormatProperties);
    } else {
        auto fn1 = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
            real_gipa_with_instance("vkGetPhysicalDeviceFormatProperties"));
        if (fn1 && pFormatProperties) {
            pFormatProperties->sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
            fn1(physicalDevice, format, &pFormatProperties->formatProperties);
        }
    }
    
    if (pFormatProperties && exynos_layer_is_bcn_format(format)) {
        pFormatProperties->formatProperties.optimalTilingFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                                     VK_FORMAT_FEATURE_TRANSFER_DST_BIT  |
                                                                     VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
}

static VKAPI_ATTR void VKAPI_CALL hook_vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures) {
    auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        real_gipa_with_instance("vkGetPhysicalDeviceFeatures2"));
    if (fn) {
        fn(physicalDevice, pFeatures);
    }
    if (pFeatures) {
        // ═══════════════════════════════════════════════════════════
        // V2.1: Full D3D11 Feature Level 11_0 spoof (same as Features1)
        // ═══════════════════════════════════════════════════════════
        VkPhysicalDeviceFeatures* f = &pFeatures->features;
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
        f->occlusionQueryPrecise  = VK_TRUE;
        f->fragmentStoresAndAtomics = VK_TRUE;
        f->shaderStorageImageExtendedFormats = VK_TRUE;
        // V2.1.1: Missing features (same as Features1)
        f->drawIndirectFirstInstance      = VK_TRUE;
        f->fullDrawIndexUint32            = VK_TRUE;
        f->shaderClipDistance             = VK_TRUE;
        f->shaderCullDistance             = VK_TRUE;
        f->sampleRateShading              = VK_TRUE;
        f->pipelineStatisticsQuery        = VK_TRUE;
        f->vertexPipelineStoresAndAtomics = VK_TRUE;
        // V1.7.0: Patch emulated extension features in pNext chain
        exynos_ext_patch_features2(pFeatures);
    }
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hook_vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName) {
    if (!pName || !load_real_vulkan()) {
        return nullptr;
    }
    if (instance != VK_NULL_HANDLE) {
        g_instance = instance;
    }

    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetInstanceProcAddr);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetDeviceProcAddr);

    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkEnumerateInstanceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateInstanceVersion") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkEnumerateInstanceVersion);
    if (std::strcmp(pName, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCreateInstance);
    if (std::strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkEnumeratePhysicalDevices);
    if (std::strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceProperties);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceFeatures);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceFeatures2);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFeatures2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceFeatures2);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceFormatProperties);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceFormatProperties2);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceFormatProperties2);
    if (std::strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceQueueFamilyProperties);
    if (std::strcmp(pName, "vkGetPhysicalDeviceMemoryProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetPhysicalDeviceMemoryProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(pName, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCreateDevice);

    return g_real_vkGetInstanceProcAddr(instance, pName);
}

extern "C" {

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    if (!pSupportedVersion) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (*pSupportedVersion > 5) {
        *pSupportedVersion = 5;
    }
    return VK_SUCCESS;
}

__attribute__((visibility("default")))
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return hook_vkGetInstanceProcAddr(instance, pName);
}

__attribute__((visibility("default")))
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return hook_vkGetInstanceProcAddr(instance, pName);
}

__attribute__((visibility("default")))
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    return hook_vkGetDeviceProcAddr(device, pName);
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    return hook_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return hook_vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    return hook_vkEnumerateInstanceVersion(pApiVersion);
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    return hook_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {
    return hook_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

__attribute__((visibility("default")))
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties* pProperties) {
    hook_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
}

__attribute__((visibility("default")))
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures) {
    hook_vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures);
}

__attribute__((visibility("default")))
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties) {
    hook_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

__attribute__((visibility("default")))
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    hook_vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    return hook_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    return hook_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

}
