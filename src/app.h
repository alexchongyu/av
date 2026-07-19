#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>

struct ImFont;       // forward declaration — full type in imgui.h
struct SDL_Window;   // forward declaration — full type in SDL3/SDL.h

// ─── Core data types ──────────────────────────────────────────────────────────

struct ImageEntry {
    std::string          path;
    std::vector<uint8_t> pixels;      // CPU-side RGBA8 (or empty if freed)
    std::vector<float>   pixels_f32;  // CPU-side RGBA32F for HDR
    std::vector<uint16_t> pixels_orig; // 원본 RGB (3ch, uint16), PPM P2/P3/P5/P6 전용
    int   width    = 0;
    int   height   = 0;
    int   channels = 0;
    int   ppm_maxval = 0;              // PPM maxval (0=PPM 아님, 255=8bit, 1023=10bit 등)
    uintptr_t texture_id = 0;          // GL texture handle or SDL_Texture*
    bool  loaded   = false;
    bool  is_hdr   = false;
    uint64_t content_version = 0;      // bumped on (re)load/rotate; cache-invalidation key
};

struct ViewportState {
    float zoom  = 1.0f;   // display scale: 0.1 ~ 32.0
    float pan_x = 0.0f;   // image-pixel offset from center (+right)
    float pan_y = 0.0f;   // image-pixel offset from center (+down)
    bool  fit   = true;   // fit-to-window mode
};

enum class ChannelMode { RGB = 0, Red = 1, Green = 2, Blue = 3 };
enum class PixelFormat { Decimal = 0, Hex0x = 1, HexH = 2 };

struct DiffState {
    enum class Mode {
        None,
        PixelAbsolute,
        PixelRelative,
        Highlight,
        FalseColor,
        SSIM,
        Enhance,      // Ctrl+5: [min,max] → [128,255] remap + auto magnifier
        AlphaBlend    // Ctrl+2: diff 패널에 A·B 알파블렌딩
    };
    Mode  mode           = Mode::None;
    float amplify        = 1.0f;       // diff amplification factor
    float alpha          = 0.5f;       // AlphaBlend 비율 (0=A만, 1=B만)
    float ssim_score     = -1.0f;      // -1 = not computed
    uintptr_t ssim_texture_id = 0;     // SSIM heatmap texture (GLuint or SDL_Texture*)
    bool  ssim_computing = false;
    std::vector<uint8_t> ssim_pixels;  // RGBA8 falsecolor heatmap for software mode
    int   ssim_w = 0, ssim_h = 0;     // heatmap dimensions

    // PSNR cache (auto-computed when diff mode active + both images loaded)
    float psnr_db       = -1.0f;      // overall PSNR (avg of channels), -1 = not computed
    bool  psnr_computed  = false;

    // Tolerance-based diff highlighting
    int   threshold              = 0;   // 0=disabled; diff > threshold => highlight
    int   threshold_exceed_count = 0;   // pixels exceeding threshold (cache)
    int   threshold_total_count  = 0;   // total compared pixels

    // Enhance mode: global min/max of non-zero diff (0-255 scale, all channels)
    int   enhance_min            = 0;
    int   enhance_max            = 0;
    bool  enhance_range_computed = false;
};

// ─── Diff-mode descriptor: single source of truth ─────────────────────────────
// Keeps the Diff menu, statusbar tag, window-title tag and --diff-mode CLI in
// sync. Add a new mode here once and every presentation site picks it up.
struct DiffModeInfo {
    DiffState::Mode mode;
    const char*     menu_label;  // Diff menu text     ("Off", "Alpha Blend", …)
    const char*     short_tag;   // statusbar/title tag ("None", "Abs", …)
    const char*     hotkey;      // menu shortcut label ("Ctrl+2", …)
    const char*     cli_token;   // --diff-mode token   (nullptr = not CLI-settable)
};

// Order = Diff menu order. Hotkeys mirror handle_keyboard() in app.cpp.
inline constexpr DiffModeInfo kDiffModes[] = {
    { DiffState::Mode::None,          "Off",         "None",       "Ctrl+D", "none"       },
    { DiffState::Mode::AlphaBlend,    "Alpha Blend", "AlphaBlend", "Ctrl+2", "alphablend" },
    { DiffState::Mode::PixelAbsolute, "Absolute",    "Abs",        "Ctrl+3", "abs"        },
    { DiffState::Mode::PixelRelative, "Relative",    "Rel",        "Ctrl+4", "rel"        },
    { DiffState::Mode::Enhance,       "Enhance",     "Enhance",    "Ctrl+5", "enhance"    },
    { DiffState::Mode::FalseColor,    "FalseColor",  "FalseColor", "Ctrl+6", "falsecolor" },
    { DiffState::Mode::SSIM,          "SSIM",        "SSIM",       "Ctrl+7", "ssim"       },
    { DiffState::Mode::Highlight,     "Highlight",   "Highlight",  "Ctrl+9", "highlight"  },
};

