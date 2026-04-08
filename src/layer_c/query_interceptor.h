#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V2.1.1 — Query Pool Safety Interceptor
//
// Category B features (pipelineStatisticsQuery, occlusionQueryPrecise) are
// spoofed as VK_TRUE to pass DXVK validation. However, the native Samsung
// driver may not support them. This interceptor catches the dangerous calls
// and provides safe fallbacks to prevent crashes.
//
// Intercepted functions:
//   - vkCreateQueryPool: If pipeline statistics type is unsupported,
//     silently downgrade to VK_QUERY_TYPE_TIMESTAMP (always supported).
//   - vkGetQueryPoolResults: Return zeros for emulated query pools.
// ═══════════════════════════════════════════════════════════════════════════

#define EXYNOS_MAX_EMULATED_POOLS 64

typedef struct ExynosQueryInterceptor {
    int      enabled;
    VkDevice device;
    // Track which query pools are "emulated" (not real pipeline stats)
    VkQueryPool emulated_pools[EXYNOS_MAX_EMULATED_POOLS];
    uint32_t    emulated_pool_count;
    uint32_t    stats_intercepted;
    uint32_t    stats_fallback_used;
} ExynosQueryInterceptor;

/// Initialize the query interceptor.
void exynos_query_init(ExynosQueryInterceptor* qi, VkDevice device);

/// Destroy the query interceptor.
void exynos_query_destroy(ExynosQueryInterceptor* qi);

/// Intercept vkCreateQueryPool. Returns VK_SUCCESS always.
/// If the query type is unsupported, creates a safe fallback pool.
VkResult exynos_query_create_pool(
    ExynosQueryInterceptor* qi,
    const VkQueryPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkQueryPool* pQueryPool,
    PFN_vkCreateQueryPool real_fn);

/// Intercept vkGetQueryPoolResults. Returns zeros for emulated pools.
VkResult exynos_query_get_results(
    ExynosQueryInterceptor* qi,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount,
    size_t dataSize,
    void* pData,
    VkDeviceSize stride,
    VkQueryResultFlags flags,
    PFN_vkGetQueryPoolResults real_fn);

/// Check if a query pool is emulated (created by us as fallback).
int exynos_query_is_emulated(const ExynosQueryInterceptor* qi, VkQueryPool pool);

#ifdef __cplusplus
}
#endif
