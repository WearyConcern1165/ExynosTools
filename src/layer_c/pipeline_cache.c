#include "pipeline_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define PCACHE_TAG "ExynosToolsPCache"
#define PCACHE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, PCACHE_TAG, __VA_ARGS__)
#define PCACHE_LOGW(...) __android_log_print(ANDROID_LOG_WARN, PCACHE_TAG, __VA_ARGS__)
#define PCACHE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, PCACHE_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// Load cache binary from disk
// ═══════════════════════════════════════════════════════════════════════════

static void* load_file_binary(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) {
        fclose(f);
        return NULL;
    }

    void* data = malloc((size_t)sz);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)sz, f);
    fclose(f);

    if (read != (size_t)sz) {
        free(data);
        return NULL;
    }

    *out_size = (size_t)sz;
    return data;
}

// ═══════════════════════════════════════════════════════════════════════════
// Init: load existing cache or create empty
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_pcache_init(ExynosPipelineCache* pc,
                            VkDevice device,
                            const char* cache_file_path) {
    if (!pc || device == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;

    memset(pc, 0, sizeof(*pc));
    pc->device = device;
    pc->dirty = 0;

    if (cache_file_path) {
        strncpy(pc->file_path, cache_file_path, sizeof(pc->file_path) - 1);
    }

    // Try to load existing cache from disk
    size_t cache_size = 0;
    void* cache_data = NULL;

    if (pc->file_path[0] != '\0') {
        cache_data = load_file_binary(pc->file_path, &cache_size);
        if (cache_data) {
            PCACHE_LOGI("Loaded pipeline cache from disk: %zu bytes", cache_size);
        } else {
            PCACHE_LOGI("No existing pipeline cache found, creating fresh");
        }
    }

    VkPipelineCacheCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    ci.initialDataSize = cache_size;
    ci.pInitialData    = cache_data;

    VkResult res = vkCreatePipelineCache(device, &ci, NULL, &pc->cache);

    if (cache_data) free(cache_data);

    if (res != VK_SUCCESS) {
        PCACHE_LOGE("Failed to create pipeline cache: %d", res);
        return res;
    }

    PCACHE_LOGI("Pipeline cache initialized successfully");
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════
// Save cache to disk
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_pcache_save(ExynosPipelineCache* pc) {
    if (!pc || pc->cache == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
    if (pc->file_path[0] == '\0') return VK_SUCCESS; // No path configured

    size_t data_size = 0;
    VkResult res = vkGetPipelineCacheData(pc->device, pc->cache, &data_size, NULL);
    if (res != VK_SUCCESS || data_size == 0) {
        PCACHE_LOGW("Pipeline cache is empty, nothing to save");
        return VK_SUCCESS;
    }

    void* data = malloc(data_size);
    if (!data) return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = vkGetPipelineCacheData(pc->device, pc->cache, &data_size, data);
    if (res != VK_SUCCESS) {
        free(data);
        return res;
    }

    FILE* f = fopen(pc->file_path, "wb");
    if (!f) {
        PCACHE_LOGW("Cannot write pipeline cache to: %s", pc->file_path);
        free(data);
        return VK_SUCCESS; // Non-fatal
    }

    fwrite(data, 1, data_size, f);
    fclose(f);
    free(data);

    PCACHE_LOGI("Pipeline cache saved: %zu bytes -> %s", data_size, pc->file_path);
    pc->dirty = 0;
    return VK_SUCCESS;
}

VkPipelineCache exynos_pcache_get(const ExynosPipelineCache* pc) {
    return pc ? pc->cache : VK_NULL_HANDLE;
}

void exynos_pcache_mark_dirty(ExynosPipelineCache* pc) {
    if (pc) pc->dirty = 1;
}

void exynos_pcache_destroy(ExynosPipelineCache* pc) {
    if (!pc) return;

    // Auto-save if dirty
    if (pc->dirty && pc->cache != VK_NULL_HANDLE) {
        exynos_pcache_save(pc);
    }

    if (pc->cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(pc->device, pc->cache, NULL);
        pc->cache = VK_NULL_HANDLE;
    }
    PCACHE_LOGI("Pipeline cache destroyed");
}