// Short tag for statusbar/title; "?" if somehow unmapped.
inline const char* diff_short_tag(DiffState::Mode m) {
    for (const auto& d : kDiffModes)
        if (d.mode == m) return d.short_tag;
    return "?";
}

struct DiffPixelInfo {
    int x, y;           // 이미지 좌표
    int ar, ag, ab;     // Image A 픽셀값 (원본 단위: 8bit=0~255, 10bit=0~1023 등)
    int br, bg, bb;     // Image B 픽셀값
    int dr, dg, db;     // |A-B| 차이값
};

struct DiffListingState {
    bool show      = false;
    bool computed   = false;
    bool identical  = false;
    bool identical_r = false;  // all dr == 0
    bool identical_g = false;  // all dg == 0
    bool identical_b = false;  // all db == 0
    std::vector<DiffPixelInfo> pixels;  // 차이 픽셀 목록
    int  start_from = 0;                // 표시 시작 인덱스 (0-based)
    int  per_page   = 50;               // 페이지당 표시 개수
    int  goto_num   = 1;                // Go to pixel # 입력값 (1-based)
    int  list_from_num = 1;             // List from # 입력값 (1-based)
    // 캐시 무효화용
    int  cache_img_a_w = -1, cache_img_a_h = -1;
    int  cache_img_b_w = -1, cache_img_b_h = -1;
    uint64_t cache_ver_a = ~0ull, cache_ver_b = ~0ull;  // content-version key: catches same-size content changes

    void reset() {
        show = false; computed = false; identical = false;
        identical_r = false; identical_g = false; identical_b = false;
        pixels.clear(); start_from = 0; goto_num = 1; list_from_num = 1;
        cache_img_a_w = cache_img_a_h = -1;
        cache_img_b_w = cache_img_b_h = -1;
        cache_ver_a = cache_ver_b = ~0ull;
    }
};

struct SaveItemState {
    bool checked = true;
    char path[512] = {};
};

struct ImageSaveDialog {
    enum class Format { PNG, BMP, PPM };
    Format format = Format::PNG;
    Format prev_format = Format::PNG;  // for extension auto-update

    enum class PpmMode { Binary, ASCII };
    PpmMode ppm_mode = PpmMode::Binary;
    int     ppm_bits = 8;   // 8, 10, 12, or 16

    enum class Target { None, ImageA, ImageB, Diff };

    SaveItemState items[3];   // 0=A, 1=B, 2=Diff
    bool initialized = false;
    std::string status_msg;
    bool status_error = false;
};

struct ChartSaveDialog {
    int  export_width      = 1920;
    int  export_height     = 1080;
    bool separate_channels = false;

    SaveItemState png_items[3];   // 0=A, 1=B, 2=Diff
    SaveItemState csv_items[3];
    bool initialized      = false;
    bool init_was_histogram = false;
    bool init_was_hcut      = false;
    std::string status_msg;
    bool status_error = false;
};

struct StatsSaveDialog {
    SaveItemState items[3];   // 0=A, 1=B, 2=Diff
    bool initialized = false;
    std::string status_msg;
    bool status_error = false;
};

struct OpenDialogState {
    bool  show         = false;   // Shift+Cmd+O toggle
    std::string opened_path;      // set by SDL dialog callback
    int   open_target  = -1;      // 0=A, 1=B
    bool  open_pending = false;   // waiting for main loop to handle
    bool  clear_other  = false;   // remove other slot when loading single image
};

struct ContextSaveState {
    bool        save_pending = false;
    std::string save_path;
    int         save_target = -1;  // 0=A, 1=B, 2=Diff
};

// ─── ROI (Region of Interest) state ──────────────────────────────────────────

struct RoiState {
    bool active  = false;   // Ctrl+E: ROI 선택 모드 ON/OFF
    bool has_roi = false;   // 유효한 ROI가 선택됨
    int  x = 0, y = 0;     // 이미지 픽셀 좌표 (top-left)
    int  w = 0, h = 0;     // ROI 크기 (픽셀)
    int  panel_idx = 0;    // ROI가 그려진 패널 인덱스

    // 드래그 중 임시 상태
    bool  dragging  = false;
    float drag_sx   = 0.0f; // 드래그 시작 화면 좌표
    float drag_sy   = 0.0f;
    int   drag_panel = 0;
};

