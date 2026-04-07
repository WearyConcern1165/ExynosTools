#include "staging_buffer_pool.h"
#include "bcn_decoder/bcn_config.h"
#include <cstring>
#include <android/log.h>

#define POOL_TAG "ExynosToolsPool"
#define POOL_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  POOL_TAG, __VA_ARGS__)
#define POOL_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  POOL_TAG, __VA_ARGS__)

namespace bcn {

// ─── Slot creation/destruction ──────────────────────────────────────────

BcnResult StagingBufferPool::createSlot(VkDeviceSize size, bool isTemp, StagingSlot* slot) {
    *slot = {};
    slot->isTemp = isTemp;

    VkResult res = slot->buffer.create(
        device_, physDevice_, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (res != VK_SUCCESS) return BcnResult::ErrorOutOfMemory;

    void* mapped = nullptr;
    res = slot->buffer.map(&mapped);
    if (res != VK_SUCCESS) {
        slot->buffer.destroy();
        return BcnResult::ErrorVulkanFailed;
    }
    slot->mapped = mapped;
    slot->capacity = size;

    // Create fence (starts signaled so first acquire doesn't wait)
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    res = vkCreateFence(device_, &fenceInfo, nullptr, &slot->fence);
    if (res != VK_SUCCESS) {
        slot->buffer.unmap();
        slot->buffer.destroy();
        slot->mapped = nullptr;
        return BcnResult::ErrorVulkanFailed;
    }

    slot->inUse = false;
    return BcnResult::Success;
}

void StagingBufferPool::destroySlot(StagingSlot* slot) {
    if (slot->fence != VK_NULL_HANDLE) {
        vkDestroyFence(device_, slot->fence, nullptr);
        slot->fence = VK_NULL_HANDLE;
    }
    if (slot->mapped) {
        slot->buffer.unmap();
        slot->mapped = nullptr;
    }
    slot->buffer.destroy();
    slot->capacity = 0;
    slot->inUse = false;
}

// ─── Init / Shutdown ────────────────────────────────────────────────────

BcnResult StagingBufferPool::init(VkDevice device, VkPhysicalDevice physDevice) {
    return init(device, physDevice, StagingPoolConfig{});
}

BcnResult StagingBufferPool::init(VkDevice device, VkPhysicalDevice physDevice,
                                   const StagingPoolConfig& config) {
    device_ = device;
    physDevice_ = physDevice;
    config_ = config;

    ring_.resize(config_.ringSize);
    for (uint32_t i = 0; i < config_.ringSize; ++i) {
        BcnResult res = createSlot(config_.defaultSize, false, &ring_[i]);
        if (res != BcnResult::Success) {
            // Destroy what we've created so far
            for (uint32_t j = 0; j < i; ++j) destroySlot(&ring_[j]);
            ring_.clear();
            return res;
        }
    }

    POOL_LOGI("Ring pool initialized: %u slots x %llu bytes",
              config_.ringSize, (unsigned long long)config_.defaultSize);
    return BcnResult::Success;
}

void StagingBufferPool::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Destroy temp overflow buffers
    for (auto* temp : tempSlots_) {
        destroySlot(temp);
        delete temp;
    }
    tempSlots_.clear();

    // Destroy ring buffers
    for (auto& slot : ring_) {
        destroySlot(&slot);
    }
    ring_.clear();

    lastUploadSlot_ = nullptr;
    nextSlot_ = 0;
    device_ = VK_NULL_HANDLE;
    physDevice_ = VK_NULL_HANDLE;
}

// ─── Acquire / Release ──────────────────────────────────────────────────

BcnResult StagingBufferPool::acquire(VkDeviceSize size, StagingSlot** outSlot) {
    if (!outSlot || size == 0) return BcnResult::ErrorInvalidFormat;
    std::lock_guard<std::mutex> lock(mutex_);

    // If requested size exceeds ring buffer default, allocate a temp overflow buffer
    if (size > config_.defaultSize) {
        POOL_LOGW("Oversized request %llu > ring default %llu, allocating temp buffer",
                  (unsigned long long)size, (unsigned long long)config_.defaultSize);
        StagingSlot* temp = new StagingSlot();
        BcnResult res = createSlot(size, true, temp);
        if (res != BcnResult::Success) {
            delete temp;
            return res;
        }
        temp->inUse = true;
        tempSlots_.push_back(temp);
        *outSlot = temp;
        return BcnResult::Success;
    }

    // Try round-robin through the ring
    for (uint32_t attempt = 0; attempt < ring_.size(); ++attempt) {
        uint32_t idx = (nextSlot_ + attempt) % ring_.size();
        StagingSlot& slot = ring_[idx];

        if (slot.inUse) {
            // Check if GPU is done with this slot
            VkResult fenceStatus = vkGetFenceStatus(device_, slot.fence);
            if (fenceStatus == VK_NOT_READY) continue;  // still in use
            slot.inUse = false;  // GPU is done, reclaim it
        }

        // Found a free slot
        if (slot.capacity < size) {
            // Need to grow this slot
            destroySlot(&slot);
            BcnResult res = createSlot(size, false, &slot);
            if (res != BcnResult::Success) return res;
        }

        vkResetFences(device_, 1, &slot.fence);
        slot.inUse = true;
        nextSlot_ = (idx + 1) % ring_.size();
        *outSlot = &slot;
        return BcnResult::Success;
    }

    // All ring slots busy — wait for the current one
    StagingSlot& waitSlot = ring_[nextSlot_];
    // V2.0 fix: Use a bounded timeout instead of UINT64_MAX to prevent
    // indefinite GPU stalls that cause color corruption in RE Engine games
    vkWaitForFences(device_, 1, &waitSlot.fence, VK_TRUE, 100000000ULL); // 100ms
    waitSlot.inUse = false;

    if (waitSlot.capacity < size) {
        destroySlot(&waitSlot);
        BcnResult res = createSlot(size, false, &waitSlot);
        if (res != BcnResult::Success) return res;
    }

    vkResetFences(device_, 1, &waitSlot.fence);
    waitSlot.inUse = true;
    nextSlot_ = (nextSlot_ + 1) % ring_.size();
    *outSlot = &waitSlot;
    return BcnResult::Success;
}

void StagingBufferPool::release(StagingSlot* slot) {
    if (!slot) return;
    std::lock_guard<std::mutex> lock(mutex_);

    if (slot->isTemp) {
        // Find and remove from temp list, destroy it
        for (auto it = tempSlots_.begin(); it != tempSlots_.end(); ++it) {
            if (*it == slot) {
                destroySlot(slot);
                delete slot;
                tempSlots_.erase(it);
                return;
            }
        }
    } else {
        // Ring slot — just mark as not in use (will be reclaimed on next acquire)
        slot->inUse = false;
    }
}

// ─── Legacy API (backward compatible) ───────────────────────────────────

BcnResult StagingBufferPool::upload(const uint8_t* data, VkDeviceSize size,
                                     VkBuffer* outBuffer, VkDeviceSize* outSize) {
    if (!data || size == 0 || !outBuffer || !outSize)
        return BcnResult::ErrorInvalidFormat;

    // Auto-release previous upload slot
    if (lastUploadSlot_) {
        release(lastUploadSlot_);
        lastUploadSlot_ = nullptr;
    }

    StagingSlot* slot = nullptr;
    BcnResult res = acquire(size, &slot);
    if (res != BcnResult::Success) return res;

    std::memcpy(slot->mapped, data, static_cast<size_t>(size));
    *outBuffer = slot->buffer.buffer();
    *outSize = size;
    lastUploadSlot_ = slot;
    return BcnResult::Success;
}

uint32_t StagingBufferPool::activeSlotsCount() const {
    uint32_t count = 0;
    for (const auto& slot : ring_) {
        if (slot.inUse) ++count;
    }
    count += static_cast<uint32_t>(tempSlots_.size());
    return count;
}

} // namespace bcn
