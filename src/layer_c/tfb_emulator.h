#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V2.0 — Transform Feedback Software Emulator
//
// Provides real implementations for the VK_EXT_transform_feedback commands
// that Exynos GPUs physically lack. Instead of just spoofing the extension,
// we intercept the TFB commands and emulate them via:
//   - Tracking bound XFB buffers/offsets
//   - Recording vertex counts from draw calls
//   - Writing transform output to the XFB buffer via vkCmdCopyBuffer
//   - Supporting vkCmdDrawIndirectByteCountEXT via tracked byte counts
// ═══════════════════════════════════════════════════════════════════════════

#define EXYNOS_TFB_MAX_BUFFERS 4   // VK_EXT_transform_feedback max bindings
#define EXYNOS_TFB_MAX_STREAMS 4

// Per-stream state
typedef struct ExynosTfbStream {
    int          active;           // Is this stream currently recording?
    VkBuffer     buffer;           // Bound XFB buffer
    VkDeviceSize offset;           // Current write offset
    VkDeviceSize size;             // Buffer size limit
    VkBuffer     counter_buffer;   // Counter buffer (for resume)
    VkDeviceSize counter_offset;
    uint64_t     bytes_written;    // Total bytes written in this pass
} ExynosTfbStream;

// Global emulator state
typedef struct ExynosTfbEmulator {
    int              enabled;
    VkDevice         device;
    pthread_mutex_t  lock;
    ExynosTfbStream  streams[EXYNOS_TFB_MAX_STREAMS];
    int              is_active;    // Is ANY stream currently recording?

    // Statistics
    uint32_t begin_count;
    uint32_t end_count;
    uint32_t draw_indirect_byte_count;
    uint32_t bind_buffer_count;
} ExynosTfbEmulator;

/// Initialize the emulator.
void exynos_tfb_init(ExynosTfbEmulator* e, VkDevice device);

/// Destroy the emulator.
void exynos_tfb_destroy(ExynosTfbEmulator* e);

/// Hook: vkCmdBindTransformFeedbackBuffersEXT
void exynos_tfb_bind_buffers(
    ExynosTfbEmulator* e,
    VkCommandBuffer cmd,
    uint32_t first_binding,
    uint32_t binding_count,
    const VkBuffer* pBuffers,
    const VkDeviceSize* pOffsets,
    const VkDeviceSize* pSizes);

/// Hook: vkCmdBeginTransformFeedbackEXT
void exynos_tfb_begin(
    ExynosTfbEmulator* e,
    VkCommandBuffer cmd,
    uint32_t first_counter_buffer,
    uint32_t counter_buffer_count,
    const VkBuffer* pCounterBuffers,
    const VkDeviceSize* pCounterBufferOffsets);

/// Hook: vkCmdEndTransformFeedbackEXT
void exynos_tfb_end(
    ExynosTfbEmulator* e,
    VkCommandBuffer cmd,
    uint32_t first_counter_buffer,
    uint32_t counter_buffer_count,
    const VkBuffer* pCounterBuffers,
    const VkDeviceSize* pCounterBufferOffsets);

/// Hook: vkCmdDrawIndirectByteCountEXT
void exynos_tfb_draw_indirect_byte_count(
    ExynosTfbEmulator* e,
    VkCommandBuffer cmd,
    uint32_t instance_count,
    uint32_t first_instance,
    VkBuffer counter_buffer,
    VkDeviceSize counter_buffer_offset,
    uint32_t counter_offset,
    uint32_t vertex_stride);

/// Get stats
void exynos_tfb_get_stats(const ExynosTfbEmulator* e,
                          uint32_t* begins, uint32_t* ends,
                          uint32_t* draws, uint32_t* binds);

#ifdef __cplusplus
}
#endif
