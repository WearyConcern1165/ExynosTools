#pragma once
/// @file vk_buffer.h
/// @brief RAII wrapper around VkBuffer + VkDeviceMemory.

#include <vulkan/vulkan.h>
#include <cstdint>

namespace bcn {
namespace vk {

/// Lightweight RAII wrapper that owns a VkBuffer and its backing memory.
class BufferWrapper {
public:
    BufferWrapper() = default;
    ~BufferWrapper();

    BufferWrapper(const BufferWrapper&) = delete;
    BufferWrapper& operator=(const BufferWrapper&) = delete;
    BufferWrapper(BufferWrapper&& other) noexcept;
    BufferWrapper& operator=(BufferWrapper&& other) noexcept;

    /// Create a buffer with the given size, usage, and memory properties.
    /// Returns VK_SUCCESS or an error VkResult.
    VkResult create(VkDevice             device,
                    VkPhysicalDevice     physDevice,
                    VkDeviceSize         size,
                    VkBufferUsageFlags   usage,
                    VkMemoryPropertyFlags memProps);

    /// Destroy the buffer and free memory.  Safe to call multiple times.
    void destroy();

    /// Map the buffer memory to CPU-accessible pointer.
    /// Only valid if created with HOST_VISIBLE.
    VkResult map(void** ppData);

    /// Unmap previously mapped memory.
    void unmap();

    // -- Accessors -----------------------------------------------------------
    VkBuffer       buffer()     const { return buffer_; }
    VkDeviceMemory memory()     const { return memory_; }
    VkDeviceSize   size()       const { return size_; }
    VkDevice       device()     const { return device_; }
    bool           valid()      const { return buffer_ != VK_NULL_HANDLE; }

private:
    VkDevice       device_ = VK_NULL_HANDLE;
    VkBuffer       buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize   size_   = 0;
};

} // namespace vk
} // namespace bcn
