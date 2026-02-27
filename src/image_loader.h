#pragma once

#include "app.h"
#include <string>
#include <vector>
#include <cstdint>

// ─── Single-image load / free ─────────────────────────────────────────────────

// Load image from disk into entry (pixels + GPU texture).
// Returns true on success; entry.loaded is set accordingly.
bool load_image(const std::string& path, ImageEntry& entry);

// Free GPU texture and CPU pixel data for an entry.
void free_image(ImageEntry& entry);

// ─── LRU image cache ──────────────────────────────────────────────────────────
// Keeps the most-recently-used MAX_ENTRIES images in GPU memory.

class ImageCache {
public:
    static constexpr int MAX_ENTRIES = 8;

    ImageCache()  = default;
    ~ImageCache() { clear(); }

    // Returns pointer to a valid, loaded entry, or nullptr on failure.
    // The pointer remains valid until the next call that triggers eviction.
    ImageEntry* get(const std::string& path);

    // Remove all cached entries (frees GPU textures).
    void clear();

    // Number of currently cached entries.
    int size() const { return static_cast<int>(entries_.size()); }

private:
    struct CacheEntry {
        ImageEntry image;
        uint64_t   last_access = 0;
    };

    std::vector<CacheEntry> entries_;
    uint64_t                access_counter_ = 0;

    // Evict the least-recently-used entry.
    void evict_lru();
};

// Global cache used by the application.
extern ImageCache g_image_cache;
