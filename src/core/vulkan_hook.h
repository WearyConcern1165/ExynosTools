#pragma once
// ExynosTools - Real Driver Bridge
// Loads libvulkan_real.so from the same directory as this wrapper.

#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <android/log.h>
#include <mutex>
#include <string>
#include <cstring>
#include <dirent.h>

#define LOG_TAG "ExynosTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace bcn {

class VulkanHook {
public:
    static VulkanHook& instance() {
        static VulkanHook inst;
        return inst;
    }

    bool init() {
        std::call_once(m_initFlag, [this]() {
            LOGI("==============================================");
            LOGI("ExynosTools v1.7 - Initializing Driver Bridge");
            LOGI("==============================================");

            // Get our own base address and path to avoid recursion.
            Dl_info myInfo;
            if (dladdr((void*)&instance, &myInfo) && myInfo.dli_fname) {
                m_myBaseAddr = myInfo.dli_fbase;
                m_myPath = myInfo.dli_fname;
                LOGI("  My location: %s", m_myPath.c_str());
            }

            if (!tryLoadLocalRealDriver()) {
                if (!tryLoadVendorFallbacks()) {
                    if (!tryLoadSystemLoader()) {
                        LOGE("ERROR: No real driver found.");
                        LOGE("Make sure libvulkan_real.so is in the ZIP");
                        LOGE("alongside libvulkan_exynos.so / libvulkan_adreno.so");
                    }
                }
            }

            if (m_ready) {
                LOGI("Driver Bridge Ready!");
            }
        });
        return m_ready;
    }

    VkResult enumerateInstanceExtensions(
        const char* pLayerName,
        uint32_t* pCount,
        VkExtensionProperties* pProps) {
        if (m_vkEnumerateInstanceExtensionProperties) {
            return m_vkEnumerateInstanceExtensionProperties(pLayerName, pCount, pProps);
        }
        if (pCount) *pCount = 0;
        return VK_SUCCESS;
    }

    VkResult enumerateDeviceExtensions(
        VkPhysicalDevice pd,
        const char* pLayerName,
        uint32_t* pCount,
        VkExtensionProperties* pProps) {
        if (m_vkEnumerateDeviceExtensionProperties) {
            return m_vkEnumerateDeviceExtensionProperties(pd, pLayerName, pCount, pProps);
        }
        if (pCount) *pCount = 0;
        return VK_SUCCESS;
    }

    void* getProcAddr(VkInstance instance, const char* pName) {
        if (m_realGIPA) return (void*)m_realGIPA(instance, pName);
        return nullptr;
    }

private:
    VulkanHook() = default;
    ~VulkanHook() { if (m_realDriver) dlclose(m_realDriver); }

    bool tryLoadLocalRealDriver() {
        if (m_myPath.empty()) return false;

        size_t slashPos = m_myPath.find_last_of("/\\");
        if (slashPos == std::string::npos) return false;

        std::string dir = m_myPath.substr(0, slashPos + 1);
        std::string realDriverPath = dir + "libvulkan_real.so";

        LOGI("  [Strategy A] Trying local: %s", realDriverPath.c_str());
        return tryLoad(realDriverPath.c_str());
    }

    bool tryLoadVendorFallbacks() {
        LOGI("  [Strategy B] Trying vendor fallbacks...");
        const char* paths[] = {
            "/vendor/lib64/hw/vulkan.samsung.so",
            "/vendor/lib64/hw/vulkan.exynos.so",
            "/vendor/lib64/hw/vulkan.mali.so",
            nullptr
        };

        for (int i = 0; paths[i]; i++) {
            LOGI("    Checking %s", paths[i]);
            if (tryLoad(paths[i])) return true;
        }

        DIR* vdir = opendir("/vendor/lib64/hw/");
        if (vdir) {
            struct dirent* entry;
            while ((entry = readdir(vdir))) {
                if (strncmp(entry->d_name, "vulkan.", 7) == 0) {
                    std::string p = "/vendor/lib64/hw/";
                    p += entry->d_name;
                    LOGI("    Scanning found: %s", p.c_str());
                    if (tryLoad(p.c_str())) {
                        closedir(vdir);
                        return true;
                    }
                }
            }
            closedir(vdir);
        }
        return false;
    }

    bool tryLoadSystemLoader() {
        LOGI("  [Strategy C] Last resort: libvulkan.so");
        return tryLoad("libvulkan.so");
    }

    bool probeExtensions(PFN_vkEnumerateInstanceExtensionProperties enumInst) {
        if (!enumInst) return false;
        uint32_t count = 0;
        VkResult res = enumInst(nullptr, &count, nullptr);
        if (res != VK_SUCCESS || count == 0) {
            LOGE("    Probe failed: vkEnumerateInstanceExtensionProperties returned %d, count=%u", (int)res, count);
            return false;
        }
        LOGI("    Probe success: %u instance extensions", count);
        return true;
    }

    bool tryLoad(const char* path) {
        void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            LOGI("    Failed to load %s: %s", path, dlerror());
            return false;
        }

        auto gipa = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
        if (!gipa) {
            LOGI("    No vkGetInstanceProcAddr found in %s", path);
            dlclose(h);
            return false;
        }

        Dl_info info;
        if (dladdr((void*)gipa, &info)) {
            if (info.dli_fbase == m_myBaseAddr) {
                LOGI("    Recursion detected for %s. Skipping.", path);
                dlclose(h);
                return false;
            }
        }

        auto enumInst = (PFN_vkEnumerateInstanceExtensionProperties)gipa(nullptr, "vkEnumerateInstanceExtensionProperties");
        auto enumDev = (PFN_vkEnumerateDeviceExtensionProperties)gipa(nullptr, "vkEnumerateDeviceExtensionProperties");

        if (!probeExtensions(enumInst)) {
            LOGE("    Rejecting %s due to zero extensions", path);
            dlclose(h);
            return false;
        }

        m_realDriver = h;
        m_realGIPA = gipa;
        m_vkEnumerateInstanceExtensionProperties = enumInst;
        m_vkEnumerateDeviceExtensionProperties = enumDev;

        LOGI("    Loaded %s", path);
        m_ready = true;
        return true;
    }

    std::once_flag m_initFlag;
    void* m_realDriver = nullptr;
    PFN_vkGetInstanceProcAddr m_realGIPA = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties m_vkEnumerateInstanceExtensionProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties m_vkEnumerateDeviceExtensionProperties = nullptr;

    std::string m_myPath;
    void* m_myBaseAddr = nullptr;
    bool m_ready = false;
};

} // namespace bcn
