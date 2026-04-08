#include "native_upload.h"
#include "../core/format_support.h"
#include "../vulkan/vk_utils.h"
#include "bcn_decoder/bcn_config.h"

#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>

namespace bcn {

BcnResult NativeUploader::upload(BcnFormat          format,
                                  const uint8_t*     compressedData,
                                  uint64_t           dataSize,
                                  uint32_t           texelWidth,
                                  uint32_t           texelHeight,
                                  uint32_t           mipLevels,
                                  VkDevice           device,
                                  VkPhysicalDevice   physDevice,
                                  VkQueue            queue,
                                  VkCommandPool      cmdPool,
                                  StagingBufferPool* stagingPool,
                                  vk::ImageWrapper*  outImage)
{
    assert(compressedData && dataSize > 0);
    assert(texelWidth > 0 && texelHeight > 0);

    if (!stagingPool || !outImage || mipLevels == 0) {
        return BcnResult::ErrorInvalidFormat;
    }

    VkFormat vkFmt = bcnFormatToVkFormat(format);
    if (vkFmt == VK_FORMAT_UNDEFINED)
        return BcnResult::ErrorInvalidFormat;

    const uint32_t blockBytes = bcnBlockSizeBytes(format);
    if (blockBytes == 0) {
        return BcnResult::ErrorInvalidFormat;
    }

    VkDeviceSize expectedSize = 0;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        uint32_t mipWidth = std::max(1u, texelWidth >> mip);
        uint32_t mipHeight = std::max(1u, texelHeight >> mip);
        uint32_t blocksX = (mipWidth + config::kBlockWidth - 1) / config::kBlockWidth;
        uint32_t blocksY = (mipHeight + config::kBlockHeight - 1) / config::kBlockHeight;
        expectedSize += static_cast<VkDeviceSize>(blocksX) * static_cast<VkDeviceSize>(blocksY) * blockBytes;
    }
    if (expectedSize != static_cast<VkDeviceSize>(dataSize)) {
        return BcnResult::ErrorInvalidFormat;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingSize = 0;
    BcnResult stagingResult = stagingPool->upload(
        compressedData,
        static_cast<VkDeviceSize>(dataSize),
        &stagingBuffer,
        &stagingSize);
    if (stagingResult != BcnResult::Success) {
        return stagingResult;
    }

    VkResult res = outImage->create(
        device, physDevice,
        texelWidth, texelHeight,
        mipLevels,
        vkFmt,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (res != VK_SUCCESS)
        return BcnResult::ErrorOutOfMemory;

    res = outImage->createView();
    if (res != VK_SUCCESS) {
        outImage->destroy();
        return BcnResult::ErrorVulkanFailed;
    }

    // -- 3. Record transfer commands -----------------------------------------
    VkCommandBuffer cmd = vk::beginSingleTimeCommands(device, cmdPool);

    // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
    vk::transitionImageLayout(cmd, outImage->image(), vkFmt,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              mipLevels);

    std::vector<VkBufferImageCopy> regions;
    regions.reserve(mipLevels);
    VkDeviceSize offset = 0;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        uint32_t mipWidth = std::max(1u, texelWidth >> mip);
        uint32_t mipHeight = std::max(1u, texelHeight >> mip);
        uint32_t blocksX = (mipWidth + config::kBlockWidth - 1) / config::kBlockWidth;
        uint32_t blocksY = (mipHeight + config::kBlockHeight - 1) / config::kBlockHeight;
        VkDeviceSize mipSize = static_cast<VkDeviceSize>(blocksX) * static_cast<VkDeviceSize>(blocksY) * blockBytes;

        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { mipWidth, mipHeight, 1 };
        regions.push_back(region);
        offset += mipSize;
    }

    vkCmdCopyBufferToImage(cmd, stagingBuffer, outImage->image(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    // Transition: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
    vk::transitionImageLayout(cmd, outImage->image(), vkFmt,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              mipLevels);

    // -- 4. Submit and wait --------------------------------------------------
    vk::endSingleTimeCommands(device, cmdPool, queue, cmd);

    return BcnResult::Success;
}

} // namespace bcn
