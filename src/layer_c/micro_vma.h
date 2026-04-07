#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V1.6.0 — Micro-VMA (Vulkan Memory Allocator)
// Sub-allocates from large pre-allocated blocks to prevent Android OOM
// caused by memory fragmentation in long emulation sessions.
// ═══════════════════════════════════════════════════════════════════════════

#define EXYNOS_VMA_MAX_BLOCKS 32
#define EXYNOS_VMA_MAX_SUBALLOCS 4096

typedef struct ExynosVmaSubAlloc {
    VkDeviceSize offset;
    VkDeviceSize size;
    int          in_use;
} ExynosVmaSubAlloc;

typedef struct ExynosVmaBlock {
    VkDeviceMemory  memory;
    VkDeviceSize    block_size;
    uint32_t        memory_type_index;
    ExynosVmaSubAlloc suballocs[EXYNOS_VMA_MAX_SUBALLOCS];
    uint32_t        suballoc_count;
    VkDeviceSize    used_bytes;
} ExynosVmaBlock;

typedef struct ExynosVma {
    VkDevice            device;
    VkPhysicalDevice    physical_device;
    VkDeviceSize        block_size;         // Size of each mega-block (e.g. 128MB)
    ExynosVmaBlock      blocks[EXYNOS_VMA_MAX_BLOCKS];
    uint32_t            block_count;
    pthread_mutex_t     lock;
    int                 enabled;

    // Stats
    uint64_t            total_allocated;
    uint64_t            total_suballocs;
    uint64_t            saved_alloc_calls;  // How many vkAllocateMemory calls we avoided
} ExynosVma;

/// Initialize the micro-VMA.
VkResult exynos_vma_init(ExynosVma* vma,
                          VkDevice device,
                          VkPhysicalDevice physical_device,
                          uint32_t block_size_mb);

/// Allocate memory through the VMA (sub-allocates from existing blocks).
/// Returns the block's VkDeviceMemory and the offset within it.
VkResult exynos_vma_alloc(ExynosVma* vma,
                           const VkMemoryRequirements* reqs,
                           uint32_t memory_type_index,
                           VkDeviceMemory* out_memory,
                           VkDeviceSize* out_offset);

/// Free a sub-allocation.
void exynos_vma_free(ExynosVma* vma,
                      VkDeviceMemory memory,
                      VkDeviceSize offset);

/// Get stats.
void exynos_vma_get_stats(const ExynosVma* vma,
                           uint64_t* out_total_allocated,
                           uint64_t* out_total_suballocs,
                           uint64_t* out_saved_calls);

/// Destroy all blocks and free GPU memory.
void exynos_vma_destroy(ExynosVma* vma);

#ifdef __cplusplus
}
#endif
