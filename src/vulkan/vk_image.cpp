#include "vk_image.h"
#include "vk_utils.h"

#include <cassert>
#include <utility>

namespace bcn {
namespace vk {

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
ImageWrapper::~ImageWrapper() {
    destroy();
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------
ImageWrapper::ImageWrapper(ImageWrapper&& o) noexcept
    : device_(o.device_), image_(o.image_), memory_(o.memory_),
      view_(o.view_), format_(o.format_), width_(o.width_),
      height_(o.height_), mipLevels_(o.mipLevels_), memSize_(o.memSize_)
{
    o.device_ = VK_NULL_HANDLE;
    o.image_  = VK_NULL_HANDLE;
    o.memory_ = VK_NULL_HANDLE;
    o.view_   = VK_NULL_HANDLE;
    o.width_  = o.height_ = 0;
    o.memSize_ = 0;
}

ImageWrapper& ImageWrapper::operator=(ImageWrapper&& o) noexcept {
    if (this != &o) {
        destroy();
        device_    = o.device_;
        image_     = o.image_;
        memory_    = o.memory_;
        view_      = o.view_;
        format_    = o.format_;
        width_     = o.width_;
        height_    = o.height_;
        mipLevels_ = o.mipLevels_;
        memSize_   = o.memSize_;
        o.device_ = VK_NULL_HANDLE;
        o.image_  = VK_NULL_HANDLE;
        o.memory_ = VK_NULL_HANDLE;
        o.view_   = VK_NULL_HANDLE;
        o.width_  = o.height_ = 0;
        o.memSize_ = 0;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------
VkResult ImageWrapper::create(VkDevice             device,
                              VkPhysicalDevice     physDevice,
                              uint32_t             width,
                              uint32_t             height,
                              uint32_t             mipLevels,
                              VkFormat             format,
                              VkImageTiling        tiling,
                              VkImageUsageFlags    usage,
                              VkMemoryPropertyFlags memProps)
{
    assert(device != VK_NULL_HANDLE);
    device_    = device;
    width_     = width;
    height_    = height;
    mipLevels_ = mipLevels;
    format_    = format;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = format;
    imgInfo.extent        = { width, height, 1 };
    imgInfo.mipLevels     = mipLevels;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = tiling;
    imgInfo.usage         = usage;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = vkCreateImage(device_, &imgInfo, nullptr, &image_);
    if (res != VK_SUCCESS) return res;

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device_, image_, &memReqs);

    uint32_t memIdx = findMemoryType(physDevice,
                                     memReqs.memoryTypeBits,
                                     memProps);
    if (memIdx == UINT32_MAX) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = memIdx;

    res = vkAllocateMemory(device_, &allocInfo, nullptr, &memory_);
    if (res != VK_SUCCESS) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
        return res;
    }

    memSize_ = memReqs.size;

    res = vkBindImageMemory(device_, image_, memory_, 0);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device_, memory_, nullptr);
        vkDestroyImage(device_, image_, nullptr);
        image_  = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        memSize_ = 0;
        return res;
    }

    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// createView
// ---------------------------------------------------------------------------
VkResult ImageWrapper::createView(VkImageAspectFlags aspectMask) {
    assert(image_ != VK_NULL_HANDLE);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = format_;
    viewInfo.subresourceRange.aspectMask     = aspectMask;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    return vkCreateImageView(device_, &viewInfo, nullptr, &view_);
}

// ---------------------------------------------------------------------------
// destroy
// ---------------------------------------------------------------------------
void ImageWrapper::destroy() {
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    memSize_ = 0;
}

} // namespace vk
} // namespace bcn
