#include "app.h"
#include "viewport.h"
#include "image_loader.h"

#include <SDL3/SDL.h>

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>

// ─── parse_cli ────────────────────────────────────────────────────────────────

// Parse a 6-digit hex RGB string (with optional '#') into an ImGui ABGR uint32
// with alpha = 230.
static uint32_t parse_hex_color(const std::string& s) {
    const char* p = s.c_str();
    if (*p == '#') ++p;
    unsigned int rgb = 0;
    if (std::sscanf(p, "%6x", &rgb) != 1) {
        std::cerr << "Invalid hex colour: " << s << "\n";
        std::exit(1);
    }
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8)  & 0xFF;
    uint8_t b =  rgb        & 0xFF;
    // ImGui IM_COL32 stores as ABGR: (A<<24)|(B<<16)|(G<<8)|R
    return (230u << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static void print_help(const char* prog) {
    std::cout <<
        "Usage: " << prog << " [options] [imageA] [imageB]\n"
        "\n"
        "Options:\n"
        "  --diff-mode <mode>   none|abs|rel|falsecolor|ssim  (default: none)\n"
        "  --zoom <factor>      fit|1|2.0 etc.                (default: fit)\n"
        "  --sync               Enable viewport sync          (default: on)\n"
        "  --no-sync            Disable viewport sync\n"
        "  --amplify <val>      Diff amplification 0.1-100   (default: 1.0)\n"
        "  --fullscreen         Start in fullscreen\n"
        "  --geometry <WxH>     Initial window size           (default: 1280x720)\n"
        "  --profile <file>     ICC colour profile path\n"
        "  --no-color-mgmt      Disable colour management\n"
        "  -p, --pan-step <N>   Shift+hjkl jump size in pixels   (default: 32)\n"
        "  -bc <A> <B> <D>      Border colours for A/B/Diff panels as 6-digit hex\n"
        "                       e.g. -bc ff00ff ffff00 00ffff   (default: magenta/yellow/cyan)\n"
        "  -d, --diff           Show pixel-absolute diff (shortcut)\n"
        "  --version            Print version and exit\n"
        "  -h, --help           Print this help\n"
        "\n";
}

CliOptions parse_cli(int argc, char* argv[]) {
    CliOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto next = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Expected value after " << arg << "\n";
            std::exit(1);
        };

        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            std::exit(0);
        } else if (arg == "--version") {
            std::cout << "av 0.1.0\n";
            std::exit(0);
        } else if (arg == "--diff-mode") {
            std::string m = next();
            if      (m == "none")       opts.diff_mode = DiffState::Mode::None;
            else if (m == "abs")        opts.diff_mode = DiffState::Mode::PixelAbsolute;
            else if (m == "rel")        opts.diff_mode = DiffState::Mode::PixelRelative;
            else if (m == "falsecolor") opts.diff_mode = DiffState::Mode::FalseColor;
            else if (m == "ssim")       opts.diff_mode = DiffState::Mode::SSIM;
            else { std::cerr << "Unknown diff-mode: " << m << "\n"; std::exit(1); }
        } else if (arg == "--zoom") {
            std::string z = next();
            if (z == "fit") opts.zoom = 0.0f;
            else            opts.zoom = std::stof(z);
        } else if (arg == "--sync") {
            opts.sync = true;
        } else if (arg == "--no-sync") {
            opts.sync = false;
        } else if (arg == "--amplify") {
            opts.amplify = std::stof(next());
        } else if (arg == "--fullscreen") {
            opts.fullscreen = true;
        } else if (arg == "--geometry") {
            std::string g = next();
            auto x = g.find('x');
            if (x == std::string::npos) x = g.find('X');
            if (x != std::string::npos) {
                opts.win_w = std::stoi(g.substr(0, x));
                opts.win_h = std::stoi(g.substr(x + 1));
            }
        } else if (arg == "--profile") {
            opts.icc_profile = next();
        } else if (arg == "--no-color-mgmt") {
            opts.no_color_mgmt = true;
        } else if (arg == "-p" || arg == "--pan-step") {
            opts.pan_step = std::stoi(next());
        } else if (arg == "-d" || arg == "--diff") {
            opts.diff_mode = DiffState::Mode::PixelAbsolute;
        } else if (arg == "-bc") {
            for (int j = 0; j < 3; ++j)
                opts.border_colors[j] = parse_hex_color(next());
        } else if (arg.size() > 2 && arg.substr(0, 2) == "--") {
            std::cerr << "Unknown option: " << arg << "\n";
            std::exit(1);
        } else {
            // Positional: imageA then imageB
            if (opts.image_a.empty())      opts.image_a = arg;
            else if (opts.image_b.empty()) opts.image_b = arg;
            else {
                std::cerr << "Too many image arguments (max 2)\n";
                std::exit(1);
            }
        }
    }
    return opts;
}

