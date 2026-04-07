#include "staging_pool.h"

#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define EXYNOS_POOL_TAG "ExynosToolsPool"
#define EXYNOS_POOL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, EXYNOS_POOL_TAG, __VA_ARGS__)
#define EXYNOS_POOL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, EXYNOS_POOL_TAG, __VA_ARGS__)

static uint32_t exynos_pool_find_memory_type(VkPhysicalDevice physical_device,
                                             uint32_t type_filter,
                                             VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static VkResult exynos_pool_create_buffer(ExynosStagingPool* pool,
                                          VkDeviceSize size,
                                          int is_temporary,
                                          ExynosStagingBuffer* out_buffer) {
    memset(out_buffer, 0, sizeof(*out_buffer));
    out_buffer->size = size;
    out_buffer->is_temporary = is_temporary;

    VkBufferCreateInfo buffer_info;
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = vkCreateBuffer(pool->device, &buffer_info, NULL, &out_buffer->buffer);
    if (res != VK_SUCCESS) {
        EXYNOS_POOL_LOGE("vkCreateBuffer failed: %d", res);
        return res;
    }

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(pool->device, out_buffer->buffer, &reqs);

    uint32_t memory_type = exynos_pool_find_memory_type(
        pool->physical_device,
        reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        vkDestroyBuffer(pool->device, out_buffer->buffer, NULL);
        out_buffer->buffer = VK_NULL_HANDLE;
        EXYNOS_POOL_LOGE("No HOST_VISIBLE|HOST_COHERENT memory type found");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkMemoryAllocateInfo alloc_info;
    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = reqs.size;
    alloc_info.memoryTypeIndex = memory_type;

    res = vkAllocateMemory(pool->device, &alloc_info, NULL, &out_buffer->memory);
    if (res != VK_SUCCESS) {
        vkDestroyBuffer(pool->device, out_buffer->buffer, NULL);
        out_buffer->buffer = VK_NULL_HANDLE;
        EXYNOS_POOL_LOGE("vkAllocateMemory failed: %d", res);
        return res;
    }

    res = vkBindBufferMemory(pool->device, out_buffer->buffer, out_buffer->memory, 0);
    if (res != VK_SUCCESS) {
        vkFreeMemory(pool->device, out_buffer->memory, NULL);
        vkDestroyBuffer(pool->device, out_buffer->buffer, NULL);
        out_buffer->memory = VK_NULL_HANDLE;
        out_buffer->buffer = VK_NULL_HANDLE;
        EXYNOS_POOL_LOGE("vkBindBufferMemory failed: %d", res);
        return res;
    }

    res = vkMapMemory(pool->device, out_buffer->memory, 0, reqs.size, 0, &out_buffer->mapped);
    if (res != VK_SUCCESS) {
        vkFreeMemory(pool->device, out_buffer->memory, NULL);
        vkDestroyBuffer(pool->device, out_buffer->buffer, NULL);
        out_buffer->memory = VK_NULL_HANDLE;
        out_buffer->buffer = VK_NULL_HANDLE;
        EXYNOS_POOL_LOGE("vkMapMemory failed: %d", res);
        return res;
    }

    VkFenceCreateInfo fence_info;
    memset(&fence_info, 0, sizeof(fence_info));
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    res = vkCreateFence(pool->device, &fence_info, NULL, &out_buffer->fence);
    if (res != VK_SUCCESS) {
        vkUnmapMemory(pool->device, out_buffer->memory);
        vkFreeMemory(pool->device, out_buffer->memory, NULL);
        vkDestroyBuffer(pool->device, out_buffer->buffer, NULL);
        out_buffer->mapped = NULL;
        out_buffer->memory = VK_NULL_HANDLE;
        out_buffer->buffer = VK_NULL_HANDLE;
        EXYNOS_POOL_LOGE("vkCreateFence failed: %d", res);
        return res;
    }

    out_buffer->in_use = 0;
    EXYNOS_POOL_LOGI("Buffer pool created size=%llu temp=%d", (unsigned long long)size, is_temporary);
    return VK_SUCCESS;
}

static void exynos_pool_destroy_buffer(ExynosStagingPool* pool, ExynosStagingBuffer* buffer) {
    if (buffer->fence != VK_NULL_HANDLE) {
        vkDestroyFence(pool->device, buffer->fence, NULL);
        buffer->fence = VK_NULL_HANDLE;
    }
    if (buffer->mapped) {
        vkUnmapMemory(pool->device, buffer->memory);
        buffer->mapped = NULL;
    }
    if (buffer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(pool->device, buffer->memory, NULL);
        buffer->memory = VK_NULL_HANDLE;
    }
    if (buffer->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(pool->device, buffer->buffer, NULL);
        buffer->buffer = VK_NULL_HANDLE;
    }
    buffer->size = 0;
    buffer->in_use = 0;
    buffer->is_temporary = 0;
}

VkResult exynos_pool_init(ExynosStagingPool* pool,
                          VkDevice device,
                          VkPhysicalDevice physical_device,
                          const ExynosStagingPoolConfig* config) {
    if (!pool || device == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    memset(pool, 0, sizeof(*pool));
    pool->device = device;
    pool->physical_device = physical_device;
    pool->buffer_count = (config && config->buffer_count) ? config->buffer_count : 8u;
    pool->default_buffer_size = (config && config->default_buffer_size) ? config->default_buffer_size : (32u * 1024u * 1024u);

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    pool->buffers = (ExynosStagingBuffer*)calloc(pool->buffer_count, sizeof(ExynosStagingBuffer));
    if (!pool->buffers) {
        pthread_mutex_destroy(&pool->mutex);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    for (uint32_t i = 0; i < pool->buffer_count; ++i) {
        VkResult res = exynos_pool_create_buffer(pool, pool->default_buffer_size, 0, &pool->buffers[i]);
        if (res != VK_SUCCESS) {
            for (uint32_t j = 0; j < i; ++j) {
                exynos_pool_destroy_buffer(pool, &pool->buffers[j]);
            }
            free(pool->buffers);
            pool->buffers = NULL;
            pthread_mutex_destroy(&pool->mutex);
            return res;
        }
    }

    EXYNOS_POOL_LOGI("Pool initialized buffers=%u size=%llu",
                     pool->buffer_count,
                     (unsigned long long)pool->default_buffer_size);
    return VK_SUCCESS;
}

VkResult exynos_pool_acquire(ExynosStagingPool* pool,
                             VkDeviceSize min_size,
                             ExynosStagingBuffer** out_buffer) {
    if (!pool || !out_buffer || min_size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *out_buffer = NULL;
    pthread_mutex_lock(&pool->mutex);

    if (min_size > pool->default_buffer_size) {
        ExynosStagingBuffer* temp = (ExynosStagingBuffer*)calloc(1, sizeof(ExynosStagingBuffer));
        if (!temp) {
            pthread_mutex_unlock(&pool->mutex);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        VkResult temp_res = exynos_pool_create_buffer(pool, min_size, 1, temp);
        if (temp_res != VK_SUCCESS) {
            free(temp);
            pthread_mutex_unlock(&pool->mutex);
            return temp_res;
        }
        temp->in_use = 1;
        *out_buffer = temp;
        pthread_mutex_unlock(&pool->mutex);
        return VK_SUCCESS;
    }

    for (;;) {
        for (uint32_t i = 0; i < pool->buffer_count; ++i) {
            ExynosStagingBuffer* buf = &pool->buffers[i];
            if (buf->in_use) {
                continue;
            }
            VkResult fence_status = vkGetFenceStatus(pool->device, buf->fence);
            if (fence_status == VK_NOT_READY) {
                continue;
            }
            if (fence_status != VK_SUCCESS) {
                pthread_mutex_unlock(&pool->mutex);
                return fence_status;
            }
            vkResetFences(pool->device, 1, &buf->fence);
            buf->in_use = 1;
            *out_buffer = buf;
            pthread_mutex_unlock(&pool->mutex);
            return VK_SUCCESS;
        }

        // All slots busy — wait on the OLDEST fence only (not the entire GPU!)
        // This prevents the catastrophic full-GPU stall that causes color
        // corruption and stuttering in texture-heavy games (RE Engine, etc.)
        ExynosStagingBuffer* oldest = &pool->buffers[0];
        for (uint32_t i = 1; i < pool->buffer_count; i++) {
            if (pool->buffers[i].in_use) {
                oldest = &pool->buffers[i];
                break;
            }
        }
        pthread_mutex_unlock(&pool->mutex);
        VkResult wait_res = vkWaitForFences(pool->device, 1, &oldest->fence,
                                            VK_TRUE, 100000000ULL); // 100ms timeout
        if (wait_res != VK_SUCCESS && wait_res != VK_TIMEOUT) {
            return wait_res;
        }
        pthread_mutex_lock(&pool->mutex);
        // Mark the waited slot as available
        oldest->in_use = 0;
    }
}

void exynos_pool_release(ExynosStagingPool* pool, ExynosStagingBuffer* buffer) {
    if (!pool || !buffer) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);
    if (buffer->is_temporary) {
        exynos_pool_destroy_buffer(pool, buffer);
        free(buffer);
    } else {
        buffer->in_use = 0;
    }
    pthread_mutex_unlock(&pool->mutex);
}

void exynos_pool_destroy(ExynosStagingPool* pool) {
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);
    if (pool->buffers) {
        for (uint32_t i = 0; i < pool->buffer_count; ++i) {
            exynos_pool_destroy_buffer(pool, &pool->buffers[i]);
        }
        free(pool->buffers);
        pool->buffers = NULL;
    }
    pool->buffer_count = 0;
    pool->default_buffer_size = 0;
    pool->device = VK_NULL_HANDLE;
    pool->physical_device = VK_NULL_HANDLE;
    pthread_mutex_unlock(&pool->mutex);
    pthread_mutex_destroy(&pool->mutex);
}
