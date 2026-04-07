#pragma once

#include <vulkan/vulkan.h>

#include "staging_pool.h"
#include "exynostools_hashmap.h"
#include "mipmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExynosBCnDispatch {
    VkDevice device;
    VkPhysicalDevice physical_device;
    ExynosStagingPool staging_pool;
    VkPipeline bc_pipeline[7];
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    int enabled;
} ExynosBCnDispatch;

VkResult exynos_bcn_dispatch_init(ExynosBCnDispatch* dispatch,
                                  VkDevice device,
                                  VkPhysicalDevice physical_device);

void exynos_bcn_dispatch_destroy(ExynosBCnDispatch* dispatch);

VkResult exynos_bcn_dispatch_decode(ExynosBCnDispatch* dispatch,
                                    VkCommandBuffer cmd,
                                    VkBuffer src_buffer,
                                    VkImage dst_image,
                                    VkImageLayout dst_layout,
                                    const VkBufferImageCopy* regions,
                                    uint32_t region_count,
                                    ExynosBCFormat format,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t mip_levels);

int exynos_bcn_dispatch_can_decode(const ExynosBCnDispatch* dispatch);

#ifdef __cplusplus
}
#endif