// ─── Image Sequence Navigation state ─────────────────────────────────────────

struct SequenceState {
    std::vector<std::string> files;    // 디렉토리 내 이미지 파일 목록 (정렬됨)
    int                      current_index = -1;  // 현재 파일 인덱스 (-1=시퀀스 없음)
};

// 로드된 파일명을 상단 중앙에 잠시 표시하는 토스트 상태.
struct FilenameToastState {
    std::string filename;   // basename만 (비어있으면 비활성)
    int         panel = 0;  // 0=A, 1=B
    double      until = 0.0; // ImGui::GetTime() 기반 만료 시각
    bool        failed = false; // load 실패 시 "(failed)" 접두사 표시
};

// ─── Overlay / Blend comparison state ────────────────────────────────────────

struct OverlayState {
    bool  active    = false;   // O 키: Overlay 모드 ON/OFF
    float alpha     = 0.5f;   // 0=A만, 1=B만
    enum class Mode { Blend, Curtain } mode = Mode::Blend;
    float curtain_x = 0.5f;   // Curtain 모드: 구분선 위치 [0,1] (화면 비율)
    bool  curtain_dragging = false;
};


struct CliOptions {
    std::string     image_a;
    std::string     image_b;
    DiffState::Mode diff_mode    = DiffState::Mode::None;
    float           zoom         = 0.0f;    // 0 = fit
    bool            sync         = true;
    bool            pair         = false;   // --pair: 두 dir에서 같은 파일명 짝지어 비교
    float           amplify      = 1.0f;
    bool            fullscreen   = false;
    int             win_w        = 1280;
    int             win_h        = 720;
    std::string     icc_profile;
    bool            no_color_mgmt = false;
    bool            software     = false;  // --software: force SDL software renderer
    bool            windowed     = false;  // --windowed: start with title bar + resizable
    bool            no_border    = false;  // -nb: start with panel borders hidden
    int             pan_step     = 32;     // Shift+hjkl jump size in image-pixels
    // Border colors for A / B / Diff panels (ImGui ABGR uint32, alpha=230)
    // Defaults: magenta / yellow / cyan
    std::array<uint32_t, 3> border_colors = {0xE6FF00FFu, 0xE600FFFFu, 0xE6FFFF00u};
};

struct AppState {
    std::array<ImageEntry, 2>    images;        // [0]=left/A, [1]=right/B
    std::array<ViewportState, 2> views;
    DiffState  diff;
    uint64_t content_version_counter = 0;  // monotonic source for ImageEntry.content_version
    bool  sync_viewports  = true;
    bool  show_ui         = false;   // U key: toggle menu/statusbar overlay
    bool  show_histogram  = false;
    bool  show_hline_cut  = false;   // Ctrl+L: 수평 라인 컷뷰
    bool  show_vline_cut  = false;   // Ctrl+Y: 수직 라인 컷뷰
    bool  show_stats      = false;   // Ctrl+S: Image Statistics window
    bool  show_info       = false;
    bool  show_pixel_info = false;   // V key: cursor pixel balloon
    bool  show_hotkey_help = false;  // Ctrl+Shift+H: hotkey reference window
    int   pathfinder_mode = 1;      // 0=hidden, 1=image (P), 2=schematic (Ctrl+P)
    bool  swap_images     = false;   // A↔B quick-swap toggle
    int   active_panel    = 0;       // 0 or 1 (keyboard focus)
    bool  quit            = false;
    float zoom_hud_timer  = 0.0f;   // remaining display time (seconds)
    float last_zoom_a     = 1.0f;   // previous frame zoom (change detection)
    float last_zoom_b     = 1.0f;
    ChannelMode channel_mode = ChannelMode::RGB;
    int   pan_step        = 32;       // Shift+hjkl jump size in image-pixels
    // Border colors for A / B / Diff panels (ImGui ABGR uint32)
    std::array<uint32_t, 3> border_colors = {0xE6FF00FFu, 0xE600FFFFu, 0xE6FFFF00u};
    ImFont* font_large  = nullptr;    // Roboto-Medium 26px (Image Info & Pixel Balloon)
    ImFont* font_medium = nullptr;    // Roboto-Medium 18px (Save/Open dialog)
    CliOptions cli;
    bool  show_save_dialog = false;   // Shift+Cmd/Ctrl+S: context-aware Save window
    ImageSaveDialog  image_save;
    ChartSaveDialog  chart_save;
    StatsSaveDialog  stats_save;
    OpenDialogState  open_state;
    SDL_Window* window = nullptr;     // main SDL window (for file dialogs)

