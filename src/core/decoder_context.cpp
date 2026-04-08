#include "bcn_decoder/bcn_decoder.h"
#include "bcn_decoder/bcn_config.h"
#include "core/format_support.h"
#include "compute/bc5_decoder.h"
#include "compute/bc7_decoder.h"
#include "compute/compute_pipeline.h"
#include "native/native_upload.h"
#include "memory/texture_cache.h"
#include "memory/staging_buffer_pool.h"
#include "vulkan/vk_utils.h"

#include <cstring>
#include <limits>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "bc6_iv_spv.h"
#include "bc7_iv_spv.h"
#include "bc7_spv.h"
#include "rgtc_iv_spv.h"
#include "rgtc_spv.h"
#include "s3tc_iv_spv.h"

// ---------------------------------------------------------------------------
// Embedded SPIR-V forward declaration
// ---------------------------------------------------------------------------
// These are generated at build time by glslc and linked as binary resources,
// OR loaded from the shader directory at runtime.

namespace bcn {

namespace {

struct GranitePushConstants {
    int format;
    int width;
    int height;
    int offset;
    int bufferRowLength;
    int offsetX;
    int offsetY;
};

struct ImageViewKey {
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;

    bool operator==(const ImageViewKey& other) const {
        return image == other.image &&
               format == other.format &&
               mipLevel == other.mipLevel &&
               arrayLayer == other.arrayLayer;
    }
};

struct ImageViewKeyHash {
    size_t operator()(const ImageViewKey& key) const {
        size_t h = std::hash<VkImage>{}(key.image);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(key.format)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(key.mipLevel) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(key.arrayLayer) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

static uint32_t ceil_div(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1u) / divisor;
}

static bool is_rgtc_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
            return true;
        default:
            return false;
    }
}

static bool is_s3tc_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

static bool is_bc6_format(VkFormat format) {
    return format == VK_FORMAT_BC6H_UFLOAT_BLOCK || format == VK_FORMAT_BC6H_SFLOAT_BLOCK;
}

static bool is_bc7_format(VkFormat format) {
    return format == VK_FORMAT_BC7_UNORM_BLOCK || format == VK_FORMAT_BC7_SRGB_BLOCK;
}

static uint32_t compressed_block_bytes(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return config::kBc1BlockBytes;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
            return config::kBc2BlockBytes;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return config::kBc3BlockBytes;
        case VK_FORMAT_BC4_UNORM_BLOCK:
            return config::kBc4BlockBytes;
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return config::kBc7BlockBytes;
        default:
            return 0;
    }
}

static VkResult create_subresource_image_view(VkDevice device,
                                              VkImage image,
                                              VkFormat format,
                                              uint32_t mipLevel,
                                              uint32_t arrayLayer,
                                              VkImageView* outView) {
    if (!outView) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = mipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
    viewInfo.subresourceRange.layerCount = 1;
    return vkCreateImageView(device, &viewInfo, nullptr, outView);
}

} // namespace

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
    ComputePipeline      s3tcInlinePipeline;
    ComputePipeline      rgtcInlinePipeline;
    ComputePipeline      bc6InlinePipeline;
    ComputePipeline      bc7InlinePipeline;
    bool                 s3tcInlineReady = false;
    bool                 rgtcInlineReady = false;
    bool                 bc6InlineReady = false;
    bool                 bc7InlineReady = false;
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

    std::unordered_map<ImageViewKey, VkImageView, ImageViewKeyHash> inlineViews;
    std::mutex inlineViewsMutex;
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

    // -- Inline decode pipelines for wrapper integration ---------------------
    if (d->s3tcInlinePipeline.create(
            d->device,
            spirv::S3TC_IV_SPV,
            spirv::S3TC_IV_SPV_SIZE,
            sizeof(GranitePushConstants),
            config::kWorkgroupSizeX,
            config::kWorkgroupSizeY) == VK_SUCCESS) {
        d->s3tcInlineReady = true;
    }

    if (d->rgtcInlinePipeline.create(
            d->device,
            spirv::RGTC_IV_SPV,
            spirv::RGTC_IV_SPV_SIZE,
            sizeof(GranitePushConstants),
            config::kWorkgroupSizeX,
            config::kWorkgroupSizeY) == VK_SUCCESS) {
        d->rgtcInlineReady = true;
    }

    if (d->bc6InlinePipeline.create(
            d->device,
            spirv::BC6_IV_SPV,
            spirv::BC6_IV_SPV_SIZE,
            sizeof(GranitePushConstants),
            config::kWorkgroupSizeX,
            config::kWorkgroupSizeY) == VK_SUCCESS) {
        d->bc6InlineReady = true;
    }

    if (d->bc7InlinePipeline.create(
            d->device,
            spirv::BC7_IV_SPV,
            spirv::BC7_IV_SPV_SIZE,
            sizeof(GranitePushConstants),
            config::kWorkgroupSizeX,
            config::kWorkgroupSizeY) == VK_SUCCESS) {
        d->bc7InlineReady = true;
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

    for (auto& [key, view] : impl_->inlineViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(impl_->device, view, nullptr);
        }
    }
    impl_->inlineViews.clear();

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
    impl_->bc7InlinePipeline.destroy();
    impl_->bc6InlinePipeline.destroy();
    impl_->rgtcInlinePipeline.destroy();
    impl_->s3tcInlinePipeline.destroy();
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

