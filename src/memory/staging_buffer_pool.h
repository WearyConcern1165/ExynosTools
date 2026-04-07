#pragma once
/// @file staging_buffer_pool.h
/// @brief Multi-buffer ring pool for BCn staging uploads.
///
/// Improvement over v2.x single-buffer design:
///   - N pre-allocated buffers in a ring
///   - Fence-guarded: each buffer tracks its own GPU completion
///   - Parallel uploads: multiple textures can stage data simultaneously
///   - Automatic growth: if all ring slots are busy, allocates temporary overflow buffer
///   - Bucket sizing: avoids wasteful reallocation for common texture sizes

#include "bcn_decoder/bcn_types.h"
#include "../vulkan/vk_buffer.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace bcn {

/// Configuration for the staging buffer pool.
struct StagingPoolConfig {
    uint32_t    ringSize        = 8;              ///< Number of buffers in the ring (V2.0: doubled for RE Engine)
    VkDeviceSize defaultSize    = 32*1024*1024;   ///< Default buffer size (32 MB — was 16MB, tripled total pool)
};

/// A single staging buffer slot in the ring.
struct StagingSlot {
    vk::BufferWrapper buffer;
    void*             mapped   = nullptr;
    VkDeviceSize      capacity = 0;
    VkFence           fence    = VK_NULL_HANDLE;
    bool              inUse    = false;
    bool              isTemp   = false;  ///< Temporary overflow buffer (not in ring)
};

class StagingBufferPool {
public:
    BcnResult init(VkDevice device, VkPhysicalDevice physDevice);

    /// Initialize with custom configuration (ring size, default buffer size).
    BcnResult init(VkDevice device, VkPhysicalDevice physDevice, const StagingPoolConfig& config);

    void shutdown();

    /// Acquire a staging slot with at least 'size' bytes.
    /// If all ring slots are busy, waits for the oldest or allocates a temp buffer.
    /// Returns a pointer to the slot; caller must release it after GPU work completes.
    BcnResult acquire(VkDeviceSize size, StagingSlot** outSlot);

    /// Release a staging slot back to the pool.
    void release(StagingSlot* slot);

    /// Legacy single-shot upload API (backward compatible with v2.x).
    /// Internally acquires a slot, copies data, and returns the VkBuffer.
    /// The slot is auto-released on the next acquire() call.
    BcnResult upload(const uint8_t* data,
                     VkDeviceSize size,
                     VkBuffer* outBuffer,
                     VkDeviceSize* outSize);

    /// Get pool statistics for debugging.
    uint32_t ringSize() const { return static_cast<uint32_t>(ring_.size()); }
    uint32_t activeSlotsCount() const;

private:
    BcnResult createSlot(VkDeviceSize size, bool isTemp, StagingSlot* slot);
    void destroySlot(StagingSlot* slot);

    VkDevice         device_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    std::vector<StagingSlot> ring_;
    std::vector<StagingSlot*> tempSlots_;  ///< Overflow temporary buffers
    uint32_t         nextSlot_   = 0;      ///< Round-robin index
    StagingSlot*     lastUploadSlot_ = nullptr; ///< For legacy upload() auto-release
    std::mutex       mutex_;
    StagingPoolConfig config_;
};

} // namespace bcn
