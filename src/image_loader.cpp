#include "image_loader.h"

#include <glad/gl.h>
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <iostream>

// ─── Global cache instance ────────────────────────────────────────────────────
ImageCache g_image_cache;

// ─── GPU upload helpers ───────────────────────────────────────────────────────

static GLuint upload_rgba8(const uint8_t* pixels, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static GLuint upload_rgba_f32(const float* pixels, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0,
                 GL_RGBA, GL_FLOAT, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ─── load_image ───────────────────────────────────────────────────────────────

bool load_image(const std::string& path, ImageEntry& entry) {
    entry = {};
    entry.path = path;

    // Check if HDR
    entry.is_hdr = (stbi_is_hdr(path.c_str()) != 0);

    int w = 0, h = 0, ch = 0;

    if (entry.is_hdr) {
        float* data = stbi_loadf(path.c_str(), &w, &h, &ch, 4);
        if (!data) {
            std::cerr << "[image_loader] failed to load HDR: "
                      << path << " — " << stbi_failure_reason() << "\n";
            return false;
        }
        entry.width    = w;
        entry.height   = h;
        entry.channels = 4;
        entry.pixels_f32.assign(data, data + static_cast<size_t>(w) * h * 4);
        stbi_image_free(data);
        entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), w, h);
    } else {
        // Force RGBA (4 channels)
        uint8_t* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!data) {
            std::cerr << "[image_loader] failed to load: "
                      << path << " — " << stbi_failure_reason() << "\n";
            return false;
        }
        entry.width    = w;
        entry.height   = h;
        entry.channels = 4;
        entry.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
        stbi_image_free(data);
        entry.texture_id = upload_rgba8(entry.pixels.data(), w, h);
    }

    if (entry.texture_id == 0) {
        std::cerr << "[image_loader] GPU upload failed for: " << path << "\n";
        return false;
    }

    entry.loaded = true;
    return true;
}

void free_image(ImageEntry& entry) {
    if (entry.texture_id) {
        glDeleteTextures(1, &entry.texture_id);
        entry.texture_id = 0;
    }
    entry.pixels    = {};
    entry.pixels_f32 = {};
    entry.loaded    = false;
}

// ─── ImageCache ───────────────────────────────────────────────────────────────

ImageEntry* ImageCache::get(const std::string& path) {
    // Search cache
    for (auto& ce : entries_) {
        if (ce.image.path == path) {
            ce.last_access = ++access_counter_;
            return &ce.image;
        }
    }

    // Not in cache — load it
    if (static_cast<int>(entries_.size()) >= MAX_ENTRIES) {
        evict_lru();
    }

    entries_.emplace_back();
    CacheEntry& ce = entries_.back();
    ce.last_access = ++access_counter_;

    if (!load_image(path, ce.image)) {
        entries_.pop_back();
        return nullptr;
    }

    return &ce.image;
}

void ImageCache::clear() {
    for (auto& ce : entries_) {
        free_image(ce.image);
    }
    entries_.clear();
    access_counter_ = 0;
}

void ImageCache::evict_lru() {
    if (entries_.empty()) return;
    auto it = std::min_element(entries_.begin(), entries_.end(),
        [](const CacheEntry& a, const CacheEntry& b) {
            return a.last_access < b.last_access;
        });
    free_image(it->image);
    entries_.erase(it);
}
