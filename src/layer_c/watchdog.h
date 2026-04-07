#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V1.6.0 — Watchdog Anti-Crash System
// Intercepts VK_ERROR_DEVICE_LOST and GPU hangs, preventing emulator crashes.
// ═══════════════════════════════════════════════════════════════════════════

typedef struct ExynosWatchdog {
    VkDevice    device;
    uint32_t    timeout_ms;        // Fence wait timeout
    int         enabled;
    uint32_t    device_lost_count; // How many device-lost errors we masked
    uint32_t    timeout_count;     // How many timeouts we recovered from
    int         device_is_lost;    // Permanent flag once GPU truly dies
} ExynosWatchdog;

/// Initialize watchdog.
void exynos_watchdog_init(ExynosWatchdog* wd, VkDevice device, uint32_t timeout_ms);

/// Safe fence wait — returns VK_SUCCESS even if device is lost (masks error).
VkResult exynos_watchdog_wait_fence(ExynosWatchdog* wd,
                                     VkFence fence,
                                     uint64_t timeout_ns_override);

/// Safe queue submit — wraps vkQueueSubmit with error recovery.
VkResult exynos_watchdog_queue_submit(ExynosWatchdog* wd,
                                       VkQueue queue,
                                       uint32_t submitCount,
                                       const VkSubmitInfo* pSubmits,
                                       VkFence fence);

/// Check if device has been lost (for graceful fallback decisions).
int exynos_watchdog_is_device_lost(const ExynosWatchdog* wd);

/// Get stats for logging.
void exynos_watchdog_get_stats(const ExynosWatchdog* wd,
                                uint32_t* out_lost_count,
                                uint32_t* out_timeout_count);

#ifdef __cplusplus
}
#endif
