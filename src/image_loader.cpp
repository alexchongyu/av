#include "image_loader.h"
#include "render_backend.h"

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

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

// ─── Software-mode SDL_Texture upload ─────────────────────────────────────────

static uintptr_t upload_sdl_texture(const uint8_t* pixels, int w, int h) {
    SDL_Renderer* renderer = g_render_ctx.sdl_renderer;
    if (!renderer) return 0;

    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32,
                                               const_cast<uint8_t*>(pixels),
                                               w * 4);
    if (!surf) return 0;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) return 0;

    return reinterpret_cast<uintptr_t>(tex);
}

// ─── PPM ASCII (P2/P3) parser ─────────────────────────────────────────────────

// Skip whitespace and '#' comment lines in PPM header
static void ppm_skip_ws_comments(std::ifstream& f) {
    while (f.good()) {
        int c = f.peek();
        if (c == '#') {
            std::string dummy;
            std::getline(f, dummy);
        } else if (std::isspace(c)) {
            f.get();
        } else {
            break;
        }
    }
}

static bool try_load_ppm_ascii(const std::string& path, ImageEntry& entry) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // Read magic number
    char magic[3] = {};
    f.read(magic, 2);
    if (!f.good()) return false;

    bool is_p2 = (magic[0] == 'P' && magic[1] == '2');  // grayscale ASCII
    bool is_p3 = (magic[0] == 'P' && magic[1] == '3');  // RGB ASCII
    if (!is_p2 && !is_p3) return false;

    int w = 0, h = 0, maxval = 0;
    ppm_skip_ws_comments(f);
    f >> w;
    ppm_skip_ws_comments(f);
    f >> h;
    ppm_skip_ws_comments(f);
    f >> maxval;

    if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 65535) {
        std::cerr << "[image_loader] invalid PPM ASCII header: "
                  << path << " (w=" << w << " h=" << h << " maxval=" << maxval << ")\n";
        return false;
    }

    size_t npixels = static_cast<size_t>(w) * h;
    int channels = is_p3 ? 3 : 1;
    size_t nvalues = npixels * channels;

    // Read all pixel values
    std::vector<uint16_t> raw(nvalues);
    for (size_t i = 0; i < nvalues; ++i) {
        int val = 0;
        f >> val;
        if (f.fail()) {
            std::cerr << "[image_loader] PPM ASCII: premature end of data at value "
                      << i << "/" << nvalues << " in " << path << "\n";
            return false;
        }
        raw[i] = static_cast<uint16_t>(std::clamp(val, 0, maxval));
    }

    // Build pixels_orig (always RGB 3ch)
    entry.pixels_orig.resize(npixels * 3);
    if (is_p3) {
        entry.pixels_orig = std::move(raw);
    } else {
        // P2: grayscale -> RGB replicate
        for (size_t i = 0; i < npixels; ++i) {
            entry.pixels_orig[i * 3 + 0] = raw[i];
            entry.pixels_orig[i * 3 + 1] = raw[i];
            entry.pixels_orig[i * 3 + 2] = raw[i];
        }
    }
    entry.ppm_maxval = maxval;

    // Build 8-bit RGBA for display
    entry.pixels.resize(npixels * 4);
    for (size_t i = 0; i < npixels; ++i) {
        uint16_t r = entry.pixels_orig[i * 3 + 0];
        uint16_t g = entry.pixels_orig[i * 3 + 1];
        uint16_t b = entry.pixels_orig[i * 3 + 2];
        entry.pixels[i * 4 + 0] = static_cast<uint8_t>(r * 255 / maxval);
        entry.pixels[i * 4 + 1] = static_cast<uint8_t>(g * 255 / maxval);
        entry.pixels[i * 4 + 2] = static_cast<uint8_t>(b * 255 / maxval);
        entry.pixels[i * 4 + 3] = 255;
    }

    entry.width    = w;
    entry.height   = h;
    entry.channels = 4;

    // maxval > 255인 경우 float32 RGBA 버퍼도 생성 (정밀 diff용)
    if (maxval > 255) {
        entry.pixels_f32.resize(npixels * 4);
        float inv_max = 1.0f / static_cast<float>(maxval);
        for (size_t i = 0; i < npixels; ++i) {
            entry.pixels_f32[i * 4 + 0] = entry.pixels_orig[i * 3 + 0] * inv_max;
            entry.pixels_f32[i * 4 + 1] = entry.pixels_orig[i * 3 + 1] * inv_max;
            entry.pixels_f32[i * 4 + 2] = entry.pixels_orig[i * 3 + 2] * inv_max;
            entry.pixels_f32[i * 4 + 3] = 1.0f;
        }
    }

    // Upload texture
    if (is_software_mode()) {
        entry.texture_id = upload_sdl_texture(entry.pixels.data(), w, h);
    } else if (!entry.pixels_f32.empty()) {
        entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), w, h);
    } else {
        entry.texture_id = upload_rgba8(entry.pixels.data(), w, h);
    }

    if (entry.texture_id == 0) {
        std::cerr << "[image_loader] texture upload failed for PPM ASCII: " << path << "\n";
        return false;
    }

    entry.loaded = true;
    return true;
}

