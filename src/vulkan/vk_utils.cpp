#include "vk_utils.h"

#include <cassert>
#include <cstring>

namespace bcn {
namespace vk {

// ---------------------------------------------------------------------------
// findMemoryType
// ---------------------------------------------------------------------------
uint32_t findMemoryType(VkPhysicalDevice       physDevice,
                        uint32_t               typeFilter,
                        VkMemoryPropertyFlags  properties)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

// ---------------------------------------------------------------------------
// beginSingleTimeCommands
// ---------------------------------------------------------------------------
VkCommandBuffer beginSingleTimeCommands(VkDevice      device,
                                        VkCommandPool pool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    return cmdBuf;
}

// ---------------------------------------------------------------------------
// endSingleTimeCommands
// ---------------------------------------------------------------------------
void endSingleTimeCommands(VkDevice        device,
                           VkCommandPool   pool,
                           VkQueue         queue,
                           VkCommandBuffer cmdBuf)
{
    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmdBuf;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device, &fenceInfo, nullptr, &fence);

    vkQueueSubmit(queue, 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmdBuf);
}

// ---------------------------------------------------------------------------
// transitionImageLayout
// ---------------------------------------------------------------------------
void transitionImageSubresourceLayout(VkCommandBuffer cmdBuf,
                                      VkImage         image,
                                      VkFormat        /*format*/,
                                      VkImageLayout   oldLayout,
                                      VkImageLayout   newLayout,
                                      uint32_t        baseMipLevel,
                                      uint32_t        levelCount,
                                      uint32_t        baseArrayLayer,
                                      uint32_t        layerCount)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = baseMipLevel;
    barrier.subresourceRange.levelCount     = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount     = layerCount;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    // UNDEFINED → GENERAL  (pre-compute write)
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    // GENERAL → SHADER_READ_ONLY_OPTIMAL  (post-compute → fragment)
    else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    // UNDEFINED → TRANSFER_DST_OPTIMAL  (pre-upload native)
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    // TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL  (post-upload native)
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    // SHADER_READ_ONLY_OPTIMAL → TRANSFER_SRC_OPTIMAL  (readback for tests)
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    // GENERAL → TRANSFER_SRC_OPTIMAL  (readback from compute output)
    else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else {
        // Fallback: full pipeline drain – correct but slow.
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmdBuf,
                         srcStage, dstStage,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}

void transitionImageLayout(VkCommandBuffer cmdBuf,
                           VkImage         image,
                           VkFormat        format,
                           VkImageLayout   oldLayout,
                           VkImageLayout   newLayout,
                           uint32_t        mipLevels)
{
    transitionImageSubresourceLayout(cmdBuf,
                                     image,
                                     format,
                                     oldLayout,
                                     newLayout,
                                     0,
                                     mipLevels,
                                     0,
                                     1);
}

// ---------------------------------------------------------------------------
// pipelineBarrier (memory only)
// ---------------------------------------------------------------------------
void pipelineBarrier(VkCommandBuffer        cmdBuf,
                     VkPipelineStageFlags   srcStage,
                     VkPipelineStageFlags   dstStage,
                     VkAccessFlags          srcAccess,
                     VkAccessFlags          dstAccess)
{
    VkMemoryBarrier memBarrier{};
    memBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = srcAccess;
    memBarrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmdBuf,
                         srcStage, dstStage,
                         0,
                         1, &memBarrier,
                         0, nullptr,
                         0, nullptr);
}

} // namespace vk
} // namespace bcn
