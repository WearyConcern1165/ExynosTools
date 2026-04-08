#pragma once

#include <vulkan/vulkan.h>

/// Mip levels with width or height <= this threshold are batched into a single dispatch.
#define EXYNOS_MIP_BATCH_THRESHOLD 32u
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExynosBCFormat {
    EXYNOS_BC1 = 1,
    EXYNOS_BC2 = 2,
    EXYNOS_BC3 = 3,
    EXYNOS_BC4 = 4,
    EXYNOS_BC5 = 5,
    EXYNOS_BC6H = 6,
    EXYNOS_BC7 = 7
} ExynosBCFormat;

typedef struct MipLevelInfo {
    uint32_t width;
    uint32_t height;
    uint32_t blocks_x;
    uint32_t blocks_y;
    uint64_t compressed_offset;
    uint64_t decoded_offset;
    uint64_t compressed_size;
    uint64_t decoded_size;
    uint32_t mip_level;
} MipLevelInfo;

typedef struct MipChainInfo {
    MipLevelInfo* levels;
    uint32_t level_count;
    uint64_t total_compressed_size;
    uint64_t total_decoded_size;
} MipChainInfo;

typedef struct ExynosMipPushConstants {
    int format;
    int width;
    int height;
    int offset;
    int bufferRowLength;
    int offsetX;
    int offsetY;
} ExynosMipPushConstants;

uint32_t exynos_bc_block_bytes(ExynosBCFormat format);
VkFormat exynos_bc_output_format(ExynosBCFormat format);
int exynos_bc_to_vulkan_format(ExynosBCFormat format);
VkResult calculate_mip_chain(uint32_t base_width,
                             uint32_t base_height,
                             uint32_t mip_levels,
                             ExynosBCFormat format,
                             MipChainInfo* out_chain);

void destroy_mip_chain(MipChainInfo* chain);

VkResult decode_all_mips_multi_dispatch(VkCommandBuffer cmd,
                                        VkPipeline pipeline,
                                        VkPipelineLayout pipeline_layout,
                                        VkDescriptorSet descriptor_set,
                                        const MipChainInfo* chain,
                                        int format);

/// Optimized version: batches small mip levels (below threshold) into fewer dispatches.
/// Large mips get individual dispatches, small mips are grouped together.
/// Reduces GPU dispatch overhead from ~12 to ~4-5 for typical 4K textures.
VkResult decode_mips_batched(VkCommandBuffer cmd,
                             VkPipeline pipeline,
                             VkPipelineLayout pipeline_layout,
                             VkDescriptorSet descriptor_set,
                             const MipChainInfo* chain,
                             uint32_t batch_threshold,
                             int format);

#ifdef __cplusplus
}
#endif
