#include "logging.h"
#include "detect.h"
#include <string.h>

// Known Samsung vendor ID and Xclipse device IDs
#define VENDOR_ID_SAMSUNG 0x144D
static unsigned xclipse_device_ids[32];
static size_t xclipse_device_ids_count = 0;

static void load_device_ids_from_env(void) {
    if (xclipse_device_ids_count > 0) return;
    const char* env = getenv("EXYNOSTOOLS_XCLIPSE_IDS"); // e.g., "0x3940,0x3941"
    if (!env || !*env) return;
    char buf[256]; strncpy(buf, env, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    char* tok = strtok(buf, ",");
    while (tok && xclipse_device_ids_count < 32) {
        unsigned val = 0; sscanf(tok, "%x", &val);
        xclipse_device_ids[xclipse_device_ids_count++] = val;
        tok = strtok(NULL, ",");
    }
}

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
        load_device_ids_from_env();
        for (size_t i = 0; i < xclipse_device_ids_count; ++i) {
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

