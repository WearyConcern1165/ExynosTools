#include "bc7_decoder.h"
#include "../vulkan/vk_utils.h"
#include "bcn_decoder/bcn_config.h"

#include <cstring>
#include <cassert>

namespace bcn {

// ---------------------------------------------------------------------------
BcnResult Bc7Decoder::init(VkDevice         device,
                           VkPhysicalDevice physDevice,
                           StagingBufferPool* stagingPool,
                           const uint32_t*  spirvData,
                           size_t           spirvSize,
                           uint32_t         wgSizeX,
                           uint32_t         wgSizeY)
{
    device_     = device;
    physDevice_ = physDevice;
    stagingPool_ = stagingPool;
    wgSizeX_ = wgSizeX;
    wgSizeY_ = wgSizeY;

    VkResult res = pipeline_.create(device,
                                    spirvData,
                                    spirvSize,
                                    28, /* GranitePushConstants size */
                                    wgSizeX,
                                    wgSizeY);
    if (res != VK_SUCCESS)
        return BcnResult::ErrorPipelineCreationFailed;

    return BcnResult::Success;
}

// ---------------------------------------------------------------------------
void Bc7Decoder::destroy() {
    pipeline_.destroy();
    device_ = VK_NULL_HANDLE;
    stagingPool_ = nullptr;
}

// ---------------------------------------------------------------------------
BcnResult Bc7Decoder::decode(const uint8_t*    compressedData,
                              uint64_t          dataSize,
                              uint32_t          texelWidth,
                              uint32_t          texelHeight,
                              VkQueue           queue,
                              VkCommandPool     cmdPool,
                              vk::ImageWrapper* outImage)
{
    assert(compressedData && dataSize > 0 && stagingPool_);
    assert(texelWidth > 0 && texelHeight > 0);

    const uint32_t blocksX = (texelWidth  + 3) / 4;
    const uint32_t blocksY = (texelHeight + 3) / 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingSize = 0;
    BcnResult stagingResult = stagingPool_->upload(
        compressedData,
        static_cast<VkDeviceSize>(dataSize),
        &stagingBuffer,
        &stagingSize);
    if (stagingResult != BcnResult::Success) {
        return stagingResult;
    }

    // -- 2. Create output image (R8G8B8A8_UNORM, STORAGE | SAMPLED) ---------
    VkResult res = outImage->create(
        device_, physDevice_,
        texelWidth, texelHeight,
        1, // mipLevels
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (res != VK_SUCCESS)
        return BcnResult::ErrorOutOfMemory;

    res = outImage->createView();
    if (res != VK_SUCCESS) {
        outImage->destroy();
        return BcnResult::ErrorVulkanFailed;
    }

    // -- 3. Allocate and update descriptor set ------------------------------
    VkDescriptorSet ds = VK_NULL_HANDLE;
    res = pipeline_.allocateDescriptorSet(&ds);
    if (res != VK_SUCCESS) {
        outImage->destroy();
        return BcnResult::ErrorVulkanFailed;
    }

    pipeline_.updateDescriptorSet(ds,
                                  stagingBuffer,
                                  stagingSize,
                                  outImage->imageView());

    // -- 4. Record command buffer -------------------------------------------
    VkCommandBuffer cmd = vk::beginSingleTimeCommands(device_, cmdPool);

    // Transition output image: UNDEFINED → GENERAL
    vk::transitionImageLayout(cmd,
                              outImage->image(),
                              outImage->format(),
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_GENERAL);

    // Bind pipeline and descriptors
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.pipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_.pipelineLayout(),
                            0, 1, &ds, 0, nullptr);

    // Push constants
    struct GranitePushConstants {
        int format;
        int width;
        int height;
        int offset;
        int bufferRowLength;
        int offsetX;
        int offsetY;
    };

    GranitePushConstants pc = {};
    pc.format = 145; // VK_FORMAT_BC7_UNORM_BLOCK = 145 exactly as in s3tc.comp for BC7
    pc.width = texelWidth;
    pc.height = texelHeight;
    pc.offset = 0; // whole buffer bound
    pc.bufferRowLength = texelWidth; // contiguous
    pc.offsetX = 0;
    pc.offsetY = 0;

    vkCmdPushConstants(cmd, pipeline_.pipelineLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    // Dispatch: one workgroup per wgSize_ region of blocks
    const uint32_t groupsX = (blocksX + wgSizeX_ - 1) / wgSizeX_;
    const uint32_t groupsY = (blocksY + wgSizeY_ - 1) / wgSizeY_;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition output image: GENERAL → SHADER_READ_ONLY_OPTIMAL
    vk::transitionImageLayout(cmd,
                              outImage->image(),
                              outImage->format(),
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // -- 5. Submit and wait --------------------------------------------------
    vk::endSingleTimeCommands(device_, cmdPool, queue, cmd);

    return BcnResult::Success;
}

} // namespace bcn