// ─── PPM Binary (P5/P6) parser ───────────────────────────────────────────────

static bool try_load_ppm_binary(const std::string& path, ImageEntry& entry) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // Read magic number
    char magic[3] = {};
    f.read(magic, 2);
    if (!f.good()) return false;

    bool is_p5 = (magic[0] == 'P' && magic[1] == '5');  // grayscale binary
    bool is_p6 = (magic[0] == 'P' && magic[1] == '6');  // RGB binary
    if (!is_p5 && !is_p6) return false;

    int w = 0, h = 0, maxval = 0;
    ppm_skip_ws_comments(f);
    f >> w;
    ppm_skip_ws_comments(f);
    f >> h;
    ppm_skip_ws_comments(f);
    f >> maxval;

    if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 65535) {
        std::cerr << "[image_loader] invalid PPM binary header: "
                  << path << " (w=" << w << " h=" << h << " maxval=" << maxval << ")\n";
        return false;
    }

    // Skip exactly one whitespace character after maxval (per PPM spec)
    f.get();

    size_t npixels = static_cast<size_t>(w) * h;
    int channels = is_p6 ? 3 : 1;
    int bytes_per_channel = (maxval > 255) ? 2 : 1;
    size_t data_size = npixels * channels * bytes_per_channel;

    // Read all raw pixel data at once
    std::vector<uint8_t> rawbuf(data_size);
    f.read(reinterpret_cast<char*>(rawbuf.data()), static_cast<std::streamsize>(data_size));
    if (f.gcount() != static_cast<std::streamsize>(data_size)) {
        std::cerr << "[image_loader] PPM binary: premature end of data in " << path
                  << " (expected " << data_size << " bytes, got " << f.gcount() << ")\n";
        return false;
    }

    // Convert raw bytes to uint16 values
    size_t nvalues = npixels * channels;
    std::vector<uint16_t> raw(nvalues);
    if (bytes_per_channel == 1) {
        for (size_t i = 0; i < nvalues; ++i)
            raw[i] = rawbuf[i];
    } else {
        // 16-bit big-endian
        for (size_t i = 0; i < nvalues; ++i)
            raw[i] = static_cast<uint16_t>((rawbuf[i * 2] << 8) | rawbuf[i * 2 + 1]);
    }

    // Build pixels_orig (always RGB 3ch)
    entry.pixels_orig.resize(npixels * 3);
    if (is_p6) {
        entry.pixels_orig = std::move(raw);
    } else {
        // P5: grayscale -> RGB replicate
        for (size_t i = 0; i < npixels; ++i) {
            entry.pixels_orig[i * 3 + 0] = raw[i];
            entry.pixels_orig[i * 3 + 1] = raw[i];
            entry.pixels_orig[i * 3 + 2] = raw[i];
        }
    }
    entry.ppm_maxval = maxval;

    // Build 8-bit RGBA for display
    entry.pixels.resize(npixels * 4);
    for (size_t i = 0; i < npixels; ++i) {
        uint16_t r = entry.pixels_orig[i * 3 + 0];
        uint16_t g = entry.pixels_orig[i * 3 + 1];
        uint16_t b = entry.pixels_orig[i * 3 + 2];
        entry.pixels[i * 4 + 0] = static_cast<uint8_t>(r * 255 / maxval);
        entry.pixels[i * 4 + 1] = static_cast<uint8_t>(g * 255 / maxval);
        entry.pixels[i * 4 + 2] = static_cast<uint8_t>(b * 255 / maxval);
        entry.pixels[i * 4 + 3] = 255;
    }

    entry.width    = w;
    entry.height   = h;
    entry.channels = 4;

    // maxval > 255인 경우 float32 RGBA 버퍼도 생성 (정밀 diff용)
    if (maxval > 255) {
        entry.pixels_f32.resize(npixels * 4);
        float inv_max = 1.0f / static_cast<float>(maxval);
        for (size_t i = 0; i < npixels; ++i) {
            entry.pixels_f32[i * 4 + 0] = entry.pixels_orig[i * 3 + 0] * inv_max;
            entry.pixels_f32[i * 4 + 1] = entry.pixels_orig[i * 3 + 1] * inv_max;
            entry.pixels_f32[i * 4 + 2] = entry.pixels_orig[i * 3 + 2] * inv_max;
            entry.pixels_f32[i * 4 + 3] = 1.0f;
        }
    }

    // Upload texture
    if (is_software_mode()) {
        entry.texture_id = upload_sdl_texture(entry.pixels.data(), w, h);
    } else if (!entry.pixels_f32.empty()) {
        entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), w, h);
    } else {
        entry.texture_id = upload_rgba8(entry.pixels.data(), w, h);
    }

    if (entry.texture_id == 0) {
        std::cerr << "[image_loader] texture upload failed for PPM binary: " << path << "\n";
        return false;
    }

    entry.loaded = true;
    return true;
}