void apply_cli_options(AppState& state, const CliOptions& opts) {
    state.cli             = opts;
    state.diff.mode       = opts.diff_mode;
    state.diff.amplify    = opts.amplify;
    state.sync_viewports  = opts.sync;
    state.pan_step        = opts.pan_step;
    state.border_colors   = opts.border_colors;
}

// ─── handle_keyboard ──────────────────────────────────────────────────────────

void handle_keyboard(AppState& state, int scancode, bool ctrl, bool shift, bool alt, bool gui) {
    (void)alt;

    auto& vA = state.views[0];
    auto& vB = state.views[1];

    // Pan step in image-pixels (zoom-independent)
    float step = shift ? static_cast<float>(state.pan_step) : 1.0f;

    switch (scancode) {
    // ── Quit ──────────────────────────────────────────────────────────────────
    case SDL_SCANCODE_Q:
        state.quit = true;
        break;

    // ── Zoom ──────────────────────────────────────────────────────────────────
    case SDL_SCANCODE_EQUALS:   // + / =
    case SDL_SCANCODE_KP_PLUS:
        viewport_zoom_in(vA);
        if (state.sync_viewports) { vB.zoom = vA.zoom; vB.fit = false; }
        break;
    case SDL_SCANCODE_MINUS:
    case SDL_SCANCODE_KP_MINUS:
        viewport_zoom_out(vA);
        if (state.sync_viewports) { vB.zoom = vA.zoom; vB.fit = false; }
        break;
    case SDL_SCANCODE_Z:
        if (shift) {
            viewport_zoom_out(vA);
            if (state.sync_viewports) { vB.zoom = vA.zoom; vB.fit = false; }
        } else {
            viewport_zoom_in(vA);
            if (state.sync_viewports) { vB.zoom = vA.zoom; vB.fit = false; }
        }
        break;
    case SDL_SCANCODE_0: case SDL_SCANCODE_1: case SDL_SCANCODE_2:
    case SDL_SCANCODE_3: case SDL_SCANCODE_4: case SDL_SCANCODE_5:
    case SDL_SCANCODE_6: case SDL_SCANCODE_7: case SDL_SCANCODE_8: {
        if (ctrl) {
            // ctrl+number: diff mode shortcuts
            int key_num = (scancode == SDL_SCANCODE_0) ? 0 : (scancode - SDL_SCANCODE_1 + 1);
            if      (key_num == 3) state.diff.mode = DiffState::Mode::PixelAbsolute;
            else if (key_num == 5) state.diff.mode = DiffState::Mode::FalseColor;
            else if (key_num == 6) state.diff.mode = DiffState::Mode::SSIM;
            break;
        }
        int key_num = (scancode == SDL_SCANCODE_0) ? 0 : (scancode - SDL_SCANCODE_1 + 1);
        float zoom = std::pow(2.0f, static_cast<float>(key_num));
        viewport_set_zoom(vA, zoom);
        viewport_center(vA);
        if (state.sync_viewports) { viewport_set_zoom(vB, zoom); viewport_center(vB); }
        break;
    }
    case SDL_SCANCODE_F:
        vA.fit = !vA.fit;
        if (state.sync_viewports) vB.fit = vA.fit;
        break;
    case SDL_SCANCODE_G:
        if (shift) {
            state.channel_mode = ChannelMode::Green;
        } else {
            viewport_center(vA);
            if (state.sync_viewports) viewport_center(vB);
        }
        break;

    // ── Pan ───────────────────────────────────────────────────────────────────
    case SDL_SCANCODE_H:
        if (ctrl) { state.show_histogram = !state.show_histogram; break; }
        [[fallthrough]];
    case SDL_SCANCODE_LEFT:
        viewport_pan(vA, -step, 0.0f);
        if (state.sync_viewports) viewport_pan(vB, -step, 0.0f);
        break;
    case SDL_SCANCODE_L:
        if (ctrl) { state.show_hline_cut = !state.show_hline_cut; break; }
        [[fallthrough]];
    case SDL_SCANCODE_RIGHT:
        viewport_pan(vA, +step, 0.0f);
        if (state.sync_viewports) viewport_pan(vB, +step, 0.0f);
        break;
    case SDL_SCANCODE_K:
    case SDL_SCANCODE_UP:
        viewport_pan(vA, 0.0f, +step);
        if (state.sync_viewports) viewport_pan(vB, 0.0f, +step);
        break;
    case SDL_SCANCODE_J:
    case SDL_SCANCODE_DOWN:
        viewport_pan(vA, 0.0f, -step);
        if (state.sync_viewports) viewport_pan(vB, 0.0f, -step);
        break;

    // ── Diff mode ─────────────────────────────────────────────────────────────
    case SDL_SCANCODE_D:
        if (ctrl) state.diff.mode = DiffState::Mode::None;
        break;

    // ── Diff amplify ──────────────────────────────────────────────────────────
    case SDL_SCANCODE_LEFTBRACKET:
        state.diff.amplify = std::max(0.5f, state.diff.amplify - 0.5f);
        break;
    case SDL_SCANCODE_RIGHTBRACKET:
        state.diff.amplify = std::min(100.0f, state.diff.amplify + 0.5f);
        break;
    case SDL_SCANCODE_BACKSLASH:
        state.diff.amplify = 1.0f;
        break;

    // ── Open Images dialog ───────────────────────────────────────────────────
    case SDL_SCANCODE_O:
        if (shift && (ctrl || gui))
            state.open_state.show = !state.open_state.show;
        break;

    // ── Viewport sync / Statistics / Save ────────────────────────────────────
    case SDL_SCANCODE_S:
        if (shift && (ctrl || gui)) state.show_save_dialog = !state.show_save_dialog;
        else if (ctrl)              state.show_stats = !state.show_stats;
        else                        state.sync_viewports = !state.sync_viewports;
        break;

    // ── A/B swap / 1:1 zoom ───────────────────────────────────────────────────
    case SDL_SCANCODE_SPACE:
        if (shift && state.images[0].loaded && state.images[1].loaded) {
            state.swap_images = !state.swap_images;
        } else {
            viewport_set_zoom(vA, 1.0f);
            viewport_center(vA);
            if (state.sync_viewports) { viewport_set_zoom(vB, 1.0f); viewport_center(vB); }
        }
        break;

    // ── Panel focus ───────────────────────────────────────────────────────────
    case SDL_SCANCODE_TAB:
        state.active_panel = 1 - state.active_panel;
        break;

    // ── Channel display / Image rotation ──────────────────────────────────
    case SDL_SCANCODE_R:
        if (shift) {
            state.channel_mode = ChannelMode::Red;
        } else if (ctrl) {
            for (auto& img : state.images) if (img.loaded) rotate_image_ccw(img);
            for (auto& v : state.views) { v.fit = true; v.pan_x = 0; v.pan_y = 0; }
        } else {
            for (auto& img : state.images) if (img.loaded) rotate_image_cw(img);
            for (auto& v : state.views) { v.fit = true; v.pan_x = 0; v.pan_y = 0; }
        }
        break;
    case SDL_SCANCODE_B:
        if (shift) state.channel_mode = ChannelMode::Blue;
        break;
    case SDL_SCANCODE_C:
        if (shift) state.channel_mode = ChannelMode::RGB;
        break;

    // ── UI toggles ────────────────────────────────────────────────────────────
    case SDL_SCANCODE_U:
        state.show_ui = !state.show_ui;
        break;
    case SDL_SCANCODE_I:
        state.show_info = !state.show_info;
        break;
    case SDL_SCANCODE_V:
        state.show_pixel_info = !state.show_pixel_info;
        break;
    case SDL_SCANCODE_P:
        state.show_pathfinder = !state.show_pathfinder;
        break;
    case SDL_SCANCODE_Y:
        if (ctrl) state.show_vline_cut = !state.show_vline_cut;
        break;

    default:
        break;
    }
}
