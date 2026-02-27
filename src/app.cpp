#include "app.h"
#include "viewport.h"
#include "image_loader.h"

#include <SDL3/SDL.h>

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>

// ─── parse_cli ────────────────────────────────────────────────────────────────

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
}

// ─── handle_keyboard ──────────────────────────────────────────────────────────

void handle_keyboard(AppState& state, int scancode, bool ctrl, bool shift, bool alt) {
    (void)alt;

    auto& vA = state.views[0];
    auto& vB = state.views[1];
    auto& imgA = state.images[0];
    auto& imgB = state.images[1];

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
    case SDL_SCANCODE_0:
    case SDL_SCANCODE_KP_0:
        if (imgA.loaded) viewport_fit(vA, imgA.width, imgA.height, 0, 0);
        if (imgB.loaded) viewport_fit(vB, imgB.width, imgB.height, 0, 0);
        vA.fit = vB.fit = true;
        break;
    case SDL_SCANCODE_1:
        viewport_set_zoom(vA, 1.0f);
        viewport_center(vA);
        if (state.sync_viewports) { viewport_set_zoom(vB, 1.0f); viewport_center(vB); }
        break;
    case SDL_SCANCODE_2:
        viewport_set_zoom(vA, 2.0f);
        viewport_center(vA);
        if (state.sync_viewports) { viewport_set_zoom(vB, 2.0f); viewport_center(vB); }
        break;
    case SDL_SCANCODE_4:
        viewport_set_zoom(vA, 4.0f);
        viewport_center(vA);
        if (state.sync_viewports) { viewport_set_zoom(vB, 4.0f); viewport_center(vB); }
        break;
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
    case SDL_SCANCODE_LEFT:
        viewport_pan(vA, -step, 0.0f);
        if (state.sync_viewports) viewport_pan(vB, -step, 0.0f);
        break;
    case SDL_SCANCODE_L:
    case SDL_SCANCODE_RIGHT:
        viewport_pan(vA, +step, 0.0f);
        if (state.sync_viewports) viewport_pan(vB, +step, 0.0f);
        break;
    case SDL_SCANCODE_K:
    case SDL_SCANCODE_UP:
        viewport_pan(vA, 0.0f, -step);
        if (state.sync_viewports) viewport_pan(vB, 0.0f, -step);
        break;
    case SDL_SCANCODE_J:
    case SDL_SCANCODE_DOWN:
        viewport_pan(vA, 0.0f, +step);
        if (state.sync_viewports) viewport_pan(vB, 0.0f, +step);
        break;

    // ── Diff mode ─────────────────────────────────────────────────────────────
    case SDL_SCANCODE_3:
        if (ctrl) state.diff.mode = DiffState::Mode::PixelAbsolute;
        break;
    case SDL_SCANCODE_5:
        if (ctrl) state.diff.mode = DiffState::Mode::FalseColor;
        break;
    case SDL_SCANCODE_6:
        if (ctrl) state.diff.mode = DiffState::Mode::SSIM;
        break;
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

    // ── Viewport sync ─────────────────────────────────────────────────────────
    case SDL_SCANCODE_S:
        if (!ctrl) state.sync_viewports = !state.sync_viewports;
        break;

    // ── A/B swap ──────────────────────────────────────────────────────────────
    case SDL_SCANCODE_SPACE:
        state.swap_images = !state.swap_images;
        break;

    // ── Panel focus ───────────────────────────────────────────────────────────
    case SDL_SCANCODE_TAB:
        state.active_panel = 1 - state.active_panel;
        break;

    // ── Channel display ─────────────────────────────────────────────────────
    case SDL_SCANCODE_R:
        if (shift) state.channel_mode = ChannelMode::Red;
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
    case SDL_SCANCODE_P:
        state.show_pixel_info = !state.show_pixel_info;
        break;

    default:
        break;
    }
}