BcnResult BcnDecoderContext::recordDecodeBufferToImage(const BufferImageDecodeInfo& info) {
    if (!impl_) {
        return BcnResult::ErrorNotInitialized;
    }
    if (info.commandBuffer == VK_NULL_HANDLE ||
        info.srcBuffer == VK_NULL_HANDLE ||
        info.dstImage == VK_NULL_HANDLE ||
        info.regionCount == 0 ||
        !info.pRegions) {
        return BcnResult::ErrorInvalidFormat;
    }

    ComputePipeline* pipeline = nullptr;
    if (is_s3tc_format(info.sourceFormat)) {
        if (!impl_->s3tcInlineReady) {
            return BcnResult::ErrorUnsupportedFormat;
        }
        pipeline = &impl_->s3tcInlinePipeline;
    } else if (is_rgtc_format(info.sourceFormat)) {
        if (!impl_->rgtcInlineReady) {
            return BcnResult::ErrorUnsupportedFormat;
        }
        pipeline = &impl_->rgtcInlinePipeline;
    } else if (is_bc6_format(info.sourceFormat)) {
        if (!impl_->bc6InlineReady) {
            return BcnResult::ErrorUnsupportedFormat;
        }
        pipeline = &impl_->bc6InlinePipeline;
    } else if (is_bc7_format(info.sourceFormat)) {
        if (!impl_->bc7InlineReady) {
            return BcnResult::ErrorUnsupportedFormat;
        }
        pipeline = &impl_->bc7InlinePipeline;
    } else {
        return BcnResult::ErrorUnsupportedFormat;
    }

    const uint32_t blockBytes = compressed_block_bytes(info.sourceFormat);
    if (blockBytes == 0) {
        return BcnResult::ErrorUnsupportedFormat;
    }

    for (uint32_t regionIndex = 0; regionIndex < info.regionCount; ++regionIndex) {
        const VkBufferImageCopy& region = info.pRegions[regionIndex];
        if (region.imageExtent.depth != 1 || region.imageSubresource.layerCount == 0) {
            return BcnResult::ErrorUnsupportedFormat;
        }
        if (region.imageOffset.x < 0 || region.imageOffset.y < 0) {
            return BcnResult::ErrorInvalidDimensions;
        }

        const uint32_t rowLength = region.bufferRowLength ? region.bufferRowLength : region.imageExtent.width;
        const uint32_t imageHeight = region.bufferImageHeight ? region.bufferImageHeight : region.imageExtent.height;
        const uint64_t blocksPerRow = ceil_div(rowLength, config::kBlockWidth);
        const uint64_t rowsPerImage = ceil_div(imageHeight, config::kBlockHeight);
        const uint64_t layerStride = blocksPerRow * rowsPerImage * blockBytes;

        for (uint32_t layer = 0; layer < region.imageSubresource.layerCount; ++layer) {
            const uint32_t arrayLayer = region.imageSubresource.baseArrayLayer + layer;
            const uint64_t layerOffset = region.bufferOffset + layerStride * layer;
            if (layerOffset > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                return BcnResult::ErrorInvalidDimensions;
            }

            ImageViewKey key{};
            key.image = info.dstImage;
            key.format = info.destinationFormat;
            key.mipLevel = region.imageSubresource.mipLevel;
            key.arrayLayer = arrayLayer;

            VkImageView view = VK_NULL_HANDLE;
            {
                std::lock_guard<std::mutex> lock(impl_->inlineViewsMutex);
                auto it = impl_->inlineViews.find(key);
                if (it != impl_->inlineViews.end()) {
                    view = it->second;
                } else {
                    VkResult createRes = create_subresource_image_view(
                        impl_->device,
                        info.dstImage,
                        info.destinationFormat,
                        region.imageSubresource.mipLevel,
                        arrayLayer,
                        &view);
                    if (createRes != VK_SUCCESS) {
                        return BcnResult::ErrorVulkanFailed;
                    }
                    impl_->inlineViews.emplace(key, view);
                }
            }

            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            VkResult dsRes = pipeline->allocateDescriptorSet(&descriptorSet);
            if (dsRes != VK_SUCCESS) {
                return BcnResult::ErrorVulkanFailed;
            }

            pipeline->updateDescriptorSet(descriptorSet, info.srcBuffer, VK_WHOLE_SIZE, view);

            if (info.currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
                vk::transitionImageSubresourceLayout(
                    info.commandBuffer,
                    info.dstImage,
                    info.destinationFormat,
                    info.currentLayout,
                    VK_IMAGE_LAYOUT_GENERAL,
                    region.imageSubresource.mipLevel,
                    1,
                    arrayLayer,
                    1);
            }

            vkCmdBindPipeline(info.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline());
            vkCmdBindDescriptorSets(info.commandBuffer,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    pipeline->pipelineLayout(),
                                    0,
                                    1,
                                    &descriptorSet,
                                    0,
                                    nullptr);

            GranitePushConstants pc{};
            pc.format = static_cast<int>(info.sourceFormat);
            pc.width = static_cast<int>(region.imageExtent.width);
            pc.height = static_cast<int>(region.imageExtent.height);
            pc.offset = static_cast<int>(layerOffset);
            pc.bufferRowLength = static_cast<int>(rowLength);
            pc.offsetX = region.imageOffset.x;
            pc.offsetY = region.imageOffset.y;

            vkCmdPushConstants(info.commandBuffer,
                               pipeline->pipelineLayout(),
                               VK_SHADER_STAGE_COMPUTE_BIT,
                               0,
                               sizeof(pc),
                               &pc);

            vkCmdDispatch(info.commandBuffer,
                          ceil_div(region.imageExtent.width, config::kWorkgroupSizeX),
                          ceil_div(region.imageExtent.height, config::kWorkgroupSizeY),
                          1);

            if (info.finalLayout != VK_IMAGE_LAYOUT_GENERAL) {
                vk::transitionImageSubresourceLayout(
                    info.commandBuffer,
                    info.dstImage,
                    info.destinationFormat,
                    VK_IMAGE_LAYOUT_GENERAL,
                    info.finalLayout,
                    region.imageSubresource.mipLevel,
                    1,
                    arrayLayer,
                    1);
            }
        }
    }

    return BcnResult::Success;
}

void BcnDecoderContext::releaseImageViews(VkImage image) {
    if (!impl_ || image == VK_NULL_HANDLE) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl_->inlineViewsMutex);
    for (auto it = impl_->inlineViews.begin(); it != impl_->inlineViews.end(); ) {
        if (it->first.image == image) {
            if (it->second != VK_NULL_HANDLE) {
                vkDestroyImageView(impl_->device, it->second, nullptr);
            }
            it = impl_->inlineViews.erase(it);
        } else {
            ++it;
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
