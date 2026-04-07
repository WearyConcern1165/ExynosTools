#include "format_support.h"
#include "bcn_decoder/bcn_config.h"

namespace bcn {

// ---------------------------------------------------------------------------
// BCn format → VkFormat (compressed)
// ---------------------------------------------------------------------------
VkFormat bcnFormatToVkFormat(BcnFormat fmt) {
    switch (fmt) {
        case BcnFormat::BC1_RGB_UNORM: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case BcnFormat::BC2_UNORM:     return VK_FORMAT_BC2_UNORM_BLOCK;
        case BcnFormat::BC3_UNORM:     return VK_FORMAT_BC3_UNORM_BLOCK;
        case BcnFormat::BC4_UNORM:     return VK_FORMAT_BC4_UNORM_BLOCK;
        case BcnFormat::BC5_UNORM:     return VK_FORMAT_BC5_UNORM_BLOCK;
        case BcnFormat::BC7_UNORM:     return VK_FORMAT_BC7_UNORM_BLOCK;
        default:                       return VK_FORMAT_UNDEFINED;
    }
}

// ---------------------------------------------------------------------------
// BCn format → uncompressed output VkFormat (compute fallback output)
// ---------------------------------------------------------------------------
VkFormat bcnFormatToOutputVkFormat(BcnFormat fmt) {
    switch (fmt) {
        case BcnFormat::BC1_RGB_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case BcnFormat::BC2_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
        case BcnFormat::BC3_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
        case BcnFormat::BC4_UNORM:     return VK_FORMAT_R8_UNORM;
        case BcnFormat::BC5_UNORM:     return VK_FORMAT_R8G8_UNORM;
        case BcnFormat::BC7_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
        default:                       return VK_FORMAT_UNDEFINED;
    }
}

// ---------------------------------------------------------------------------
// Block size in bytes
// ---------------------------------------------------------------------------
uint32_t bcnBlockSizeBytes(BcnFormat fmt) {
    switch (fmt) {
        case BcnFormat::BC1_RGB_UNORM: return config::kBc1BlockBytes;
        case BcnFormat::BC2_UNORM:     return config::kBc2BlockBytes;
        case BcnFormat::BC3_UNORM:     return config::kBc3BlockBytes;
        case BcnFormat::BC4_UNORM:     return config::kBc4BlockBytes;
        case BcnFormat::BC5_UNORM:     return config::kBc5BlockBytes;
        case BcnFormat::BC7_UNORM:     return config::kBc7BlockBytes;
        default:                       return 0;
    }
}

// ---------------------------------------------------------------------------
// FormatSupportTable
// ---------------------------------------------------------------------------
void FormatSupportTable::queryAll(VkPhysicalDevice physDevice) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(BcnFormat::Count); ++i) {
        BcnFormat fmt = static_cast<BcnFormat>(i);
        VkFormat  vkFmt = bcnFormatToVkFormat(fmt);
        if (vkFmt == VK_FORMAT_UNDEFINED) {
            table_[i] = BcnSupportPath::NotSupported;
            continue;
        }

        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physDevice, vkFmt, &props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
            table_[i] = BcnSupportPath::Native;
        } else {
            // Check if we have a compute fallback implementation for this format.
            // In Phase 1, only BC5 and BC7 have compute fallback.
            if (fmt == BcnFormat::BC5_UNORM || fmt == BcnFormat::BC7_UNORM) {
                table_[i] = BcnSupportPath::ComputeFallback;
            } else {
                table_[i] = BcnSupportPath::NotSupported;
            }
        }
    }
}

BcnSupportPath FormatSupportTable::getSupport(BcnFormat fmt) const {
    uint32_t idx = static_cast<uint32_t>(fmt);
    if (idx >= static_cast<uint32_t>(BcnFormat::Count))
        return BcnSupportPath::NotSupported;
    return table_[idx];
}

bool FormatSupportTable::isNativelySupported(BcnFormat fmt) const {
    return getSupport(fmt) == BcnSupportPath::Native;
}

} // namespace bcn
