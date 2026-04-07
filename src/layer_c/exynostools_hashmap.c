#include "exynostools_hashmap.h"

#include <stdlib.h>
#include <string.h>

static uint32_t exynos_hash_image(VkImage key, uint32_t bucket_count) {
    uintptr_t v = (uintptr_t)key;
    v ^= (v >> 33);
    v *= 0xff51afd7ed558ccdULL;
    v ^= (v >> 33);
    return (uint32_t)(v % bucket_count);
}

VkResult exynos_imap_init(ExynosImageMap* map, uint32_t bucket_count) {
    if (!map || bucket_count == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    memset(map, 0, sizeof(*map));
    map->bucket_count = bucket_count;
    map->buckets = (ExynosHashNode**)calloc(bucket_count, sizeof(ExynosHashNode*));
    if (!map->buckets) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (pthread_mutex_init(&map->mutex, NULL) != 0) {
        free(map->buckets);
        map->buckets = NULL;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

void exynos_imap_destroy(ExynosImageMap* map) {
    if (!map || !map->buckets) {
        return;
    }

    pthread_mutex_lock(&map->mutex);
    for (uint32_t i = 0; i < map->bucket_count; ++i) {
        ExynosHashNode* node = map->buckets[i];
        while (node) {
            ExynosHashNode* next = node->next;
            free(node);
            node = next;
        }
        map->buckets[i] = NULL;
    }
    free(map->buckets);
    map->buckets = NULL;
    map->bucket_count = 0;
    pthread_mutex_unlock(&map->mutex);
    pthread_mutex_destroy(&map->mutex);
}

VkResult exynos_imap_put(ExynosImageMap* map, VkImage key, const BCnImageInfo* value) {
    if (!map || !map->buckets || !value) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    pthread_mutex_lock(&map->mutex);
    uint32_t bucket = exynos_hash_image(key, map->bucket_count);
    ExynosHashNode* node = map->buckets[bucket];
    while (node) {
        if (node->key == key) {
            node->value = *value;
            pthread_mutex_unlock(&map->mutex);
            return VK_SUCCESS;
        }
        node = node->next;
    }

    ExynosHashNode* inserted = (ExynosHashNode*)calloc(1, sizeof(ExynosHashNode));
    if (!inserted) {
        pthread_mutex_unlock(&map->mutex);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    inserted->key = key;
    inserted->value = *value;
    inserted->next = map->buckets[bucket];
    map->buckets[bucket] = inserted;
    pthread_mutex_unlock(&map->mutex);
    return VK_SUCCESS;
}

int exynos_imap_get(ExynosImageMap* map, VkImage key, BCnImageInfo* out_value) {
    if (!map || !map->buckets) {
        return 0;
    }

    pthread_mutex_lock(&map->mutex);
    uint32_t bucket = exynos_hash_image(key, map->bucket_count);
    ExynosHashNode* node = map->buckets[bucket];
    while (node) {
        if (node->key == key) {
            if (out_value) {
                *out_value = node->value;
            }
            pthread_mutex_unlock(&map->mutex);
            return 1;
        }
        node = node->next;
    }
    pthread_mutex_unlock(&map->mutex);
    return 0;
}

void exynos_imap_remove(ExynosImageMap* map, VkImage key) {
    if (!map || !map->buckets) {
        return;
    }

    pthread_mutex_lock(&map->mutex);
    uint32_t bucket = exynos_hash_image(key, map->bucket_count);
    ExynosHashNode* prev = NULL;
    ExynosHashNode* node = map->buckets[bucket];
    while (node) {
        if (node->key == key) {
            if (prev) {
                prev->next = node->next;
            } else {
                map->buckets[bucket] = node->next;
            }
            free(node);
            break;
        }
        prev = node;
        node = node->next;
    }
    pthread_mutex_unlock(&map->mutex);
}
