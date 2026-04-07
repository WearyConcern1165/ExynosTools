#pragma once
/// @file bcn_decoder.h
/// @brief Public API for the BCn texture decoder library.
///
/// Usage:
///   1. Fill a DecoderCreateInfo with your Vulkan handles.
///   2. Call BcnDecoderContext::create() to initialise.
///   3. For each texture call decodeTexture() → getDecodedTexture().
///   4. When done with a texture call releaseTexture().
///   5. Call destroy() before tearing down Vulkan.

#include <vulkan/vulkan.h>
#include "bcn_types.h"

namespace bcn {

// ---------------------------------------------------------------------------
// Structs for context creation and texture decode requests
// ---------------------------------------------------------------------------

/// Parameters required to initialise the decoder context.
struct DecoderCreateInfo {
    VkInstance           instance           = VK_NULL_HANDLE;
    VkPhysicalDevice     physicalDevice     = VK_NULL_HANDLE;
    VkDevice             device             = VK_NULL_HANDLE;
    VkQueue              computeQueue       = VK_NULL_HANDLE;  ///< Must support COMPUTE
    uint32_t             computeQueueFamily = 0;
    uint64_t             memoryCacheBudgetBytes = 0;           ///< 0 = cache disabled
    bool                 enableValidation   = false;

    /// Optional: path to the directory containing compiled SPIR-V shaders.
    /// If null the library looks next to the .so / in assets.
    const char*          shaderDirectory    = nullptr;
};

/// Describes a texture to be decoded.
struct TextureDecodeInfo {
    BcnFormat            format             = BcnFormat::BC5_UNORM;
    const uint8_t*       compressedData     = nullptr;
    uint64_t             compressedDataSize = 0;
    uint32_t             width              = 0;   ///< Texel width  (not blocks)
    uint32_t             height             = 0;   ///< Texel height (not blocks)
    uint32_t             mipLevels          = 1;   ///< 1 = base mip only
    const char*          cacheKey           = nullptr; ///< nullptr = do not cache
};

/// Information about a successfully decoded texture.
struct DecodedTexture {
    VkImage              image      = VK_NULL_HANDLE;
    VkImageView          imageView  = VK_NULL_HANDLE;
    VkFormat             vulkanFormat = VK_FORMAT_UNDEFINED;
    uint32_t             width      = 0;
    uint32_t             height     = 0;
    BcnSupportPath       pathUsed   = BcnSupportPath::NotSupported;
};

// ---------------------------------------------------------------------------
// Main context class (PImpl)
// ---------------------------------------------------------------------------

class BcnDecoderContext {
public:
    /// Factory – creates and initialises a decoder context.
    /// On success *outCtx is a valid pointer; on failure it is nullptr.
    static BcnResult create(const DecoderCreateInfo& info,
                            BcnDecoderContext** outCtx);

    /// Tears down all internal resources.  Must be called before destroying
    /// the VkDevice that was passed to create().
    void destroy();

    // -- Format support query ------------------------------------------------

    /// Returns how a given BCn format would be handled on this device.
    BcnSupportPath queryFormatSupport(BcnFormat format) const;

    // -- Texture decode ------------------------------------------------------

    /// Synchronously decodes a BCn texture.
    /// Blocks until the GPU work (native upload or compute decode) completes.
    BcnResult decodeTexture(const TextureDecodeInfo& info,
                            BcnTextureHandle* outHandle);

    /// Retrieves Vulkan handles for a previously decoded texture.
    BcnResult getDecodedTexture(BcnTextureHandle handle,
                                DecodedTexture* outTexture) const;

    /// Releases a decoded texture and frees its GPU resources.
    /// The handle becomes invalid after this call.
    void releaseTexture(BcnTextureHandle handle);

    // -- Cache management ----------------------------------------------------

    /// Evicts all entries from the texture cache.
    void flushCache();

    /// Returns current memory usage of cached textures in bytes.
    uint64_t getCacheUsageBytes() const;

    /// Returns the configured cache budget in bytes.
    uint64_t getCacheBudgetBytes() const;

private:
    BcnDecoderContext() = default;
    ~BcnDecoderContext() = default;
    BcnDecoderContext(const BcnDecoderContext&) = delete;
    BcnDecoderContext& operator=(const BcnDecoderContext&) = delete;

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace bcn
