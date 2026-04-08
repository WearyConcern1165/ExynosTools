#pragma once
/// @file bc7_decoder.h
/// @brief BC7 compute shader fallback decoder.

#include "bcn_decoder/bcn_types.h"
#include "compute_pipeline.h"
#include "../vulkan/vk_buffer.h"
#include "../vulkan/vk_image.h"
#include "../memory/staging_buffer_pool.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace bcn {

/// Push constants for the BC7 decode shader.
struct Bc7PushConstants {
    uint32_t widthInBlocks;   ///< ceil(texelWidth / 4)
    uint32_t heightInBlocks;  ///< ceil(texelHeight / 4)
};

/// Orchestrates BC7 → R8G8B8A8_UNORM decode via compute shader.
class Bc7Decoder {
public:
    /// Initialise the decoder by creating the compute pipeline.
    BcnResult init(VkDevice            device,
                   VkPhysicalDevice    physDevice,
                   StagingBufferPool*  stagingPool,
                   const uint32_t*     spirvData,
                   size_t              spirvSize,
                   uint32_t            wgSizeX = 8,
                   uint32_t            wgSizeY = 8);

    void destroy();

    /// Decode a BC7 texture.
    /// @param compressedData   Raw BC7 block data.
    /// @param dataSize         Size of compressedData in bytes.
    /// @param texelWidth       Width in texels (not blocks).
    /// @param texelHeight      Height in texels (not blocks).
    /// @param queue            Compute-capable queue.
    /// @param cmdPool          Command pool for the queue.
    /// @param outImage [out]   Receives the decoded R8G8B8A8_UNORM image.
    BcnResult decode(const uint8_t*   compressedData,
                     uint64_t         dataSize,
                     uint32_t         texelWidth,
                     uint32_t         texelHeight,
                     VkQueue          queue,
                     VkCommandPool    cmdPool,
                     vk::ImageWrapper* outImage);

private:
    VkDevice         device_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    StagingBufferPool* stagingPool_ = nullptr;
    ComputePipeline  pipeline_;
    uint32_t         wgSizeX_    = 8;
    uint32_t         wgSizeY_    = 8;
};

} // namespace bcn
