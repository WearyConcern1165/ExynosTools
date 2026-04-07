#pragma once
/// @file bcn_types.h
/// @brief Core types, enums and error codes for the BCn decoder library.

#include <cstdint>

namespace bcn {

// ---------------------------------------------------------------------------
// Error / result codes
// ---------------------------------------------------------------------------
enum class BcnResult : int32_t {
    Success              =  0,
    ErrorNotInitialized  = -1,
    ErrorInvalidFormat   = -2,
    ErrorOutOfMemory     = -3,
    ErrorVulkanFailed    = -4,
    ErrorUnsupportedFormat = -5,
    ErrorCacheFull       = -6,
    ErrorInvalidDimensions = -7,
    ErrorShaderLoadFailed  = -8,
    ErrorPipelineCreationFailed = -9,
};

/// Returns a human-readable string for a BcnResult code.
inline const char* resultToString(BcnResult r) {
    switch (r) {
        case BcnResult::Success:                    return "Success";
        case BcnResult::ErrorNotInitialized:        return "ErrorNotInitialized";
        case BcnResult::ErrorInvalidFormat:         return "ErrorInvalidFormat";
        case BcnResult::ErrorOutOfMemory:           return "ErrorOutOfMemory";
        case BcnResult::ErrorVulkanFailed:          return "ErrorVulkanFailed";
        case BcnResult::ErrorUnsupportedFormat:     return "ErrorUnsupportedFormat";
        case BcnResult::ErrorCacheFull:             return "ErrorCacheFull";
        case BcnResult::ErrorInvalidDimensions:     return "ErrorInvalidDimensions";
        case BcnResult::ErrorShaderLoadFailed:      return "ErrorShaderLoadFailed";
        case BcnResult::ErrorPipelineCreationFailed:return "ErrorPipelineCreationFailed";
        default:                                    return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// BCn format enumeration
// ---------------------------------------------------------------------------
enum class BcnFormat : uint32_t {
    BC1_RGB_UNORM = 0,
    BC2_UNORM     = 1,
    BC3_UNORM     = 2,
    BC4_UNORM     = 3,
    BC5_UNORM     = 4,
    BC7_UNORM     = 5,
    Count         = 6
};

// ---------------------------------------------------------------------------
// Decode path taken for a given format
// ---------------------------------------------------------------------------
enum class BcnSupportPath : uint8_t {
    NotSupported    = 0,  ///< Neither native nor compute fallback available
    Native          = 1,  ///< Hardware supports the BC format directly
    ComputeFallback = 2   ///< Decoded via compute shader at load time
};

// ---------------------------------------------------------------------------
// Opaque texture handle returned to the user
// ---------------------------------------------------------------------------
struct BcnTextureHandle {
    uint64_t id = 0;
    bool valid() const { return id != 0; }
};

} // namespace bcn