// ─── load_image ───────────────────────────────────────────────────────────────

bool load_image(const std::string& path, ImageEntry& entry) {
    entry = {};
    entry.path = path;

    // PPM 먼저 시도 (P2/P3 ASCII, P5/P6 binary)
    if (try_load_ppm_ascii(path, entry)) {
        return true;
    }
    if (try_load_ppm_binary(path, entry)) {
        return true;
    }

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

        if (is_software_mode()) {
            // Convert float32 to uint8 for SDL texture
            std::vector<uint8_t> ldr(static_cast<size_t>(w) * h * 4);
            for (size_t i = 0; i < ldr.size(); ++i) {
                float v = std::clamp(entry.pixels_f32[i], 0.0f, 1.0f);
                ldr[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
            }
            entry.pixels = std::move(ldr);
            entry.texture_id = upload_sdl_texture(entry.pixels.data(), w, h);
        } else {
            entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), w, h);
        }
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

        if (is_software_mode()) {
            entry.texture_id = upload_sdl_texture(entry.pixels.data(), w, h);
        } else {
            entry.texture_id = upload_rgba8(entry.pixels.data(), w, h);
        }
    }

    if (entry.texture_id == 0) {
        std::cerr << "[image_loader] texture upload failed for: " << path << "\n";
        return false;
    }

    entry.loaded = true;
    return true;
}

void free_image(ImageEntry& entry) {
    if (entry.texture_id) {
        if (is_software_mode()) {
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(entry.texture_id));
        } else {
            GLuint gl_id = static_cast<GLuint>(entry.texture_id);
            glDeleteTextures(1, &gl_id);
        }
        entry.texture_id = 0;
    }
    entry.pixels     = {};
    entry.pixels_f32 = {};
    entry.pixels_orig = {};
    entry.ppm_maxval = 0;
    entry.loaded     = false;
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

    if (!entry.pixels_orig.empty()) {
        std::vector<uint16_t> dst(static_cast<size_t>(new_w) * new_h * 3);
        const uint16_t* src = entry.pixels_orig.data();
        for (int y = 0; y < old_h; ++y) {
            for (int x = 0; x < old_w; ++x) {
                int dst_row = x;
                int dst_col = old_h - 1 - y;
                const uint16_t* sp = src + (y * old_w + x) * 3;
                uint16_t*       dp = dst.data() + (dst_row * new_w + dst_col) * 3;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
            }
        }
        entry.pixels_orig = std::move(dst);
    }

    entry.width  = new_w;
    entry.height = new_h;

    if (entry.texture_id) {
        if (is_software_mode())
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(entry.texture_id));
        else {
            GLuint gl_id = static_cast<GLuint>(entry.texture_id);
            glDeleteTextures(1, &gl_id);
        }
        entry.texture_id = 0;
    }
    if (is_software_mode()) {
        if (!entry.pixels.empty())
            entry.texture_id = upload_sdl_texture(entry.pixels.data(), new_w, new_h);
    } else {
        if (!entry.pixels.empty())
            entry.texture_id = upload_rgba8(entry.pixels.data(), new_w, new_h);
        else if (!entry.pixels_f32.empty())
            entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), new_w, new_h);
    }
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

    if (!entry.pixels_orig.empty()) {
        std::vector<uint16_t> dst(static_cast<size_t>(new_w) * new_h * 3);
        const uint16_t* src = entry.pixels_orig.data();
        for (int y = 0; y < old_h; ++y) {
            for (int x = 0; x < old_w; ++x) {
                int dst_row = old_w - 1 - x;
                int dst_col = y;
                const uint16_t* sp = src + (y * old_w + x) * 3;
                uint16_t*       dp = dst.data() + (dst_row * new_w + dst_col) * 3;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
            }
        }
        entry.pixels_orig = std::move(dst);
    }

    entry.width  = new_w;
    entry.height = new_h;

    if (entry.texture_id) {
        if (is_software_mode())
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(entry.texture_id));
        else {
            GLuint gl_id = static_cast<GLuint>(entry.texture_id);
            glDeleteTextures(1, &gl_id);
        }
        entry.texture_id = 0;
    }
    if (is_software_mode()) {
        if (!entry.pixels.empty())
            entry.texture_id = upload_sdl_texture(entry.pixels.data(), new_w, new_h);
    } else {
        if (!entry.pixels.empty())
            entry.texture_id = upload_rgba8(entry.pixels.data(), new_w, new_h);
        else if (!entry.pixels_f32.empty())
            entry.texture_id = upload_rgba_f32(entry.pixels_f32.data(), new_w, new_h);
    }
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
