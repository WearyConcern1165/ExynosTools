#include "logging.h"
#include "detect.h"
#include <string.h>

// Known Samsung vendor ID and Xclipse device IDs
#define VENDOR_ID_SAMSUNG 0x144D
static const unsigned xclipse_device_ids[] = {
    0x73A0, // Xclipse 920 (Exynos 2400)
    0x73A1, // Xclipse 920 variant
    0x73B0, // Xclipse 940 (Exynos 2500)
    0x73B1, // Xclipse 940 variant
    0x7300, // Xclipse 930 (theoretical)
    0x7310, // Additional Xclipse variants
    0x7320, // Additional Xclipse variants
};

static int is_whitelisted_by_env(void) {
    const char* wl = getenv("EXYNOSTOOLS_WHITELIST");
    return wl && *wl; // any non-empty string enables
}

void xeno_detect_parse_env(XenoDetectConfig* cfg) {
    cfg->force_enable = 0;
    cfg->force_disable = 0;
    const char* force = getenv("EXYNOSTOOLS_FORCE");
    if (force && *force == '1') cfg->force_enable = 1;
    const char* force_off = getenv("EXYNOSTOOLS_FORCE");
    if (force_off && *force_off == '0') cfg->force_disable = 1;
}

int xeno_is_xclipse_gpu(VkPhysicalDevice phys, const XenoDetectConfig* cfg) {
    if (cfg && cfg->force_enable) {
        XENO_LOGI("detect: forced enable via EXYNOSTOOLS_FORCE=1");
        return 1;
    }
    if (cfg && cfg->force_disable) {
        XENO_LOGI("detect: forced disable via EXYNOSTOOLS_FORCE=0");
        return 0;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);

    XENO_LOGD("detect: vendorID=0x%04x deviceID=0x%04x deviceName=%s",
              props.vendorID, props.deviceID, props.deviceName);

    if (props.vendorID == VENDOR_ID_SAMSUNG) {
        // Heuristic: treat Samsung with deviceName containing Xclipse as Xclipse GPU
        if (strstr(props.deviceName, "Xclipse") != NULL) {
            return 1;
        }
        // Device ID list
        for (size_t i = 0; i < sizeof(xclipse_device_ids)/sizeof(xclipse_device_ids[0]); ++i) {
            if (props.deviceID == xclipse_device_ids[i]) return 1;
        }
        // Allow whitelist override
        if (is_whitelisted_by_env()) {
            XENO_LOGI("detect: Samsung device whitelisted via EXYNOSTOOLS_WHITELIST");
            return 1;
        }
    }
    return 0;
}

