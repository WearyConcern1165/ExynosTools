#include "query_interceptor.h"
#include <string.h>
#include <android/log.h>

#define QI_TAG "ExynosQueryIntcpt"
#define QI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, QI_TAG, __VA_ARGS__)
#define QI_LOGW(...) __android_log_print(ANDROID_LOG_WARN, QI_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// Init / Destroy
// ═══════════════════════════════════════════════════════════════════════════

void exynos_query_init(ExynosQueryInterceptor* qi, VkDevice device) {
    if (!qi) return;
    memset(qi, 0, sizeof(*qi));
    qi->enabled = 1;
    qi->device = device;
    QI_LOGI("Query Interceptor V2.1.1 initialized (max_pools=%d)", EXYNOS_MAX_EMULATED_POOLS);
}

void exynos_query_destroy(ExynosQueryInterceptor* qi) {
    if (!qi) return;
    QI_LOGI("Query Interceptor stats: intercepted=%u fallbacks=%u",
            qi->stats_intercepted, qi->stats_fallback_used);
    qi->enabled = 0;
    qi->emulated_pool_count = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: track emulated pool
// ═══════════════════════════════════════════════════════════════════════════

static void qi_track_pool(ExynosQueryInterceptor* qi, VkQueryPool pool) {
    if (qi->emulated_pool_count < EXYNOS_MAX_EMULATED_POOLS) {
        qi->emulated_pools[qi->emulated_pool_count++] = pool;
    }
}

int exynos_query_is_emulated(const ExynosQueryInterceptor* qi, VkQueryPool pool) {
    if (!qi) return 0;
    for (uint32_t i = 0; i < qi->emulated_pool_count; i++) {
        if (qi->emulated_pools[i] == pool) return 1;
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// vkCreateQueryPool interceptor
//
// Strategy:
//   - VK_QUERY_TYPE_OCCLUSION: let it pass through (RDNA3 supports it)
//   - VK_QUERY_TYPE_PIPELINE_STATISTICS: Samsung may reject this.
//     Try the real call first; if it fails, create a safe TIMESTAMP
//     pool as fallback and track it as "emulated".
//   - VK_QUERY_TYPE_TIMESTAMP: always pass through (universally supported)
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_query_create_pool(
    ExynosQueryInterceptor* qi,
    const VkQueryPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkQueryPool* pQueryPool,
    PFN_vkCreateQueryPool real_fn) {

    if (!qi || !qi->enabled || !real_fn || !pCreateInfo || !pQueryPool) {
        // Passthrough if interceptor is disabled
        if (real_fn && pCreateInfo && pQueryPool) {
            return real_fn(qi ? qi->device : VK_NULL_HANDLE,
                           pCreateInfo, pAllocator, pQueryPool);
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    qi->stats_intercepted++;

    // Try the real call first — it might work!
    VkResult result = real_fn(qi->device, pCreateInfo, pAllocator, pQueryPool);

    if (result == VK_SUCCESS) {
        // Native driver accepted it, no intervention needed
        return VK_SUCCESS;
    }

    // ─── Fallback for PIPELINE_STATISTICS ────────────────────────────
    if (pCreateInfo->queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
        QI_LOGW("PIPELINE_STATISTICS rejected by driver (err=%d), creating TIMESTAMP fallback",
                result);

        // Create a safe timestamp query pool instead
        VkQueryPoolCreateInfo safe_info;
        memcpy(&safe_info, pCreateInfo, sizeof(safe_info));
        safe_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        safe_info.pipelineStatistics = 0;  // Clear stats flags

        result = real_fn(qi->device, &safe_info, pAllocator, pQueryPool);
        if (result == VK_SUCCESS) {
            qi_track_pool(qi, *pQueryPool);
            qi->stats_fallback_used++;
            QI_LOGI("Fallback TIMESTAMP pool created (handle=%p)", (void*)*pQueryPool);
        }
        return result;
    }

    // Other query types failed — nothing we can do, return original error
    QI_LOGW("QueryPool creation failed (type=%d, err=%d)", pCreateInfo->queryType, result);
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// vkGetQueryPoolResults interceptor
//
// For emulated (fallback) pools, return zeros instead of querying the
// timestamp pool. This is safe because DXVK uses pipeline statistics
// only for debug/profiling overlays, not for rendering decisions.
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_query_get_results(
    ExynosQueryInterceptor* qi,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount,
    size_t dataSize,
    void* pData,
    VkDeviceSize stride,
    VkQueryResultFlags flags,
    PFN_vkGetQueryPoolResults real_fn) {

    if (!qi || !qi->enabled) {
        if (real_fn) {
            return real_fn(qi ? qi->device : VK_NULL_HANDLE,
                           queryPool, firstQuery, queryCount,
                           dataSize, pData, stride, flags);
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Check if this is one of our emulated pools
    if (exynos_query_is_emulated(qi, queryPool)) {
        // Return zeros — DXVK will see "0 primitives rendered" which is
        // harmless for its HUD overlay stats
        if (pData && dataSize > 0) {
            memset(pData, 0, dataSize);
        }
        return VK_SUCCESS;
    }

    // Real pool — pass through to native driver
    return real_fn(qi->device, queryPool, firstQuery, queryCount,
                   dataSize, pData, stride, flags);
}
