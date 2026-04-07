#include "mipmap.h"

#include <stdlib.h>
#include <string.h>

uint32_t exynos_bc_block_bytes(ExynosBCFormat format) {
    switch (format) {
        case EXYNOS_BC1: return 8;
        case EXYNOS_BC2: return 16;
        case EXYNOS_BC3: return 16;
        case EXYNOS_BC4: return 8;
        case EXYNOS_BC5: return 16;
        case EXYNOS_BC6H: return 16;
        case EXYNOS_BC7: return 16;
        default: return 0;
    }
}

VkFormat exynos_bc_output_format(ExynosBCFormat format) {
    switch (format) {
        case EXYNOS_BC4: return VK_FORMAT_R8_UNORM;
        case EXYNOS_BC5: return VK_FORMAT_R8G8_UNORM;
        case EXYNOS_BC6H: return VK_FORMAT_R16G16B16A16_SFLOAT;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

int exynos_bc_to_vulkan_format(ExynosBCFormat format) {
    switch (format) {
        case EXYNOS_BC1: return 131; // VK_FORMAT_BC1_RGB_UNORM_BLOCK
        case EXYNOS_BC2: return 137; // VK_FORMAT_BC2_UNORM_BLOCK
        case EXYNOS_BC3: return 137; // mapped to BC2
        case EXYNOS_BC4: return 139; // VK_FORMAT_BC4_UNORM_BLOCK
        case EXYNOS_BC5: return 143; // VK_FORMAT_BC5_UNORM_BLOCK
        case EXYNOS_BC6H: return 141; // VK_FORMAT_BC6H_UFLOAT_BLOCK
        case EXYNOS_BC7: return 145; // VK_FORMAT_BC7_UNORM_BLOCK
        default: return 145;
    }
}

VkResult calculate_mip_chain(uint32_t base_width,
                             uint32_t base_height,
                             uint32_t mip_levels,
                             ExynosBCFormat format,
                             MipChainInfo* out_chain) {
    if (!out_chain || base_width == 0 || base_height == 0 || mip_levels == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t block_bytes = exynos_bc_block_bytes(format);
    if (block_bytes == 0) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    memset(out_chain, 0, sizeof(*out_chain));
    out_chain->levels = (MipLevelInfo*)calloc(mip_levels, sizeof(MipLevelInfo));
    if (!out_chain->levels) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    out_chain->level_count = mip_levels;

    uint64_t compressed_cursor = 0;
    uint64_t decoded_cursor = 0;
    uint32_t width = base_width;
    uint32_t height = base_height;
    VkFormat out_fmt = exynos_bc_output_format(format);
    uint32_t pixel_size = (out_fmt == VK_FORMAT_R8_UNORM) ? 1 :
                          (out_fmt == VK_FORMAT_R8G8_UNORM) ? 2 :
                          (out_fmt == VK_FORMAT_R16G16B16A16_SFLOAT) ? 8 : 4;

    for (uint32_t i = 0; i < mip_levels; ++i) {
        MipLevelInfo* level = &out_chain->levels[i];
        uint32_t blocks_x = (width + 3u) / 4u;
        uint32_t blocks_y = (height + 3u) / 4u;
        uint64_t compressed_size = (uint64_t)blocks_x * (uint64_t)blocks_y * block_bytes;
        uint64_t decoded_size = (uint64_t)width * (uint64_t)height * pixel_size;

        level->width = width;
        level->height = height;
        level->blocks_x = blocks_x;
        level->blocks_y = blocks_y;
        level->compressed_offset = compressed_cursor;
        level->decoded_offset = decoded_cursor;
        level->compressed_size = compressed_size;
        level->decoded_size = decoded_size;
        level->mip_level = i;

        compressed_cursor += compressed_size;
        decoded_cursor += decoded_size;

        width = (width > 1u) ? (width >> 1u) : 1u;
        height = (height > 1u) ? (height >> 1u) : 1u;
    }

    out_chain->total_compressed_size = compressed_cursor;
    out_chain->total_decoded_size = decoded_cursor;
    return VK_SUCCESS;
}

void destroy_mip_chain(MipChainInfo* chain) {
    if (!chain) {
        return;
    }
    if (chain->levels) {
        free(chain->levels);
        chain->levels = NULL;
    }
    chain->level_count = 0;
    chain->total_compressed_size = 0;
    chain->total_decoded_size = 0;
}

VkResult decode_all_mips_multi_dispatch(VkCommandBuffer cmd,
                                        VkPipeline pipeline,
                                        VkPipelineLayout pipeline_layout,
                                        VkDescriptorSet descriptor_set,
                                        const MipChainInfo* chain,
                                        int format) {
    if (!cmd || pipeline == VK_NULL_HANDLE || pipeline_layout == VK_NULL_HANDLE || !chain || !chain->levels) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, NULL);

    for (uint32_t i = 0; i < chain->level_count; ++i) {
        const MipLevelInfo* level = &chain->levels[i];
        ExynosMipPushConstants pc;
        pc.format = format;
        pc.width = level->width;
        pc.height = level->height;
        pc.offset = (uint32_t)level->compressed_offset;
        pc.bufferRowLength = level->width;
        pc.offsetX = 0;
        pc.offsetY = 0;

        vkCmdPushConstants(cmd,
                           pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0,
                           sizeof(ExynosMipPushConstants),
                           &pc);

        uint32_t groups_x = (level->blocks_x + 7u) / 8u;
        uint32_t groups_y = (level->blocks_y + 7u) / 8u;
        vkCmdDispatch(cmd, groups_x, groups_y, 1u);

        VkMemoryBarrier barrier;
        memset(&barrier, 0, sizeof(barrier));
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1,
                             &barrier,
                             0,
                             NULL,
                             0,
                             NULL);
    }

    return VK_SUCCESS;
}

VkResult decode_mips_batched(VkCommandBuffer cmd,
                             VkPipeline pipeline,
                             VkPipelineLayout pipeline_layout,
                             VkDescriptorSet descriptor_set,
                             const MipChainInfo* chain,
                             uint32_t batch_threshold,
                             int format) {
    if (!cmd || pipeline == VK_NULL_HANDLE || pipeline_layout == VK_NULL_HANDLE || !chain || !chain->levels) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, NULL);

    /* Phase 1: Large mips — individual dispatch per mip level */
    uint32_t first_small = chain->level_count; /* index of first small mip */
    for (uint32_t i = 0; i < chain->level_count; ++i) {
        const MipLevelInfo* level = &chain->levels[i];
        if (level->width <= batch_threshold && level->height <= batch_threshold) {
            first_small = i;
            break;
        }

        // Phase 1 dispatch
        ExynosMipPushConstants pc;

        uint32_t groups_x = (level->blocks_x + 7u) / 8u;
        uint32_t groups_y = (level->blocks_y + 7u) / 8u;
        vkCmdDispatch(cmd, groups_x, groups_y, 1u);

        VkMemoryBarrier barrier;
        memset(&barrier, 0, sizeof(barrier));
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, NULL, 0, NULL);
    }

    /* Phase 2: Small mips — batch into a single dispatch based on total blocks */
    if (first_small < chain->level_count) {
        uint32_t total_blocks = 0;
        for (uint32_t i = first_small; i < chain->level_count; ++i) {
            total_blocks += chain->levels[i].blocks_x * chain->levels[i].blocks_y;
        }

        /* Use the first small mip's info for the push constants,
           the shader's mode=1 path handles mip table lookups */
        const MipLevelInfo* first = &chain->levels[first_small];
        ExynosMipPushConstants pc;
        pc.format = format;
        pc.width = first->width;
        pc.height = first->height;
        pc.offset = (uint32_t)first->compressed_offset;
        pc.bufferRowLength = first->width;
        pc.offsetX = 0;
        pc.offsetY = 0;

        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(ExynosMipPushConstants), &pc);

        /* Dispatch enough groups to cover all small mip blocks.
           One workgroup = 64 blocks, so groups = ceil(total_blocks / 64) */
        uint32_t groups = (total_blocks + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1u, 1u);

        VkMemoryBarrier barrier;
        memset(&barrier, 0, sizeof(barrier));
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 1, &barrier, 0, NULL, 0, NULL);
    }

    return VK_SUCCESS;
}
