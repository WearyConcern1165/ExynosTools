#include "texture_cache.h"
#include <algorithm>

namespace bcn {

TextureCache::TextureCache(uint64_t budgetBytes)
    : budgetBytes_(budgetBytes) {}

TextureCache::~TextureCache() {
    flush();
}

// ---------------------------------------------------------------------------
CachedTexture* TextureCache::find(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = keyMap_.find(key);
    if (it == keyMap_.end()) return nullptr;

    // Promote to MRU (front)
    lruList_.splice(lruList_.begin(), lruList_, it->second);
    return &(*it->second);
}

// ---------------------------------------------------------------------------
BcnTextureHandle TextureCache::insert(const std::string& key,
                                      vk::ImageWrapper&& image,
                                      BcnSupportPath      pathUsed,
                                      uint64_t            memorySizeBytes)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Evict until we have room
    while (budgetBytes_ > 0 &&
           currentUsageBytes_ + memorySizeBytes > budgetBytes_ &&
           !lruList_.empty()) {
        evictLRU();
    }

    uint64_t id = nextId();
    BcnTextureHandle handle{ id };

    lruList_.emplace_front(CachedTexture{
        handle,
        std::move(image),
        pathUsed,
        memorySizeBytes,
        key
    });

    auto listIt = lruList_.begin();
    keyMap_[key]   = listIt;
    handleMap_[id] = listIt;
    currentUsageBytes_ += memorySizeBytes;

    return handle;
}

// ---------------------------------------------------------------------------
const CachedTexture* TextureCache::get(BcnTextureHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handleMap_.find(handle.id);
    if (it == handleMap_.end()) return nullptr;
    return &(*it->second);
}

// ---------------------------------------------------------------------------
void TextureCache::remove(BcnTextureHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handleMap_.find(handle.id);
    if (it == handleMap_.end()) return;

    auto listIt = it->second;
    currentUsageBytes_ -= listIt->memorySizeBytes;
    keyMap_.erase(listIt->cacheKey);
    handleMap_.erase(it);
    listIt->image.destroy();
    lruList_.erase(listIt);
}

// ---------------------------------------------------------------------------
void TextureCache::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : lruList_) {
        entry.image.destroy();
    }
    lruList_.clear();
    keyMap_.clear();
    handleMap_.clear();
    currentUsageBytes_ = 0;
}

// ---------------------------------------------------------------------------
void TextureCache::evictLRU() {
    // Must be called with mutex held.
    if (lruList_.empty()) return;

    auto& back = lruList_.back();
    currentUsageBytes_ -= back.memorySizeBytes;
    keyMap_.erase(back.cacheKey);
    handleMap_.erase(back.handle.id);
    back.image.destroy();
    lruList_.pop_back();
}

// ---------------------------------------------------------------------------
uint64_t TextureCache::nextId() {
    return ++idCounter_;
}

} // namespace bcn
