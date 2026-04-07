#pragma once

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V1.6.0 — Pipeline Cache Manager
// Saves/loads VkPipelineCache to disk to eliminate shader compilation stutter.
// ═══════════════════════════════════════════════════════════════════════════

typedef struct ExynosPipelineCache {
    VkDevice         device;
    VkPipelineCache  cache;
    char             file_path[256];
    int              dirty;  // 1 = cache was modified, needs saving
} ExynosPipelineCache;

/// Initialize pipeline cache, loading from disk if available.
VkResult exynos_pcache_init(ExynosPipelineCache* pc,
                            VkDevice device,
                            const char* cache_file_path);

/// Save pipeline cache to disk (call before vkDestroyDevice).
VkResult exynos_pcache_save(ExynosPipelineCache* pc);

/// Get the VkPipelineCache handle for use in vkCreateComputePipelines.
VkPipelineCache exynos_pcache_get(const ExynosPipelineCache* pc);

/// Mark cache as dirty (new pipelines were created).
void exynos_pcache_mark_dirty(ExynosPipelineCache* pc);

/// Destroy pipeline cache and free resources.
void exynos_pcache_destroy(ExynosPipelineCache* pc);

#ifdef __cplusplus
}
#endif
