#pragma once
/// @file bcn_config.h
/// @brief Compile-time constants and tunables for the BCn decoder.

#include <cstdint>

namespace bcn {
namespace config {

// ---------------------------------------------------------------------------
// Block compression constants
// ---------------------------------------------------------------------------

/// All BC formats encode 4×4 texel blocks.
constexpr uint32_t kBlockWidth  = 4;
constexpr uint32_t kBlockHeight = 4;

/// BC5 and BC7 blocks are 16 bytes (128 bits) each.
constexpr uint32_t kBc5BlockBytes = 16;
constexpr uint32_t kBc7BlockBytes = 16;

/// BC1 blocks are 8 bytes (64 bits).
constexpr uint32_t kBc1BlockBytes = 8;
/// BC2 and BC3 blocks are 16 bytes (128 bits).
constexpr uint32_t kBc2BlockBytes = 16;
constexpr uint32_t kBc3BlockBytes = 16;
/// BC4 blocks are 8 bytes (64 bits).
constexpr uint32_t kBc4BlockBytes = 8;

// ---------------------------------------------------------------------------
// Compute shader workgroup size
// ---------------------------------------------------------------------------

/// Workgroup size for BC decode compute shaders.
/// 8×8 = 64 invocations, filling one RDNA wave64 or two wave32.
constexpr uint32_t kWorkgroupSizeX = 8;
constexpr uint32_t kWorkgroupSizeY = 8;

// ---------------------------------------------------------------------------
// Memory / cache defaults
// ---------------------------------------------------------------------------

/// Default cache budget in bytes (128 MiB).  0 = cache disabled.
constexpr uint64_t kDefaultCacheBudgetBytes = 128ULL * 1024 * 1024;

/// Maximum number of entries in the texture cache.
constexpr uint32_t kMaxCacheEntries = 1024;

// ---------------------------------------------------------------------------
// Staging buffer pool bucket sizes
// ---------------------------------------------------------------------------
constexpr uint64_t kStagingBucketSmall  =   64ULL * 1024;       //  64 KiB
constexpr uint64_t kStagingBucketMedium =  256ULL * 1024;       // 256 KiB
constexpr uint64_t kStagingBucketLarge  = 1024ULL * 1024;       //   1 MiB
constexpr uint64_t kStagingBucketXLarge = 4096ULL * 1024;       //   4 MiB

// ---------------------------------------------------------------------------
// Descriptor pool sizing
// ---------------------------------------------------------------------------

/// Number of descriptor sets pre-allocated per pool.
constexpr uint32_t kDescriptorPoolMaxSets = 64;

} // namespace config
} // namespace bcn
