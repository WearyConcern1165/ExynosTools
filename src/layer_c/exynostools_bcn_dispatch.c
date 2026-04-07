#include "exynostools_bcn_dispatch.h"

#include <android/log.h>
#include <string.h>

#define EXYNOS_DISPATCH_TAG "ExynosToolsDispatch"
#define EXYNOS_DISPATCH_LOGI(...) __android_log_print(ANDROID_LOG_INFO, EXYNOS_DISPATCH_TAG, __VA_ARGS__)
#define EXYNOS_DISPATCH_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, EXYNOS_DISPATCH_TAG, __VA_ARGS__)

VkResult exynos_bcn_dispatch_init(ExynosBCnDispatch* dispatch,
                                  VkDevice device,
                                  VkPhysicalDevice physical_device) {
    if (!dispatch || device == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    memset(dispatch, 0, sizeof(*dispatch));
    dispatch->device = device;
    dispatch->physical_device = physical_device;

    ExynosStagingPoolConfig config;
    config.buffer_count = 4;
    config.default_buffer_size = 16u * 1024u * 1024u;
    VkResult pool_res = exynos_pool_init(&dispatch->staging_pool, device, physical_device, &config);
    if (pool_res != VK_SUCCESS) {
        EXYNOS_DISPATCH_LOGE("Failed to initialize staging pool: %d", pool_res);
        return pool_res;
    }

    dispatch->enabled = 1;
    EXYNOS_DISPATCH_LOGI("BCn dispatch initialized");
    return VK_SUCCESS;
}

int exynos_bcn_dispatch_can_decode(const ExynosBCnDispatch* dispatch) {
    if (!dispatch) {
        return 0;
    }
    if (!dispatch->enabled) {
        return 0;
    }
    if (dispatch->pipeline_layout == VK_NULL_HANDLE) {
        return 0;
    }
    if (dispatch->descriptor_set == VK_NULL_HANDLE) {
        return 0;
    }
    if (dispatch->bc_pipeline[0] == VK_NULL_HANDLE) {
        return 0;
    }
    return 1;
}

void exynos_bcn_dispatch_destroy(ExynosBCnDispatch* dispatch) {
    if (!dispatch) {
        return;
    }
    exynos_pool_destroy(&dispatch->staging_pool);
    if (dispatch->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dispatch->device, dispatch->descriptor_pool, NULL);
        dispatch->descriptor_pool = VK_NULL_HANDLE;
    }
    if (dispatch->descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dispatch->device, dispatch->descriptor_set_layout, NULL);
        dispatch->descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (dispatch->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dispatch->device, dispatch->pipeline_layout, NULL);
        dispatch->pipeline_layout = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 7; ++i) {
        if (dispatch->bc_pipeline[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(dispatch->device, dispatch->bc_pipeline[i], NULL);
            dispatch->bc_pipeline[i] = VK_NULL_HANDLE;
        }
    }
    dispatch->enabled = 0;
}

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
                                    uint32_t mip_levels) {
    if (!dispatch || !dispatch->enabled || cmd == VK_NULL_HANDLE || src_buffer == VK_NULL_HANDLE || dst_image == VK_NULL_HANDLE || !regions || region_count == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!exynos_bcn_dispatch_can_decode(dispatch)) {
        return VK_INCOMPLETE;
    }

    ExynosStagingBuffer* staging = NULL;
    VkResult acquire = exynos_pool_acquire(&dispatch->staging_pool, 4096, &staging);
    if (acquire != VK_SUCCESS) {
        return acquire;
    }

    MipChainInfo chain;
    VkResult chain_res = calculate_mip_chain(width, height, mip_levels, format, &chain);
    if (chain_res != VK_SUCCESS) {
        exynos_pool_release(&dispatch->staging_pool, staging);
        return chain_res;
    }

    VkResult copy_res = VK_SUCCESS;
    vkCmdCopyBufferToImage(cmd,
                           src_buffer,
                           dst_image,
                           dst_layout,
                           region_count,
                           regions);

    copy_res = decode_all_mips_multi_dispatch(cmd,
                                              dispatch->bc_pipeline[0],
                                              dispatch->pipeline_layout,
                                              dispatch->descriptor_set,
                                              &chain,
                                              exynos_bc_to_vulkan_format(format));

    destroy_mip_chain(&chain);
    exynos_pool_release(&dispatch->staging_pool, staging);
    return copy_res;
}
