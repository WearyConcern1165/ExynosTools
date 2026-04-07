#pragma once
/// @file vk_image.h
/// @brief RAII wrapper around VkImage + VkDeviceMemory + VkImageView.

#include <vulkan/vulkan.h>
#include <cstdint>

namespace bcn {
namespace vk {

/// Lightweight RAII wrapper that owns a VkImage, its backing memory, and
/// an optional VkImageView.
class ImageWrapper {
public:
    ImageWrapper() = default;
    ~ImageWrapper();

    ImageWrapper(const ImageWrapper&) = delete;
    ImageWrapper& operator=(const ImageWrapper&) = delete;
    ImageWrapper(ImageWrapper&& other) noexcept;
    ImageWrapper& operator=(ImageWrapper&& other) noexcept;

    /// Create a 2D image.
    VkResult create(VkDevice            device,
                    VkPhysicalDevice    physDevice,
                    uint32_t            width,
                    uint32_t            height,
                    uint32_t            mipLevels,
                    VkFormat            format,
                    VkImageTiling       tiling,
                    VkImageUsageFlags   usage,
                    VkMemoryPropertyFlags memProps);

    /// Create a VkImageView for the image (must be called after create()).
    VkResult createView(VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    /// Destroy image, memory, and view.
    void destroy();

    // -- Accessors -----------------------------------------------------------
    VkImage        image()       const { return image_; }
    VkImageView    imageView()   const { return view_; }
    VkDeviceMemory memory()      const { return memory_; }
    VkFormat       format()      const { return format_; }
    uint32_t       width()       const { return width_; }
    uint32_t       height()      const { return height_; }
    uint32_t       mipLevels()   const { return mipLevels_; }
    VkDevice       device()      const { return device_; }
    bool           valid()       const { return image_ != VK_NULL_HANDLE; }

    /// Returns the DEVICE_LOCAL memory size bound to the image.
    VkDeviceSize   memorySize()  const { return memSize_; }

private:
    VkDevice       device_    = VK_NULL_HANDLE;
    VkImage        image_     = VK_NULL_HANDLE;
    VkDeviceMemory memory_    = VK_NULL_HANDLE;
    VkImageView    view_      = VK_NULL_HANDLE;
    VkFormat       format_    = VK_FORMAT_UNDEFINED;
    uint32_t       width_     = 0;
    uint32_t       height_    = 0;
    uint32_t       mipLevels_ = 1;
    VkDeviceSize   memSize_   = 0;
};

} // namespace vk
} // namespace bcn
