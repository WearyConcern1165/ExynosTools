#pragma once
/// @file native_upload.h
/// @brief Native (hardware-supported) texture upload path.

#include "bcn_decoder/bcn_types.h"
#include "../vulkan/vk_buffer.h"
#include "../vulkan/vk_image.h"
#include "../memory/staging_buffer_pool.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace bcn {

/// Uploads BC-compressed data directly to a VkImage when the format is
/// natively supported by the hardware.
class NativeUploader {
public:
    /// Upload BC data to a natively-supported compressed VkImage.
    /// @param format         The BC format (must be natively supported).
    /// @param compressedData Raw BC block data from the application.
    /// @param dataSize       Size of compressedData in bytes.
    /// @param texelWidth     Width in texels.
    /// @param texelHeight    Height in texels.
    /// @param mipLevels      Number of mip levels (currently only 1 supported).
    /// @param device         Vulkan device.
    /// @param physDevice     Physical device (for memory type selection).
    /// @param queue          Transfer-capable queue.
    /// @param cmdPool        Command pool for the queue.
    /// @param outImage [out] Receives the uploaded compressed VkImage.
    static BcnResult upload(BcnFormat          format,
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
                            vk::ImageWrapper*  outImage);
};

} // namespace bcn
