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

// Rotate image 90 degrees clockwise / counter-clockwise in place.
// Updates entry.width, entry.height, pixel data, and GPU texture.
void rotate_image_cw(ImageEntry& entry);
void rotate_image_ccw(ImageEntry& entry);

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
        int64_t    mtime_ticks = 0;   // file last-write-time ticks; detects on-disk changes
        uint64_t   fsize       = 0;   // file size; with mtime, invalidates a stale cached copy
    };

    std::vector<CacheEntry> entries_;
    uint64_t                access_counter_ = 0;

    // Evict the least-recently-used entry.
    void evict_lru();
};

// Global cache used by the application.
extern ImageCache g_image_cache;

// ─── Directory sequence scanning ─────────────────────────────────────────────

// 지원 이미지 확장자 목록 (소문자)
static const char* const SUPPORTED_IMG_EXTS[] = {
    ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".pgm", ".ppm", ".hdr", nullptr
};

// 경로에서 디렉토리 내 이미지 파일 목록을 natural sort로 반환.
// current_path와 동일 파일의 인덱스를 current_out에 설정.
std::vector<std::string> scan_image_directory(const std::string& current_path,
                                               int& current_out);

// AppState forward declaration (avoid circular include with app.h).
struct AppState;

// 통합 로드 헬퍼: free + load + scan_image_directory + viewport/diff 상태 갱신 +
// 파일명 토스트 설정. CLI / drag-drop / open dialog / 시퀀스 네비게이션 모든
// 경로에서 공용으로 사용.
// 반환: 로드 성공 여부.
bool load_image_and_populate_sequence(AppState& state,
                                      int panel,   // 0=A, 1=B (data slot, not visual)
                                      const std::string& path);
