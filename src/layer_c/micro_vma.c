#include "micro_vma.h"

#include <string.h>
#include <android/log.h>

#define VMA_TAG "ExynosToolsVMA"
#define VMA_LOGI(...) __android_log_print(ANDROID_LOG_INFO, VMA_TAG, __VA_ARGS__)
#define VMA_LOGW(...) __android_log_print(ANDROID_LOG_WARN, VMA_TAG, __VA_ARGS__)
#define VMA_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, VMA_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// Alignment helper
// ═══════════════════════════════════════════════════════════════════════════

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_vma_init(ExynosVma* vma,
                          VkDevice device,
                          VkPhysicalDevice physical_device,
                          uint32_t block_size_mb) {
    if (!vma || device == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;

    memset(vma, 0, sizeof(*vma));
    vma->device          = device;
    vma->physical_device = physical_device;
    vma->block_size      = (VkDeviceSize)block_size_mb * 1024ULL * 1024ULL;
    vma->enabled         = 1;
    pthread_mutex_init(&vma->lock, NULL);

    VMA_LOGI("Micro-VMA initialized: block_size=%uMB, max_blocks=%d",
             block_size_mb, EXYNOS_VMA_MAX_BLOCKS);
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════
// Create a new mega-block
// ═══════════════════════════════════════════════════════════════════════════

static VkResult create_block(ExynosVma* vma, uint32_t memory_type_index, ExynosVmaBlock** out) {
    if (vma->block_count >= EXYNOS_VMA_MAX_BLOCKS) {
        VMA_LOGE("Max blocks reached (%d)", EXYNOS_VMA_MAX_BLOCKS);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    ExynosVmaBlock* block = &vma->blocks[vma->block_count];
    memset(block, 0, sizeof(*block));
    block->block_size        = vma->block_size;
    block->memory_type_index = memory_type_index;

    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = vma->block_size;
    ai.memoryTypeIndex = memory_type_index;

    VkResult res = vkAllocateMemory(vma->device, &ai, NULL, &block->memory);
    if (res != VK_SUCCESS) {
        VMA_LOGE("Failed to allocate mega-block (%uMB, type %u): %d",
                 (uint32_t)(vma->block_size / (1024*1024)), memory_type_index, res);
        return res;
    }

    vma->block_count++;
    vma->total_allocated += vma->block_size;
    *out = block;

    VMA_LOGI("Created mega-block #%u: %uMB (memory_type=%u)",
             vma->block_count, (uint32_t)(vma->block_size / (1024*1024)), memory_type_index);
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-allocate from an existing block (first-fit)
// ═══════════════════════════════════════════════════════════════════════════

static VkResult try_suballoc(ExynosVmaBlock* block,
                              VkDeviceSize size,
                              VkDeviceSize alignment,
                              VkDeviceSize* out_offset) {
    // First-fit algorithm
    VkDeviceSize cursor = 0;

    for (uint32_t i = 0; i < block->suballoc_count; i++) {
        if (!block->suballocs[i].in_use) continue;

        VkDeviceSize aligned_cursor = align_up(cursor, alignment);
        if (aligned_cursor + size <= block->suballocs[i].offset) {
            // Found a gap before this allocation
            if (block->suballoc_count >= EXYNOS_VMA_MAX_SUBALLOCS)
                return VK_ERROR_OUT_OF_HOST_MEMORY;

            // Shift suballocs to make room (insert sorted)
            for (uint32_t j = block->suballoc_count; j > i; j--) {
                block->suballocs[j] = block->suballocs[j - 1];
            }
            block->suballocs[i].offset = aligned_cursor;
            block->suballocs[i].size   = size;
            block->suballocs[i].in_use = 1;
            block->suballoc_count++;
            block->used_bytes += size;
            *out_offset = aligned_cursor;
            return VK_SUCCESS;
        }

        cursor = block->suballocs[i].offset + block->suballocs[i].size;
    }

    // Try after last allocation
    VkDeviceSize aligned_cursor = align_up(cursor, alignment);
    if (aligned_cursor + size <= block->block_size) {
        if (block->suballoc_count >= EXYNOS_VMA_MAX_SUBALLOCS)
            return VK_ERROR_OUT_OF_HOST_MEMORY;

        uint32_t idx = block->suballoc_count;
        block->suballocs[idx].offset = aligned_cursor;
        block->suballocs[idx].size   = size;
        block->suballocs[idx].in_use = 1;
        block->suballoc_count++;
        block->used_bytes += size;
        *out_offset = aligned_cursor;
        return VK_SUCCESS;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY; // Block is full
}

// ═══════════════════════════════════════════════════════════════════════════
// Public: Allocate
// ═══════════════════════════════════════════════════════════════════════════

VkResult exynos_vma_alloc(ExynosVma* vma,
                           const VkMemoryRequirements* reqs,
                           uint32_t memory_type_index,
                           VkDeviceMemory* out_memory,
                           VkDeviceSize* out_offset) {
    if (!vma || !vma->enabled || !reqs || !out_memory || !out_offset)
        return VK_ERROR_INITIALIZATION_FAILED;

    // If allocation is larger than block size, fall through to normal Vulkan
    if (reqs->size > vma->block_size) {
        return VK_INCOMPLETE; // Caller should use vkAllocateMemory directly
    }

    pthread_mutex_lock(&vma->lock);

    // Try existing blocks
    for (uint32_t i = 0; i < vma->block_count; i++) {
        if (vma->blocks[i].memory_type_index != memory_type_index) continue;

        VkDeviceSize offset;
        VkResult res = try_suballoc(&vma->blocks[i], reqs->size, reqs->alignment, &offset);
        if (res == VK_SUCCESS) {
            *out_memory = vma->blocks[i].memory;
            *out_offset = offset;
            vma->total_suballocs++;
            vma->saved_alloc_calls++;
            pthread_mutex_unlock(&vma->lock);
            return VK_SUCCESS;
        }
    }

    // Need a new block
    ExynosVmaBlock* new_block = NULL;
    VkResult res = create_block(vma, memory_type_index, &new_block);
    if (res != VK_SUCCESS) {
        pthread_mutex_unlock(&vma->lock);
        return res;
    }

    VkDeviceSize offset;
    res = try_suballoc(new_block, reqs->size, reqs->alignment, &offset);
    if (res == VK_SUCCESS) {
        *out_memory = new_block->memory;
        *out_offset = offset;
        vma->total_suballocs++;
        vma->saved_alloc_calls++;
    }

    pthread_mutex_unlock(&vma->lock);
    return res;
}

// ═══════════════════════════════════════════════════════════════════════════
// Free
// ═══════════════════════════════════════════════════════════════════════════

void exynos_vma_free(ExynosVma* vma,
                      VkDeviceMemory memory,
                      VkDeviceSize offset) {
    if (!vma || !vma->enabled) return;

    pthread_mutex_lock(&vma->lock);

    for (uint32_t i = 0; i < vma->block_count; i++) {
        if (vma->blocks[i].memory != memory) continue;

        for (uint32_t j = 0; j < vma->blocks[i].suballoc_count; j++) {
            if (vma->blocks[i].suballocs[j].offset == offset &&
                vma->blocks[i].suballocs[j].in_use) {
                vma->blocks[i].used_bytes -= vma->blocks[i].suballocs[j].size;
                vma->blocks[i].suballocs[j].in_use = 0;
                break;
            }
        }
        break;
    }

    pthread_mutex_unlock(&vma->lock);
}

// ═══════════════════════════════════════════════════════════════════════════
// Stats
// ═══════════════════════════════════════════════════════════════════════════

void exynos_vma_get_stats(const ExynosVma* vma,
                           uint64_t* out_total_allocated,
                           uint64_t* out_total_suballocs,
                           uint64_t* out_saved_calls) {
    if (!vma) return;
    if (out_total_allocated) *out_total_allocated = vma->total_allocated;
    if (out_total_suballocs) *out_total_suballocs = vma->total_suballocs;
    if (out_saved_calls)     *out_saved_calls     = vma->saved_alloc_calls;
}

// ═══════════════════════════════════════════════════════════════════════════
// Destroy
// ═══════════════════════════════════════════════════════════════════════════

void exynos_vma_destroy(ExynosVma* vma) {
    if (!vma) return;

    pthread_mutex_lock(&vma->lock);

    for (uint32_t i = 0; i < vma->block_count; i++) {
        if (vma->blocks[i].memory != VK_NULL_HANDLE) {
            vkFreeMemory(vma->device, vma->blocks[i].memory, NULL);
            vma->blocks[i].memory = VK_NULL_HANDLE;
        }
    }

    VMA_LOGI("VMA destroyed: %llu total bytes allocated, %llu sub-allocs, %llu vkAllocateMemory calls saved",
             (unsigned long long)vma->total_allocated,
             (unsigned long long)vma->total_suballocs,
             (unsigned long long)vma->saved_alloc_calls);

    vma->block_count = 0;
    vma->enabled = 0;

    pthread_mutex_unlock(&vma->lock);
    pthread_mutex_destroy(&vma->lock);
}
