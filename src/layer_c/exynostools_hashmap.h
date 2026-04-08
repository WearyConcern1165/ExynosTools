#pragma once

#include <vulkan/vulkan.h>
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BCnImageInfo {
    VkFormat original_format;
    VkFormat replacement_format;
    VkFormat storage_format;
    uint32_t width;
    uint32_t height;
    uint32_t mip_levels;
    uint32_t is_bcn;
} BCnImageInfo;

typedef struct ExynosHashNode {
    VkImage key;
    BCnImageInfo value;
    struct ExynosHashNode* next;
} ExynosHashNode;

typedef struct ExynosImageMap {
    ExynosHashNode** buckets;
    uint32_t bucket_count;
    pthread_rwlock_t lock;
} ExynosImageMap;

VkResult exynos_imap_init(ExynosImageMap* map, uint32_t bucket_count);
void exynos_imap_destroy(ExynosImageMap* map);
VkResult exynos_imap_put(ExynosImageMap* map, VkImage key, const BCnImageInfo* value);
int exynos_imap_get(ExynosImageMap* map, VkImage key, BCnImageInfo* out_value);
void exynos_imap_remove(ExynosImageMap* map, VkImage key);

#ifdef __cplusplus
}
#endif
