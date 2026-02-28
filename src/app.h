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
    int   width    = 0;
    int   height   = 0;
    int   channels = 0;
    unsigned int texture_id = 0;      // OpenGL texture handle (GLuint)
    bool  loaded   = false;
    bool  is_hdr   = false;
};

struct ViewportState {
    float zoom  = 1.0f;   // display scale: 0.1 ~ 32.0
    float pan_x = 0.0f;   // image-pixel offset from center (+right)
    float pan_y = 0.0f;   // image-pixel offset from center (+down)
    bool  fit   = true;   // fit-to-window mode
};

enum class ChannelMode { RGB = 0, Red = 1, Green = 2, Blue = 3 };

struct DiffState {
    enum class Mode {
        None,
        PixelAbsolute,
        PixelRelative,
        FalseColor,
        SSIM
    };
    Mode  mode           = Mode::None;
    float amplify        = 1.0f;       // diff amplification factor
    float ssim_score     = -1.0f;      // -1 = not computed
    unsigned int ssim_texture_id = 0;  // SSIM heatmap texture (GLuint)
    bool  ssim_computing = false;
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


struct CliOptions {
    std::string     image_a;
    std::string     image_b;
    DiffState::Mode diff_mode    = DiffState::Mode::None;
    float           zoom         = 0.0f;    // 0 = fit
    bool            sync         = true;
    float           amplify      = 1.0f;
    bool            fullscreen   = false;
    int             win_w        = 1280;
    int             win_h        = 720;
    std::string     icc_profile;
    bool            no_color_mgmt = false;
    int             pan_step     = 32;     // Shift+hjkl jump size in image-pixels
    // Border colors for A / B / Diff panels (ImGui ABGR uint32, alpha=230)
    // Defaults: magenta / yellow / cyan
    std::array<uint32_t, 3> border_colors = {0xE6FF00FFu, 0xE600FFFFu, 0xE6FFFF00u};
};

struct AppState {
    std::array<ImageEntry, 2>    images;        // [0]=left/A, [1]=right/B
    std::array<ViewportState, 2> views;
    DiffState  diff;
    bool  sync_viewports  = true;
    bool  show_ui         = false;   // U key: toggle menu/statusbar overlay
    bool  show_histogram  = false;
    bool  show_hline_cut  = false;   // Ctrl+L: 수평 라인 컷뷰
    bool  show_vline_cut  = false;   // Ctrl+Y: 수직 라인 컷뷰
    bool  show_stats      = false;   // Ctrl+S: Image Statistics window
    bool  show_info       = false;
    bool  show_pixel_info = false;   // V key: cursor pixel balloon
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
};

// ─── Function declarations ────────────────────────────────────────────────────

// Parse command-line arguments; prints help/version and exits if requested.
CliOptions parse_cli(int argc, char* argv[]);

// Apply CLI options to initial AppState (after images are loaded).
void apply_cli_options(AppState& state, const CliOptions& opts);

// Handle a keyboard event (SDL_SCANCODE_* values).
// gui: true if Cmd (macOS) / Win key is held.
void handle_keyboard(AppState& state, int sdl_scancode, bool ctrl, bool shift, bool alt, bool gui = false);
