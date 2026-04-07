#pragma once

#include <vulkan/vulkan.h>
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExynosStagingBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mapped;
    VkDeviceSize size;
    VkFence fence;
    int in_use;
    int is_temporary;
} ExynosStagingBuffer;

typedef struct ExynosStagingPoolConfig {
    uint32_t buffer_count;
    VkDeviceSize default_buffer_size;
} ExynosStagingPoolConfig;

typedef struct ExynosStagingPool {
    VkDevice device;
    VkPhysicalDevice physical_device;
    ExynosStagingBuffer* buffers;
    uint32_t buffer_count;
    VkDeviceSize default_buffer_size;
    pthread_mutex_t mutex;
} ExynosStagingPool;

VkResult exynos_pool_init(ExynosStagingPool* pool,
                          VkDevice device,
                          VkPhysicalDevice physical_device,
                          const ExynosStagingPoolConfig* config);

VkResult exynos_pool_acquire(ExynosStagingPool* pool,
                             VkDeviceSize min_size,
                             ExynosStagingBuffer** out_buffer);

void exynos_pool_release(ExynosStagingPool* pool,
                         ExynosStagingBuffer* buffer);

void exynos_pool_destroy(ExynosStagingPool* pool);

#ifdef __cplusplus
}
#endif
