#include "watchdog.h"

#include <string.h>
#include <android/log.h>

#define WD_TAG "ExynosToolsWatchdog"
#define WD_LOGI(...) __android_log_print(ANDROID_LOG_INFO, WD_TAG, __VA_ARGS__)
#define WD_LOGW(...) __android_log_print(ANDROID_LOG_WARN, WD_TAG, __VA_ARGS__)
#define WD_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, WD_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════════

void exynos_watchdog_init(ExynosWatchdog* wd, VkDevice device, uint32_t timeout_ms) {
    if (!wd) return;
    memset(wd, 0, sizeof(*wd));
    wd->device     = device;
    wd->timeout_ms = timeout_ms > 0 ? timeout_ms : 5000;
    wd->enabled    = 1;
    WD_LOGI("Watchdog initialized: timeout=%ums", wd->timeout_ms);
}

// ═══════════════════════════════════════════════════════════════════════════
// Safe Fence Wait — masks VK_ERROR_DEVICE_LOST
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_watchdog_wait_fence(ExynosWatchdog* wd,
                                     VkFence fence,
                                     uint64_t timeout_ns_override) {
    if (!wd || !wd->enabled || wd->device == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }

    // If device is already permanently lost, skip all GPU work
    if (wd->device_is_lost) {
        return VK_SUCCESS;
    }

    uint64_t timeout_ns = timeout_ns_override > 0
        ? timeout_ns_override
        : (uint64_t)wd->timeout_ms * 1000000ULL;

    VkResult res = vkWaitForFences(wd->device, 1, &fence, VK_TRUE, timeout_ns);

    switch (res) {
        case VK_SUCCESS:
            return VK_SUCCESS;

        case VK_TIMEOUT:
            wd->timeout_count++;
            WD_LOGW("GPU fence TIMEOUT #%u (waited %ums). "
                     "Resetting fence and continuing...",
                     wd->timeout_count, wd->timeout_ms);

            // Try to reset the fence and let the emulator continue
            vkResetFences(wd->device, 1, &fence);
            return VK_SUCCESS; // Mask the timeout

        case VK_ERROR_DEVICE_LOST:
            wd->device_lost_count++;
            WD_LOGE("VK_ERROR_DEVICE_LOST #%u intercepted by Watchdog! "
                     "Masking error to prevent emulator crash.",
                     wd->device_lost_count);

            // After 3 consecutive device-losts, consider it permanent
            if (wd->device_lost_count >= 3) {
                wd->device_is_lost = 1;
                WD_LOGE("Device permanently lost after %u attempts. "
                         "All future GPU work will be skipped.",
                         wd->device_lost_count);
            }
            return VK_SUCCESS; // CRITICAL: Mask the fatal error

        default:
            WD_LOGW("Unexpected fence result: %d", res);
            return VK_SUCCESS; // Mask any unknown error too
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Safe Queue Submit — wraps vkQueueSubmit with recovery
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_watchdog_queue_submit(ExynosWatchdog* wd,
                                       VkQueue queue,
                                       uint32_t submitCount,
                                       const VkSubmitInfo* pSubmits,
                                       VkFence fence) {
    if (!wd || !wd->enabled) {
        // Passthrough if watchdog is disabled
        return vkQueueSubmit(queue, submitCount, pSubmits, fence);
    }

    // If device is permanently lost, silently succeed
    if (wd->device_is_lost) {
        WD_LOGW("Skipping vkQueueSubmit (device is permanently lost)");
        return VK_SUCCESS;
    }

    VkResult res = vkQueueSubmit(queue, submitCount, pSubmits, fence);

    if (res == VK_ERROR_DEVICE_LOST) {
        wd->device_lost_count++;
        WD_LOGE("vkQueueSubmit returned VK_ERROR_DEVICE_LOST #%u. "
                 "Masking error.",
                 wd->device_lost_count);

        if (wd->device_lost_count >= 3) {
            wd->device_is_lost = 1;
            WD_LOGE("Device permanently lost. Future submits will be no-ops.");
        }
        return VK_SUCCESS;
    }

    if (res != VK_SUCCESS) {
        WD_LOGW("vkQueueSubmit returned error %d, masking", res);
        return VK_SUCCESS;
    }

    return VK_SUCCESS;
}

int exynos_watchdog_is_device_lost(const ExynosWatchdog* wd) {
    return (wd && wd->device_is_lost) ? 1 : 0;
}

void exynos_watchdog_get_stats(const ExynosWatchdog* wd,
                                uint32_t* out_lost_count,
                                uint32_t* out_timeout_count) {
    if (!wd) return;
    if (out_lost_count)    *out_lost_count    = wd->device_lost_count;
    if (out_timeout_count) *out_timeout_count = wd->timeout_count;
}
