#include "bcn_decoder/bcn_decoder.h"
#include "bcn_decoder/bcn_config.h"
#include "core/format_support.h"
#include "compute/bc5_decoder.h"
#include "compute/bc7_decoder.h"
#include "native/native_upload.h"
#include "memory/texture_cache.h"
#include "memory/staging_buffer_pool.h"

#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include "rgtc_spv.h"
#include "bc7_spv.h"

// ---------------------------------------------------------------------------
// Embedded SPIR-V forward declaration
// ---------------------------------------------------------------------------
// These are generated at build time by glslc and linked as binary resources,
// OR loaded from the shader directory at runtime.

namespace bcn {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct BcnDecoderContext::Impl {
    VkDevice             device         = VK_NULL_HANDLE;
    VkPhysicalDevice     physDevice     = VK_NULL_HANDLE;
    VkQueue              computeQueue   = VK_NULL_HANDLE;
    uint32_t             computeQueueFamily = 0;
    VkCommandPool        cmdPool        = VK_NULL_HANDLE;

    FormatSupportTable   formatTable;
    Bc5Decoder           bc5Decoder;
    Bc7Decoder           bc7Decoder;
    bool                 bc7Ready = false;
    TextureCache*        cache          = nullptr;
    StagingBufferPool    stagingPool;

    std::string          shaderDir; // No longer used for shaders, kept for API compatibility

