#pragma once
/// @file texture_cache.h
/// @brief LRU cache for decoded textures with configurable memory budget.

#include "bcn_decoder/bcn_types.h"
#include "../vulkan/vk_image.h"

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <mutex>

namespace bcn {

/// Stores a decoded texture along with metadata for cache management.
struct CachedTexture {
    BcnTextureHandle     handle;
    vk::ImageWrapper     image;            ///< Owns the GPU resources
    BcnSupportPath       pathUsed;
    uint64_t             memorySizeBytes;   ///< Approximate GPU memory used
    std::string          cacheKey;
};

/// LRU texture cache with a configurable memory budget.
/// Thread-safe: all public methods are protected by a mutex.
class TextureCache {
public:
    explicit TextureCache(uint64_t budgetBytes);
    ~TextureCache();

    /// Try to find a cached texture by key.
    /// Returns null if not found.  On hit, the entry is promoted to MRU.
    CachedTexture* find(const std::string& key);

    /// Insert a texture into the cache.  If the budget is exceeded, the LRU
    /// entry is evicted.  Takes ownership of the ImageWrapper inside entry.
    /// Returns the assigned BcnTextureHandle.
    BcnTextureHandle insert(const std::string& key,
                            vk::ImageWrapper&& image,
                            BcnSupportPath      pathUsed,
                            uint64_t            memorySizeBytes);

    /// Retrieve a cached texture by handle.  Returns null if not found.
    const CachedTexture* get(BcnTextureHandle handle) const;

    /// Remove and destroy a specific texture.
    void remove(BcnTextureHandle handle);

    /// Evict all entries.
    void flush();

    uint64_t usageBytes() const { return currentUsageBytes_; }
    uint64_t budgetBytes() const { return budgetBytes_; }

private:
    void evictLRU();
    uint64_t nextId();

    uint64_t budgetBytes_;
    uint64_t currentUsageBytes_ = 0;
    uint64_t idCounter_ = 0;

    // LRU list: front = MRU, back = LRU
    using LruList  = std::list<CachedTexture>;
    using LruIter  = LruList::iterator;

    LruList                                        lruList_;
    std::unordered_map<std::string, LruIter>       keyMap_;    // cacheKey → iter
    std::unordered_map<uint64_t, LruIter>          handleMap_; // handle.id → iter

    mutable std::mutex mutex_;
};

} // namespace bcn
