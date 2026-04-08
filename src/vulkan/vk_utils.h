#pragma once
/// @file vk_utils.h
/// @brief Low-level Vulkan helpers: memory type selection, barriers, single-
///        time command buffers.

#include <vulkan/vulkan.h>
#include <cstdint>

namespace bcn {
namespace vk {

/// Find a memory type index that satisfies both the type bits reported by
/// vkGetBufferMemoryRequirements / vkGetImageMemoryRequirements AND the
/// requested property flags.
/// Returns UINT32_MAX on failure.
uint32_t findMemoryType(VkPhysicalDevice physDevice,
                        uint32_t         typeFilter,
                        VkMemoryPropertyFlags properties);

/// Allocate, begin, and return a single-use command buffer from @p pool.
VkCommandBuffer beginSingleTimeCommands(VkDevice device,
                                        VkCommandPool pool);

/// End, submit the command buffer on @p queue, and block until completion.
/// The command buffer is freed back into @p pool after execution.
void endSingleTimeCommands(VkDevice        device,
                           VkCommandPool   pool,
                           VkQueue         queue,
                           VkCommandBuffer cmdBuf);

/// Record a full-image layout transition barrier.
void transitionImageLayout(VkCommandBuffer cmdBuf,
                           VkImage         image,
                           VkFormat        format,
                           VkImageLayout   oldLayout,
                           VkImageLayout   newLayout,
                           uint32_t        mipLevels = 1);

/// Record a layout transition for an explicit subresource range.
void transitionImageSubresourceLayout(VkCommandBuffer cmdBuf,
                                      VkImage         image,
                                      VkFormat        format,
                                      VkImageLayout   oldLayout,
                                      VkImageLayout   newLayout,
                                      uint32_t        baseMipLevel,
                                      uint32_t        levelCount,
                                      uint32_t        baseArrayLayer,
                                      uint32_t        layerCount);

/// Insert a pipeline memory barrier (no image/buffer).
void pipelineBarrier(VkCommandBuffer        cmdBuf,
                     VkPipelineStageFlags   srcStage,
                     VkPipelineStageFlags   dstStage,
                     VkAccessFlags          srcAccess,
                     VkAccessFlags          dstAccess);

} // namespace vk
} // namespace bcn
