#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef XCLIPSE_ANDROID_LOG
#include <android/log.h>
#define LOG_TAG "xclipse_wrapper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <stdio.h>
#define LOGI(...) fprintf(stderr, __VA_ARGS__), fputc('\n', stderr)
#define LOGE(...) fprintf(stderr, __VA_ARGS__), fputc('\n', stderr)
#endif

// Minimal Vulkan ICD-style entry redirection for vkGetInstanceProcAddr
typedef void* (*PFN_vkGetInstanceProcAddr)(void* instance, const char* pName);

static void* s_real_vulkan_loader_handle = NULL;
static PFN_vkGetInstanceProcAddr s_real_vkGetInstanceProcAddr = NULL;

static void xclipse_load_real_loader_once(void) __attribute__((constructor));
static void xclipse_unload_real_loader(void) __attribute__((destructor));

static const char* resolve_loader_path(void) {
    const char* override_path = getenv("XCLIPSE_WRAPPER_LOADER");
    if (override_path && override_path[0] != '\0') {
        return override_path;
    }
    // Fallbacks commonly found in Winlator/Android environments
    // Winlator Bionic exposes libvulkan.so.1, while some Android systems use libvulkan.so
    return "libvulkan.so.1";
}

static void xclipse_load_real_loader_once(void) {
    if (s_real_vulkan_loader_handle != NULL) {
        return;
    }
    const char* loader_path = resolve_loader_path();
    s_real_vulkan_loader_handle = dlopen(loader_path, RTLD_NOW | RTLD_LOCAL);
    if (!s_real_vulkan_loader_handle) {
        // Try secondary name
        s_real_vulkan_loader_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!s_real_vulkan_loader_handle) {
        LOGE("xclipse_wrapper: Failed to load Vulkan loader: %s", dlerror());
        return;
    }

    s_real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(s_real_vulkan_loader_handle, "vkGetInstanceProcAddr");
    if (!s_real_vkGetInstanceProcAddr) {
        LOGE("xclipse_wrapper: Failed to resolve vkGetInstanceProcAddr: %s", dlerror());
    } else {
        LOGI("xclipse_wrapper: Loaded Vulkan loader and resolved vkGetInstanceProcAddr");
    }
}

static void xclipse_unload_real_loader(void) {
    if (s_real_vulkan_loader_handle) {
        dlclose(s_real_vulkan_loader_handle);
        s_real_vulkan_loader_handle = NULL;
        s_real_vkGetInstanceProcAddr = NULL;
    }
}

// Minimal export required by Vulkan loader
__attribute__((visibility("default")))
void* vkGetInstanceProcAddr(void* instance, const char* pName) {
    if (!s_real_vkGetInstanceProcAddr) {
        xclipse_load_real_loader_once();
        if (!s_real_vkGetInstanceProcAddr) {
            return NULL;
        }
    }

    // Hook point for future features: BCn emulation, dynamic rendering emulation, etc.
    // For now, just forward all queries to the real loader.
    return s_real_vkGetInstanceProcAddr(instance, pName);
}

