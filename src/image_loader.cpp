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

// ─── Rotation helpers ─────────────────────────────────────────────────────────

void rotate_image_cw(ImageEntry& entry) {
    if (!entry.loaded) return;

    const int old_w = entry.width;
    const int old_h = entry.height;
    const int new_w = old_h;
    const int new_h = old_w;

    if (!entry.pixels.empty()) {
        std::vector<uint8_t> dst(static_cast<size_t>(new_w) * new_h * 4);
        const uint8_t* src = entry.pixels.data();
        for (int y = 0; y < old_h; ++y) {
            for (int x = 0; x < old_w; ++x) {
                // CW: new[x][old_h-1-y] = old[y][x]
                int dst_row = x;
                int dst_col = old_h - 1 - y;
                const uint8_t* sp = src + (y * old_w + x) * 4;
                uint8_t*       dp = dst.data() + (dst_row * new_w + dst_col) * 4;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        }
        entry.pixels = std::move(dst);
    }

    if (!entry.pixels_f32.empty()) {
        std::vector<float> dst(static_cast<size_t>(new_w) * new_h * 4);
        const float* src = entry.pixels_f32.data();
        for (int y = 0; y < old_h; ++y) {
            for (int x = 0; x < old_w; ++x) {
                int dst_row = x;
                int dst_col = old_h - 1 - y;
                const float* sp = src + (y * old_w + x) * 4;
                float*       dp = dst.data() + (dst_row * new_w + dst_col) * 4;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        }
        entry.pixels_f32 = std::move(dst);
    }

    entry.width  = new_w;
    entry.height = new_h;

    if (entry.texture_id) {
        glDeleteTextures(1, &entry.texture_id);
        entry.texture_id = 0;
    }
    if (!entry.pixels.empty())
        entry.texture_id = upload_rgba8(entry.pixels.data(), new_w, new_h);
    else if (!entry.pixels_f32.empty())
        entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), new_w, new_h);
}

void rotate_image_ccw(ImageEntry& entry) {
    if (!entry.loaded) return;

    const int old_w = entry.width;
    const int old_h = entry.height;
    const int new_w = old_h;
    const int new_h = old_w;

    if (!entry.pixels.empty()) {
        std::vector<uint8_t> dst(static_cast<size_t>(new_w) * new_h * 4);
        const uint8_t* src = entry.pixels.data();
        for (int y = 0; y < old_h; ++y) {
            for (int x = 0; x < old_w; ++x) {
                // CCW: new[old_w-1-x][y] = old[y][x]
                int dst_row = old_w - 1 - x;
                int dst_col = y;
                const uint8_t* sp = src + (y * old_w + x) * 4;
                uint8_t*       dp = dst.data() + (dst_row * new_w + dst_col) * 4;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        }
        entry.pixels = std::move(dst);
    }

    if (!entry.pixels_f32.empty()) {
        std::vector<float> dst(static_cast<size_t>(new_w) * new_h * 4);
        const float* src = entry.pixels_f32.data();
        for (int y = 0; y < old_h; ++y) {
            for (int x = 0; x < old_w; ++x) {
                int dst_row = old_w - 1 - x;
                int dst_col = y;
                const float* sp = src + (y * old_w + x) * 4;
                float*       dp = dst.data() + (dst_row * new_w + dst_col) * 4;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        }
        entry.pixels_f32 = std::move(dst);
    }

    entry.width  = new_w;
    entry.height = new_h;

    if (entry.texture_id) {
        glDeleteTextures(1, &entry.texture_id);
        entry.texture_id = 0;
    }
    if (!entry.pixels.empty())
        entry.texture_id = upload_rgba8(entry.pixels.data(), new_w, new_h);
    else if (!entry.pixels_f32.empty())
        entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), new_w, new_h);
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

// ─── Directory image sequence scanning ────────────────────────────────────────

#include <filesystem>
#include <cctype>
#include <string>
#include <vector>

// Natural sort 비교: 숫자 부분을 수치로 비교
static int natural_compare(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
            // 숫자 부분 파싱
            size_t ni = i, nj = j;
            while (ni < a.size() && std::isdigit((unsigned char)a[ni])) ++ni;
            while (nj < b.size() && std::isdigit((unsigned char)b[nj])) ++nj;
            // 앞 0 제거
            size_t si = i, sj = j;
            while (si < ni - 1 && a[si] == '0') ++si;
            while (sj < nj - 1 && b[sj] == '0') ++sj;
            size_t la2 = ni - si, lb2 = nj - sj;
            if (la2 != lb2) return (la2 < lb2) ? -1 : 1;
            int cmp = a.substr(si, la2).compare(b.substr(sj, lb2));
            if (cmp != 0) return cmp;
            i = ni; j = nj;
        } else {
            char ca = std::tolower((unsigned char)a[i]);
            char cb = std::tolower((unsigned char)b[j]);
            if (ca != cb) return (ca < cb) ? -1 : 1;
            ++i; ++j;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

static bool is_image_ext(const std::string& ext) {
    for (int k = 0; SUPPORTED_IMG_EXTS[k]; ++k) {
        if (ext == SUPPORTED_IMG_EXTS[k]) return true;
    }
    return false;
}

std::vector<std::string> scan_image_directory(const std::string& current_path,
                                               int& current_out)
{
    current_out = -1;
    if (current_path.empty()) return {};

    namespace fs = std::filesystem;
    fs::path p(current_path);
    fs::path dir = p.parent_path();
    if (dir.empty()) dir = ".";

    std::vector<std::string> result;
    std::string canonical_cur;
    try {
        canonical_cur = fs::canonical(p).string();
    } catch (...) {
        canonical_cur = current_path;
    }

    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            // 소문자로 변환
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (!is_image_ext(ext)) continue;
            result.push_back(entry.path().string());
        }
    } catch (...) {
        return {};
    }

    // Natural sort
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return natural_compare(a, b) < 0;
    });

    // 현재 파일 인덱스 찾기
    for (int i = 0; i < (int)result.size(); ++i) {
        std::string can;
        try { can = fs::canonical(result[i]).string(); } catch (...) { can = result[i]; }
        if (can == canonical_cur) { current_out = i; break; }
    }

    return result;
}
