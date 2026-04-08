#include "vk_buffer.h"
#include "vk_utils.h"

#include <cassert>
#include <cstring>
#include <utility>

namespace bcn {
namespace vk {

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
BufferWrapper::~BufferWrapper() {
    destroy();
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------
BufferWrapper::BufferWrapper(BufferWrapper&& other) noexcept
    : device_(other.device_)
    , buffer_(other.buffer_)
    , memory_(other.memory_)
    , size_(other.size_)
{
    other.device_ = VK_NULL_HANDLE;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.size_   = 0;
}

BufferWrapper& BufferWrapper::operator=(BufferWrapper&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_   = other.size_;
        other.device_ = VK_NULL_HANDLE;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_   = 0;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------
VkResult BufferWrapper::create(VkDevice              device,
                               VkPhysicalDevice      physDevice,
                               VkDeviceSize          size,
                               VkBufferUsageFlags    usage,
                               VkMemoryPropertyFlags memProps)
{
    assert(device != VK_NULL_HANDLE);
    device_ = device;
    size_   = size;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = size;
    bufInfo.usage       = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = vkCreateBuffer(device_, &bufInfo, nullptr, &buffer_);
    if (res != VK_SUCCESS) return res;

    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(device_, buffer_, &memReqs);

    uint32_t memTypeIdx = findMemoryType(physDevice,
                                         memReqs.memoryTypeBits,
                                         memProps);
    if (memTypeIdx == UINT32_MAX) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIdx;

    res = vkAllocateMemory(device_, &allocInfo, nullptr, &memory_);
    if (res != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return res;
    }

    res = vkBindBufferMemory(device_, buffer_, memory_, 0);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device_, memory_, nullptr);
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        return res;
    }

    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// destroy
// ---------------------------------------------------------------------------
void BufferWrapper::destroy() {
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    size_ = 0;
}

// ---------------------------------------------------------------------------
// map / unmap
// ---------------------------------------------------------------------------
VkResult BufferWrapper::map(void** ppData) {
    assert(buffer_ != VK_NULL_HANDLE);
    return vkMapMemory(device_, memory_, 0, size_, 0, ppData);
}

void BufferWrapper::unmap() {
    vkUnmapMemory(device_, memory_);
}

} // namespace vk
} // namespace bcn
