#pragma once
/// @file format_support.h
/// @brief Runtime detection of BCn format support via Vulkan.

#include "bcn_decoder/bcn_types.h"
#include "xclipse_extensions.h"
#include <vulkan/vulkan.h>
#include <array>

namespace bcn {

/// Maps BcnFormat → VkFormat (the compressed format).
VkFormat bcnFormatToVkFormat(BcnFormat fmt);

/// Maps BcnFormat → the uncompressed output VkFormat used by compute fallback.
VkFormat bcnFormatToOutputVkFormat(BcnFormat fmt);

/// Returns the block size in bytes for a given BcnFormat.
uint32_t bcnBlockSizeBytes(BcnFormat fmt);

/// Stores the results of a runtime format support query.
class FormatSupportTable {
public:
    /// Query support for all BCn formats on the given physical device.
    void queryAll(VkPhysicalDevice physDevice);

    /// Profile-aware query: also stores which GPU we're running on.
    void queryAll(VkPhysicalDevice physDevice, XclipseProfile profile);

    /// Get the support path for a specific format.
    BcnSupportPath getSupport(BcnFormat fmt) const;

    /// Returns true if the format has SAMPLED_IMAGE_BIT in optimalTilingFeatures.
    bool isNativelySupported(BcnFormat fmt) const;

    /// Returns true if ALL critical BCn formats (BC5, BC7) have native hardware support.
    /// If true, compute shader fallback is NOT needed (e.g., Xclipse 950 with good driver).
    bool allBcnNativelySupported() const;

    /// Get the detected profile.
    XclipseProfile detectedProfile() const { return profile_; }

private:
    std::array<BcnSupportPath, static_cast<size_t>(BcnFormat::Count)> table_{};
    XclipseProfile profile_ = XclipseProfile::Unknown;
};

} // namespace bcn

