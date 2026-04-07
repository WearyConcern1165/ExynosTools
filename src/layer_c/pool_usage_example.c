#include "staging_pool.h"

#include <string.h>

VkResult exynos_decode_upload_example(ExynosStagingPool* pool,
                                      const uint8_t* compressed_data,
                                      VkDeviceSize data_size,
                                      VkQueue queue,
                                      VkCommandBuffer cmd,
                                      VkFence submit_fence) {
    ExynosStagingBuffer* staging = NULL;
    VkResult res = exynos_pool_acquire(pool, data_size, &staging);
    if (res != VK_SUCCESS) {
        return res;
    }

    memcpy(staging->mapped, compressed_data, (size_t)data_size);

    VkMappedMemoryRange range;
    memset(&range, 0, sizeof(range));
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = staging->memory;
    range.offset = 0;
    range.size = data_size;
    vkFlushMappedMemoryRanges(pool->device, 1, &range);

    vkResetFences(pool->device, 1, &staging->fence);
    vkQueueSubmit(queue, 0, NULL, staging->fence);
    vkWaitForFences(pool->device, 1, &staging->fence, VK_TRUE, UINT64_MAX);

    exynos_pool_release(pool, staging);
    return VK_SUCCESS;
}