    // Non-cached textures that the user holds handles to
    struct OwnedTexture {
        vk::ImageWrapper  image;
        BcnSupportPath    pathUsed;
    };
    std::unordered_map<uint64_t, OwnedTexture> ownedTextures;
    uint64_t nextOwnedId = 0x8000000000000000ULL; // high bit set = non-cached
    std::mutex ownedMutex;
};

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------
BcnResult BcnDecoderContext::create(const DecoderCreateInfo& info,
                                     BcnDecoderContext** outCtx)
{
    if (!outCtx) return BcnResult::ErrorVulkanFailed;
    *outCtx = nullptr;

    if (info.device == VK_NULL_HANDLE ||
        info.physicalDevice == VK_NULL_HANDLE ||
        info.computeQueue == VK_NULL_HANDLE) {
        return BcnResult::ErrorNotInitialized;
    }

    auto* ctx = new BcnDecoderContext();
    ctx->impl_ = new Impl();
    auto* d = ctx->impl_;

    d->device             = info.device;
    d->physDevice         = info.physicalDevice;
    d->computeQueue       = info.computeQueue;
    d->computeQueueFamily = info.computeQueueFamily;
    d->shaderDir          = info.shaderDirectory ? info.shaderDirectory : "";

    // -- Command pool --------------------------------------------------------
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = d->computeQueueFamily;

    VkResult res = vkCreateCommandPool(d->device, &poolInfo, nullptr, &d->cmdPool);
    if (res != VK_SUCCESS) {
        delete d;
        delete ctx;
        return BcnResult::ErrorVulkanFailed;
    }

    // -- Format support table ------------------------------------------------
    d->formatTable.queryAll(d->physDevice);

    BcnResult poolResult = d->stagingPool.init(d->device, d->physDevice);
    if (poolResult != BcnResult::Success) {
        vkDestroyCommandPool(d->device, d->cmdPool, nullptr);
        delete d;
        delete ctx;
        return poolResult;
    }

    // -- BC5 decoder pipeline ------------------------------------------------
    if (d->formatTable.getSupport(BcnFormat::BC5_UNORM) == BcnSupportPath::ComputeFallback) {
        BcnResult bcRes = d->bc5Decoder.init(
            d->device, d->physDevice, &d->stagingPool,
            spirv::RGTC_SPV, spirv::RGTC_SPV_SIZE);
        if (bcRes != BcnResult::Success) {
            // Log warning but continue – native path still works.
        }
    }

    // -- BC7 decoder pipeline ------------------------------------------------
    if (d->formatTable.getSupport(BcnFormat::BC7_UNORM) == BcnSupportPath::ComputeFallback) {
        BcnResult bcRes = d->bc7Decoder.init(
            d->device, d->physDevice, &d->stagingPool,
            spirv::BC7_SPV, spirv::BC7_SPV_SIZE);
        if (bcRes == BcnResult::Success) {
            d->bc7Ready = true;
        }
    }

    // -- Cache ---------------------------------------------------------------
    if (info.memoryCacheBudgetBytes > 0) {
        d->cache = new TextureCache(info.memoryCacheBudgetBytes);
    }

    *outCtx = ctx;
    return BcnResult::Success;
}

// ---------------------------------------------------------------------------
// destroy
// ---------------------------------------------------------------------------
void BcnDecoderContext::destroy() {
    if (!impl_) return;

    // Destroy all non-cached owned textures
    for (auto& [id, owned] : impl_->ownedTextures) {
        owned.image.destroy();
    }
    impl_->ownedTextures.clear();

    // Destroy cache
    if (impl_->cache) {
        impl_->cache->flush();
        delete impl_->cache;
        impl_->cache = nullptr;
    }

    // Destroy decoders
    impl_->bc7Decoder.destroy();
    impl_->bc5Decoder.destroy();
    impl_->stagingPool.shutdown();

    // Destroy command pool
    if (impl_->cmdPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(impl_->device, impl_->cmdPool, nullptr);
        impl_->cmdPool = VK_NULL_HANDLE;
    }

    delete impl_;
    impl_ = nullptr;
    delete this;
}

// ---------------------------------------------------------------------------
// queryFormatSupport
// ---------------------------------------------------------------------------
BcnSupportPath BcnDecoderContext::queryFormatSupport(BcnFormat format) const {
    if (!impl_) return BcnSupportPath::NotSupported;
    return impl_->formatTable.getSupport(format);
}

// ---------------------------------------------------------------------------
// decodeTexture
// ---------------------------------------------------------------------------
BcnResult BcnDecoderContext::decodeTexture(const TextureDecodeInfo& info,
                                            BcnTextureHandle* outHandle)
{
    if (!impl_ || !outHandle) return BcnResult::ErrorNotInitialized;
    *outHandle = {};

    // Check cache first
    if (info.cacheKey && impl_->cache) {
        CachedTexture* cached = impl_->cache->find(info.cacheKey);
        if (cached) {
            *outHandle = cached->handle;
            return BcnResult::Success;
        }
    }

    BcnSupportPath path = impl_->formatTable.getSupport(info.format);
    if (path == BcnSupportPath::NotSupported)
        return BcnResult::ErrorUnsupportedFormat;

    vk::ImageWrapper resultImage;
    BcnResult result = BcnResult::ErrorVulkanFailed;

    if (path == BcnSupportPath::Native) {
        // -- Native upload path -----------------------------------------------
        result = NativeUploader::upload(
            info.format,
            info.compressedData,
            info.compressedDataSize,
            info.width,
            info.height,
            info.mipLevels,
            impl_->device,
            impl_->physDevice,
            impl_->computeQueue,
            impl_->cmdPool,
            &impl_->stagingPool,
            &resultImage);
    }
    else if (path == BcnSupportPath::ComputeFallback) {
        // -- Compute fallback path --------------------------------------------
        if (info.format == BcnFormat::BC5_UNORM) {
            result = impl_->bc5Decoder.decode(
                info.compressedData,
                info.compressedDataSize,
                info.width,
                info.height,
                impl_->computeQueue,
                impl_->cmdPool,
                &resultImage);
        } else if (info.format == BcnFormat::BC7_UNORM && impl_->bc7Ready) {
            result = impl_->bc7Decoder.decode(
                info.compressedData,
                info.compressedDataSize,
                info.width,
                info.height,
                impl_->computeQueue,
                impl_->cmdPool,
                &resultImage);
        } else {
            return BcnResult::ErrorUnsupportedFormat;
        }
    }

    if (result != BcnResult::Success)
        return result;

    uint64_t memSize = resultImage.memorySize();

    // Insert into cache if key provided
    if (info.cacheKey && impl_->cache) {
        BcnTextureHandle h = impl_->cache->insert(
            info.cacheKey,
            std::move(resultImage),
            path,
            memSize);
        *outHandle = h;
    } else {
        // Non-cached: store in owned textures map
        std::lock_guard<std::mutex> lock(impl_->ownedMutex);
        uint64_t id = impl_->nextOwnedId++;
        impl_->ownedTextures[id] = Impl::OwnedTexture{
            std::move(resultImage), path
        };
        *outHandle = BcnTextureHandle{ id };
    }

    return BcnResult::Success;
}

// ---------------------------------------------------------------------------
// getDecodedTexture
// ---------------------------------------------------------------------------
BcnResult BcnDecoderContext::getDecodedTexture(BcnTextureHandle handle,
                                                DecodedTexture* outTexture) const
{
    if (!impl_ || !outTexture) return BcnResult::ErrorNotInitialized;

    // Check cache first
    if (impl_->cache) {
        const CachedTexture* ct = impl_->cache->get(handle);
        if (ct) {
            outTexture->image       = ct->image.image();
            outTexture->imageView   = ct->image.imageView();
            outTexture->vulkanFormat = ct->image.format();
            outTexture->width       = ct->image.width();
            outTexture->height      = ct->image.height();
            outTexture->pathUsed    = ct->pathUsed;
            return BcnResult::Success;
        }
    }

    // Check non-cached owned textures
    {
        std::lock_guard<std::mutex> lock(impl_->ownedMutex);
        auto it = impl_->ownedTextures.find(handle.id);
        if (it != impl_->ownedTextures.end()) {
            const auto& owned = it->second;
            outTexture->image       = owned.image.image();
            outTexture->imageView   = owned.image.imageView();
            outTexture->vulkanFormat = owned.image.format();
            outTexture->width       = owned.image.width();
            outTexture->height      = owned.image.height();
            outTexture->pathUsed    = owned.pathUsed;
            return BcnResult::Success;
        }
    }

    return BcnResult::ErrorInvalidFormat;
}

// ---------------------------------------------------------------------------
// releaseTexture
// ---------------------------------------------------------------------------
void BcnDecoderContext::releaseTexture(BcnTextureHandle handle) {
    if (!impl_) return;

    // Try cache
    if (impl_->cache) {
        // If remove succeeds it frees the image
        impl_->cache->remove(handle);
    }

    // Try non-cached
    {
        std::lock_guard<std::mutex> lock(impl_->ownedMutex);
        auto it = impl_->ownedTextures.find(handle.id);
        if (it != impl_->ownedTextures.end()) {
            it->second.image.destroy();
            impl_->ownedTextures.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// Cache helpers
// ---------------------------------------------------------------------------
void BcnDecoderContext::flushCache() {
    if (impl_ && impl_->cache)
        impl_->cache->flush();
}

uint64_t BcnDecoderContext::getCacheUsageBytes() const {
    return (impl_ && impl_->cache) ? impl_->cache->usageBytes() : 0;
}

uint64_t BcnDecoderContext::getCacheBudgetBytes() const {
    return (impl_ && impl_->cache) ? impl_->cache->budgetBytes() : 0;
}

} // namespace bcn
