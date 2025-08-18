#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "logging.h"
#include "detect.h"
#include "features_patch.h"
#include "bc_emulate.h"
#include "perf_conf.h"
#include "app_profile.h"

// Lazy loader state
static pthread_once_t g_loader_once = PTHREAD_ONCE_INIT;
static void* g_loader_handle = NULL;

// Pointers to real functions
static PFN_vkGetInstanceProcAddr real_vkGetInstanceProcAddr = NULL;
static PFN_vkGetDeviceProcAddr real_vkGetDeviceProcAddr = NULL;
static PFN_vkCreateInstance real_vkCreateInstance = NULL;
static PFN_vkEnumeratePhysicalDevices real_vkEnumeratePhysicalDevices = NULL;
static PFN_vkGetPhysicalDeviceProperties real_vkGetPhysicalDeviceProperties = NULL;
static PFN_vkGetPhysicalDeviceFeatures2 real_vkGetPhysicalDeviceFeatures2 = NULL;
static PFN_vkEnumerateDeviceExtensionProperties real_vkEnumerateDeviceExtensionProperties = NULL;
static PFN_vkCreateDevice real_vkCreateDevice = NULL;
static PFN_vkCreateSwapchainKHR real_vkCreateSwapchainKHR = NULL;
static PFN_vkQueuePresentKHR real_vkQueuePresentKHR = NULL;

// Simple FPS counter
static int g_log_fps = 0;
static double g_last_fps_time = 0.0;
static int g_frames = 0;
static XenoPerfConf g_perf_conf;

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Forward declarations of our interceptor functions so we can return their addresses
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures2);
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

static const char* candidate_loader_names[] = {
    "libvulkan.so.1",
    "libvulkan.so",
};

static void resolve_loader_once(void) {
    const char* override = getenv("XCLIPSE_WRAPPER_LOADER");
    if (override && *override) {
        g_loader_handle = dlopen(override, RTLD_NOW | RTLD_LOCAL);
        if (!g_loader_handle) XENO_LOGE("dlopen override failed: %s", dlerror());
    }
    for (size_t i = 0; !g_loader_handle && i < sizeof(candidate_loader_names)/sizeof(candidate_loader_names[0]); ++i) {
        g_loader_handle = dlopen(candidate_loader_names[i], RTLD_NOW | RTLD_LOCAL);
    }
    if (!g_loader_handle) {
        XENO_LOGE("Failed to locate Vulkan loader (libvulkan.so.*)");
        return;
    }
    real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(g_loader_handle, "vkGetInstanceProcAddr");
    real_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)dlsym(g_loader_handle, "vkGetDeviceProcAddr");
    if (!real_vkGetInstanceProcAddr || !real_vkGetDeviceProcAddr) {
        XENO_LOGE("Failed to resolve core loader symbols");
        return;
    }
}

static void ensure_loader(void) { pthread_once(&g_loader_once, resolve_loader_once); }

// Helper to fetch device-level functions after device creation
static void resolve_device_entrypoints(VkDevice device) {
    real_vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)real_vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
    real_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)real_vkGetDeviceProcAddr(device, "vkQueuePresentKHR");
}

// Interceptors
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    ensure_loader();
    if (!real_vkGetInstanceProcAddr) return NULL;
    if (!pName) return NULL;

    // Intercept selected symbols
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)&vkGetDeviceProcAddr;
    if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)&vkCreateInstance;
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return (PFN_vkVoidFunction)&vkEnumeratePhysicalDevices;
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) return (PFN_vkVoidFunction)&vkGetPhysicalDeviceProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0) return (PFN_vkVoidFunction)&vkGetPhysicalDeviceFeatures2;
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return (PFN_vkVoidFunction)&vkEnumerateDeviceExtensionProperties;
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)&vkCreateDevice;

    return real_vkGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    ensure_loader();
    if (!real_vkGetDeviceProcAddr) return NULL;
    if (!pName) return NULL;

    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) return (PFN_vkVoidFunction)&vkCreateSwapchainKHR;
    if (strcmp(pName, "vkQueuePresentKHR") == 0) return (PFN_vkVoidFunction)&vkQueuePresentKHR;

    return real_vkGetDeviceProcAddr(device, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    ensure_loader();
    if (!real_vkCreateInstance) real_vkCreateInstance = (PFN_vkCreateInstance)real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (!real_vkCreateInstance) return VK_ERROR_INITIALIZATION_FAILED;
    g_log_fps = getenv("EXYNOSTOOLS_LOG_FPS") && *getenv("EXYNOSTOOLS_LOG_FPS") == '1';
    g_last_fps_time = now_sec();
    g_frames = 0;
    xeno_perf_conf_load("/etc/exynostools/performance_mode.conf", &g_perf_conf);
    XENO_LOGI("ExynosTools wrapper initialized. sync_mode=%d validation=%d", g_perf_conf.sync_mode, g_perf_conf.validation);
    xeno_app_profile_apply();
    return real_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    if (!real_vkEnumeratePhysicalDevices) real_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)real_vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    return real_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    if (!real_vkGetPhysicalDeviceProperties) real_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetPhysicalDeviceProperties");
    real_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures2) {
    if (!real_vkGetPhysicalDeviceFeatures2) real_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetPhysicalDeviceFeatures2");
    XenoDetectConfig dcfg; xeno_detect_parse_env(&dcfg);
    if (xeno_is_xclipse_gpu(physicalDevice, &dcfg)) {
        xeno_patch_features2(physicalDevice, real_vkGetPhysicalDeviceFeatures2, pFeatures2);
    } else {
        real_vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures2);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    if (!real_vkEnumerateDeviceExtensionProperties) real_vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateDeviceExtensionProperties");
    XenoDetectConfig dcfg; xeno_detect_parse_env(&dcfg);
    if (xeno_is_xclipse_gpu(physicalDevice, &dcfg)) {
        xeno_patch_extensions(physicalDevice, real_vkEnumerateDeviceExtensionProperties, pLayerName, pPropertyCount, pProperties);
        return VK_SUCCESS; // emulate usual behavior
    }
    return real_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    if (!real_vkCreateDevice) real_vkCreateDevice = (PFN_vkCreateDevice)real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
    VkResult r = real_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (r == VK_SUCCESS) {
        resolve_device_entrypoints(*pDevice);
    }
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    if (!real_vkCreateSwapchainKHR) real_vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)real_vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
    return real_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    if (!real_vkQueuePresentKHR) real_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)real_vkGetDeviceProcAddr(queue ? *(VkDevice*)(&queue) : VK_NULL_HANDLE, "vkQueuePresentKHR");
    VkResult r = real_vkQueuePresentKHR(queue, pPresentInfo);
    if (g_log_fps) {
        ++g_frames;
        double t = now_sec();
        if (t - g_last_fps_time >= 1.0) {
            XENO_LOGI("FPS ~ %d", g_frames);
            g_frames = 0;
            g_last_fps_time = t;
        }
    }
    return r;
}