    // ─── New feature state ────────────────────────────────────────────────────
    RoiState     roi;                   // ROI 선택 상태
    bool         show_roi_stats = false; // ROI 통계 창 표시
    SequenceState sequences[2];         // [0]=A, [1]=B 시퀀스 탐색

    // ─── Feature: --pair (두 디렉토리에서 같은 파일명 짝지어 로드) ───────────
    bool        pair_mode = false;       // A(왼쪽)=기준 dir가 시퀀스 구동, B(오른쪽)=비교 dir
    std::string pair_dir_b;              // 비교 디렉토리 B (고정)
    std::string panel_missing_msg[2];    // 매칭 영상 없음 시 표시할 basename ("" = 정상)

    // ─── Feature: Image Info 창의 PSNR 표시 (p 키) ──────────────────────────
    bool  info_psnr_computed = false;    // p로 계산됨 (영상 (재)로드 시 false로 리셋)
    float info_psnr_db       = -1.0f;    // 평균 PSNR(dB), 999=identical/inf
    bool  info_psnr_mismatch = false;    // 크기/포맷 불일치
    OverlayState overlay;               // Overlay/Blend 비교 모드
    bool         show_scatter_plot = false; // Scatter Plot 창 표시
    bool         windowed_mode = false;    // W key: windowed (title bar) mode
    bool         show_borders = true;     // B key: toggle panel borders
    ContextSaveState context_save;         // right-click context menu save state

    // ─── Feature: Crosshair Overlay (M key) ─────────────────────────────────
    bool show_crosshair = false;

    // ─── Feature: Magnifier (Ctrl+M toggle, non-diff mode) ────────────────
    bool magnifier_active = true;
    bool mouse_constrained = false;  // SDL mouse rect constraint active

    // ─── Feature: Pixel Format (Dec / 0xHex / Hexh) ────────────────────────
    PixelFormat pixel_format = PixelFormat::Decimal;

    // ─── Feature: Histogram Compare ─────────────────────────────────────────
    bool histogram_compare = false;

    // ─── Feature: Diff Listing Window (Ctrl+D) ───────────────────────────
    DiffListingState diff_listing;

    // ─── Feature: Image Copy Mode (Ctrl/Cmd+C → 1/2/3) ──────────────────
    struct CopyModeState {
        bool   active     = false;
        double started_at = 0.0;   // ImGui::GetTime() on entry
        double toast_until = 0.0;  // end time for "Copied" feedback toast
        int    last_copied = -1;   // 0=A, 1=B, 2=Diff, -1=none
        void reset() { active = false; started_at = 0.0; }
    };
    CopyModeState copy_mode;

    // ─── Feature: Slideshow ─────────────────────────────────────────────────
    struct SlideshowState {
        bool  active    = false;
        float interval  = 3.0f;   // seconds
        float countdown = 0.0f;
        int   panel     = 0;      // which panel's sequence to advance
    };
    SlideshowState slideshow;

    // ─── Window drag optimization ────────────────────────────────────────
    bool     window_moving = false;
    uint64_t last_window_move_tick = 0;  // SDL_GetTicksNS()

    // ─── Feature: Filename toast (on image load / sequence navigate) ────
    FilenameToastState filename_toast;

};

// ─── Function declarations ────────────────────────────────────────────────────

// Parse command-line arguments; prints help/version and exits if requested.
CliOptions parse_cli(int argc, char* argv[]);

// Apply CLI options to initial AppState (after images are loaded).
void apply_cli_options(AppState& state, const CliOptions& opts);

// Handle a keyboard event (SDL_SCANCODE_* values).
// gui: true if Cmd (macOS) / Win key is held.
void handle_keyboard(AppState& state, int sdl_scancode, bool ctrl, bool shift, bool alt, bool gui = false);

// Advance the given panel's sequence by dir (+1 = next, -1 = prev) with wrap-around.
// Triggers the open_state pending path so main_window picks up the load next frame.
// No-op if the panel has no sequence populated.
void sequence_navigate(AppState& state, int panel, int dir);

// Compute PSNR between panel A(기준/왼쪽) and B(비교/오른쪽) into state.info_psnr_*.
// 두 영상이 로드돼 있어야 하며, 크기/포맷 불일치면 info_psnr_mismatch를 set.
// Image Info 창이 열리고 두 영상이 있으면 자동 호출된다(별도 키 불필요).
void compute_info_psnr(AppState& state);

// Load/save persistent settings from/to av.ini.
void load_app_ini(AppState& state);
void save_app_ini(const AppState& state);
