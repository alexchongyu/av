#include "app.h"
#include "viewport.h"
#include "image_loader.h"
#include "image_open.h"
#include "chart_export.h"  // compute_diff_stats (PSNR readout)
#include "clipboard_image.h"
#include "version.h"  // AV_VERSION (auto-generated from git describe)

#include <SDL3/SDL.h>
#include <imgui.h>

// ─── sequence_navigate ───────────────────────────────────────────────────────
// 공용 시퀀스 탐색 함수. 키/마우스/슬라이드쇼 모두 이 함수를 통해 진행.
// 실제 로드는 main_window.cpp 의 open_state pending 처리 블록에서 수행.
void sequence_navigate(AppState& state, int panel, int dir) {
    if (state.pair_mode) panel = 0;   // --pair: 항상 A(왼쪽)가 시퀀스를 구동, B는 이름으로 미러
    if (panel < 0 || panel > 1) return;
    auto& seq = state.sequences[panel];
    if (seq.files.empty() || seq.current_index < 0) return;
    const int n_files = static_cast<int>(seq.files.size());
    const int step    = (dir >= 0) ? 1 : -1;
    seq.current_index = (seq.current_index + step + n_files) % n_files;
    state.open_state.opened_path  = seq.files[seq.current_index];
    state.open_state.open_target  = panel;
    state.open_state.open_pending = true;
    state.open_state.clear_other  = false;
}

#include <iostream>
#include <fstream>
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
        "  --diff-mode <mode>   none|alphablend|abs|rel|highlight|falsecolor|ssim|flip|signed|enhance  (default: none)\n"
        "  --zoom <factor>      fit|0|1|2 etc. (0/fit=window, 1=1:1 actual; saved)\n"
        "  --sync               Enable viewport sync          (default: on)\n"
        "  --no-sync            Disable viewport sync\n"
        "  --pair               Pair same-named file from imageB's directory with imageA;\n"
        "                       next/prev loads the matching filename from both dirs\n"
        "                       (the two directories must differ)\n"
        "  --metrics            Headless: print PSNR/SSIM/FLIP/MSE/MAE (CSV) for A vs B and exit.\n"
        "                       With --pair, iterates the whole sequence (one row per frame).\n"
        "  --format <fmt>       --metrics output: csv (default) | json | junit\n"
        "  --fail-psnr <dB>     CI gate: exit 10 if any pair's PSNR < dB (also --fail-ssim,\n"
        "  --fail-ssim <v>      --fail-flip <v> [>], --fail-maxerr <v> [>], --warn-psnr <dB>)\n"
        "  --diff-out <path>    Headless: write the --diff-mode diff (or FLIP) of A vs B to a\n"
        "                       PNG and exit.  --sbs = A|diff|B composite.\n"
        "  --amplify <val>      Diff amplification 0.1-100   (default: 1.0)\n"
        "  --fullscreen         Start in fullscreen\n"
        "  --geometry <WxH>     Initial window size           (default: 1280x720)\n"
        "  --profile <file>     ICC colour profile path\n"
        "  --no-color-mgmt      Disable colour management\n"
        "  -p, --pan-step <N>   Shift+hjkl jump size in pixels   (default: 32)\n"
        "  -bc <A> <B> <D>      Border colours for A/B/Diff panels as 6-digit hex\n"
        "                       e.g. -bc ff00ff ffff00 00ffff   (default: magenta/yellow/cyan)\n"
        "  -d, --diff           Show pixel-absolute diff (shortcut)\n"
        "  --software           Force SDL software renderer (no OpenGL)\n"
        "  --windowed           Start in windowed mode (title bar + resizable)\n"
        "  -nb                  Start with panel borders hidden\n"
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
            // AV_VERSION_DATE = latest commit date (git log -1 --format=%cs), set by GenVersion.cmake
            std::cout << "av " << AV_VERSION_FULL << "  (updated " << AV_VERSION_DATE << ")\n";
            std::exit(0);
        } else if (arg == "--diff-mode") {
            std::string m = next();
            bool matched = false;
            for (const auto& d : kDiffModes) {   // single source of truth (app.h)
                if (d.cli_token && m == d.cli_token) {
                    opts.diff_mode = d.mode;
                    matched = true;
                    break;
                }
            }
            if (!matched) { std::cerr << "Unknown diff-mode: " << m << "\n"; std::exit(1); }
        } else if (arg == "--zoom") {
            std::string z = next();
            if (z == "fit") opts.zoom = 0.0f;
            else            opts.zoom = std::stof(z);   // 0 → fit, 1 → 1:1 실제크기, 2 → 2배 …
            opts.zoom_set = true;                        // CLI 명시 → 저장된 초기 zoom 덮어쓰기
        } else if (arg == "--sync") {
            opts.sync = true;
        } else if (arg == "--no-sync") {
            opts.sync = false;
        } else if (arg == "--pair") {
            opts.pair = true;
        } else if (arg == "--metrics") {
            opts.metrics = true;
        } else if (arg == "--fail-psnr") {
            opts.fail_psnr = std::stof(next());
        } else if (arg == "--warn-psnr") {
            opts.warn_psnr = std::stof(next());
        } else if (arg == "--fail-ssim") {
            opts.fail_ssim = std::stof(next());
        } else if (arg == "--fail-flip") {
            opts.fail_flip = std::stof(next());
        } else if (arg == "--fail-maxerr") {
            opts.fail_maxerr = std::stof(next());
        } else if (arg == "--format") {
            opts.out_format = next();
            if (opts.out_format != "csv" && opts.out_format != "json" && opts.out_format != "junit") {
                std::cerr << "Unknown --format: " << opts.out_format << " (csv|json|junit)\n";
                std::exit(1);
            }
        } else if (arg == "--diff-out") {
            opts.diff_out = next();
        } else if (arg == "--sbs") {
            opts.diff_out_sbs = true;
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
        } else if (arg == "--software") {
            opts.software = true;
        } else if (arg == "--windowed") {
            opts.windowed = true;
        } else if (arg == "-nb") {
            opts.no_border = true;
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
    state.windowed_mode   = opts.windowed;
    if (opts.no_border) state.show_borders = false;
    // --zoom 이 CLI에 명시된 경우에만 저장된 초기 zoom 을 덮어쓴다.
    // (미지정 시 load_app_ini 가 읽어둔 .av.ini 값 유지 → "마지막 옵션 상태" 재현)
    if (opts.zoom_set) state.zoom_setting = opts.zoom;
}

// ─── compute_info_psnr ────────────────────────────────────────────────────────
// A(기준/왼쪽) 대비 B(비교/오른쪽) PSNR을 계산해 state.info_psnr_* 에 저장.
// 결과는 Image Info 창에 표시된다. 영상 (재)로드 시 info_psnr_computed가 false로
// 리셋되므로, 창이 열려 있으면 다음 프레임에 자동 재계산된다.
void compute_info_psnr(AppState& state) {
    const ImageEntry& A = state.images[0];
    const ImageEntry& B = state.images[1];
    if (!A.loaded || !B.loaded) return;
    bool bothU8  = !A.pixels.empty()     && !B.pixels.empty();
    bool bothF32 = !A.pixels_f32.empty() && !B.pixels_f32.empty();
    if (A.width != B.width || A.height != B.height || (!bothU8 && !bothF32)) {
        state.info_psnr_mismatch = true;   // 크기/포맷 불일치 → N/A
    } else {
        DiffExtraStats extra;
        compute_diff_stats(A, B, extra);   // A=ref, B=cmp (순서 유지)
        double sum = 0.0; int cnt = 0;
        for (int c = 0; c < 3; ++c) {
            if (extra.psnr[c] > 0.0 && extra.psnr[c] < 999.0) { sum += extra.psnr[c]; ++cnt; }
        }
        state.info_psnr_db       = (cnt > 0) ? static_cast<float>(sum / cnt) : 999.0f;
        state.info_psnr_mismatch = false;
    }
    state.info_psnr_computed = true;
}

// ─── handle_keyboard ──────────────────────────────────────────────────────────

void handle_keyboard(AppState& state, int scancode, bool ctrl, bool shift, bool alt, bool gui) {
    (void)alt;

    // ── Copy mode (Ctrl/Cmd+C → 1/2/3) ────────────────────────────────────────
    // When active, intercept digit keys for image copy and swallow other input.
    if (state.copy_mode.active) {
        int target = -1;
        if (scancode == SDL_SCANCODE_1 || scancode == SDL_SCANCODE_KP_1) target = 0;
        else if (scancode == SDL_SCANCODE_2 || scancode == SDL_SCANCODE_KP_2) target = 1;
        else if (scancode == SDL_SCANCODE_3 || scancode == SDL_SCANCODE_KP_3) target = 2;

        if (target >= 0) {
            if (clipboard_copy_image(state, target)) {
                state.copy_mode.last_copied = target;
                state.copy_mode.toast_until = ImGui::GetTime() + 1.5;
            }
            state.copy_mode.reset();
            return;
        }
        // Any other key (including Esc) cancels the mode.
        state.copy_mode.reset();
        if (scancode == SDL_SCANCODE_ESCAPE) return;
        // Fall through so non-digit keys still perform their normal action.
    }

    auto& vA = state.views[0];
    auto& vB = state.views[1];

    // Pan step in image-pixels (zoom-independent)
    float step = shift ? static_cast<float>(state.pan_step) : 1.0f;

    switch (scancode) {
    // ── Quit ──────────────────────────────────────────────────────────────────
    case SDL_SCANCODE_Q:
        state.quit = true;
        break;

    // ── Close popups ──────────────────────────────────────────────────────────
    case SDL_SCANCODE_ESCAPE:
        if (state.blink.active)      { state.blink.active = false;
                                       state.sync_viewports = state.blink.saved_sync; break; }
        if (state.diff_listing.show) { state.diff_listing.show = false; break; }
        if (state.show_hotkey_help)  { state.show_hotkey_help  = false; break; }
        if (state.show_histogram)    { state.show_histogram    = false; break; }
        if (state.show_hline_cut)    { state.show_hline_cut    = false; break; }
        if (state.show_vline_cut)    { state.show_vline_cut    = false; break; }
        if (state.show_stats)        { state.show_stats        = false; break; }
        if (state.show_roi_stats)    { state.show_roi_stats    = false; break; }
        if (state.show_scatter_plot) { state.show_scatter_plot = false; break; }
        if (state.show_info)         { state.show_info         = false; break; }
        if (state.show_pixel_info)   { state.show_pixel_info   = false; break; }
        if (state.show_save_dialog)  { state.show_save_dialog  = false; break; }
        if (state.show_crosshair)    { state.show_crosshair    = false; break; }
        if (state.roi.active)        { state.roi.active        = false; break; }
        if (state.overlay.active)    { state.overlay.active    = false; break; }
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
    case SDL_SCANCODE_X:
        if (ctrl && !shift && !gui) {
            // Ctrl+X: cycle pixel format (Decimal → 0xHex → Hexh)
            state.pixel_format = static_cast<PixelFormat>(
                (static_cast<int>(state.pixel_format) + 1) % 3);
        } else {
            viewport_zoom_out(vA);
            if (state.sync_viewports) { vB.zoom = vA.zoom; vB.fit = false; }
        }
        break;
    case SDL_SCANCODE_0: case SDL_SCANCODE_1: case SDL_SCANCODE_2:
    case SDL_SCANCODE_3: case SDL_SCANCODE_4: case SDL_SCANCODE_5:
    case SDL_SCANCODE_6: case SDL_SCANCODE_7: case SDL_SCANCODE_8:
    case SDL_SCANCODE_9: {
        if (ctrl) {
            // ctrl+number: diff mode shortcuts
            int key_num = (scancode == SDL_SCANCODE_0) ? 0 : (scancode - SDL_SCANCODE_1 + 1);
            if      (key_num == 2) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::AlphaBlend)
                    ? DiffState::Mode::None : DiffState::Mode::AlphaBlend;
                state.diff_listing.computed = false;
                // B 미로드 상태에서 AlphaBlend 토글 시 경고 토스트
                if (state.diff.mode == DiffState::Mode::AlphaBlend &&
                    (!state.images[0].loaded || !state.images[1].loaded)) {
                    state.filename_toast.filename = "B image required for Alpha Blend";
                    state.filename_toast.panel    = 1;
                    state.filename_toast.until    = ImGui::GetTime() + 1.8;
                    state.filename_toast.failed   = true;
                }
            }
            else if (key_num == 3) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::PixelAbsolute)
                    ? DiffState::Mode::None : DiffState::Mode::PixelAbsolute;
                state.diff_listing.computed = false;
            }
            else if (key_num == 4) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::PixelRelative)
                    ? DiffState::Mode::None : DiffState::Mode::PixelRelative;
                state.diff_listing.computed = false;
            }
            else if (key_num == 5) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::Enhance)
                    ? DiffState::Mode::None : DiffState::Mode::Enhance;
                state.diff_listing.computed = false;
                state.diff.enhance_range_computed = false;
            }
            else if (key_num == 6) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::FalseColor)
                    ? DiffState::Mode::None : DiffState::Mode::FalseColor;
                state.diff_listing.computed = false;
            }
            else if (key_num == 7) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::SSIM)
                    ? DiffState::Mode::None : DiffState::Mode::SSIM;
                state.diff_listing.computed = false;
            }
            else if (key_num == 0) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::FLIP)
                    ? DiffState::Mode::None : DiffState::Mode::FLIP;
                state.diff_listing.computed = false;
            }
            else if (key_num == 1) {
                state.diff.mode = (state.diff.mode == DiffState::Mode::Signed)
                    ? DiffState::Mode::None : DiffState::Mode::Signed;
                state.diff_listing.computed = false;
            }
            else if (key_num == 8) {
                // Ctrl+8: tolerance diff — set threshold to 1 if disabled, or toggle off
                if (state.diff.threshold == 0)
                    state.diff.threshold = 1;
                else
                    state.diff.threshold = 0;
            }
            else if (key_num == 9) {
                // Ctrl+9: Highlight diff mode toggle (moved from Ctrl+5)
                state.diff.mode = (state.diff.mode == DiffState::Mode::Highlight)
                    ? DiffState::Mode::None : DiffState::Mode::Highlight;
                state.diff_listing.computed = false;
            }
            break;
        }
        int key_num = (scancode == SDL_SCANCODE_0) ? 0 : (scancode - SDL_SCANCODE_1 + 1);
        if (key_num > 8) break;  // 9 key: Ctrl+9 only (Highlight toggle)
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
        if (shift && ctrl) {
            state.show_hotkey_help = !state.show_hotkey_help;
            break;
        }
        if (gui && shift) {
            vA.pan_x = 1e6f; vA.fit = false;
            if (state.sync_viewports) { vB.pan_x = 1e6f; vB.fit = false; }
            break;
        }
        if (ctrl) {
            state.show_histogram = !state.show_histogram;
            if (state.show_histogram) {
                state.show_hline_cut = false;
                state.show_vline_cut = false;
                state.show_stats     = false;
            }
            break;
        }
        [[fallthrough]];
    case SDL_SCANCODE_LEFT:
        viewport_pan(vA, +step, 0.0f);
        if (state.sync_viewports) viewport_pan(vB, +step, 0.0f);
        break;
    case SDL_SCANCODE_L:
        if (gui && shift) {
            vA.pan_x = -1e6f; vA.fit = false;
            if (state.sync_viewports) { vB.pan_x = -1e6f; vB.fit = false; }
            break;
        }
        if (ctrl) {
            state.show_hline_cut = !state.show_hline_cut;
            if (state.show_hline_cut) {
                state.show_histogram = false;
                state.show_vline_cut = false;
                state.show_stats     = false;
            }
            break;
        }
        [[fallthrough]];
    case SDL_SCANCODE_RIGHT:
        viewport_pan(vA, -step, 0.0f);
        if (state.sync_viewports) viewport_pan(vB, -step, 0.0f);
        break;
    case SDL_SCANCODE_K:
        if (gui && shift) {
            vA.pan_y = 1e6f; vA.fit = false;
            if (state.sync_viewports) { vB.pan_y = 1e6f; vB.fit = false; }
            break;
        }
        viewport_pan(vA, 0.0f, +step);
        if (state.sync_viewports) viewport_pan(vB, 0.0f, +step);
        break;
    case SDL_SCANCODE_UP:
        if (shift && state.slideshow.active) {
            state.slideshow.interval = std::min(10.0f, state.slideshow.interval + 1.0f);
            state.slideshow.countdown = std::min(state.slideshow.countdown, state.slideshow.interval);
        } else {
            viewport_pan(vA, 0.0f, +step);
            if (state.sync_viewports) viewport_pan(vB, 0.0f, +step);
        }
        break;
    case SDL_SCANCODE_J:
        if (gui && shift) {
            vA.pan_y = -1e6f; vA.fit = false;
            if (state.sync_viewports) { vB.pan_y = -1e6f; vB.fit = false; }
            break;
        }
        viewport_pan(vA, 0.0f, -step);
        if (state.sync_viewports) viewport_pan(vB, 0.0f, -step);
        break;
    case SDL_SCANCODE_DOWN:
        if (shift && state.slideshow.active) {
            state.slideshow.interval = std::max(0.5f, state.slideshow.interval - 1.0f);
            state.slideshow.countdown = std::min(state.slideshow.countdown, state.slideshow.interval);
        } else {
            viewport_pan(vA, 0.0f, -step);
            if (state.sync_viewports) viewport_pan(vB, 0.0f, -step);
        }
        break;

    // ── Diff mode ─────────────────────────────────────────────────────────────
    case SDL_SCANCODE_D:
        if (ctrl) {
            // diff 리스팅 윈도우 토글
            state.diff_listing.show = !state.diff_listing.show;
            if (state.diff_listing.show) state.diff_listing.start_from = 0;
        }
        break;

    // ── Diff 주 파라미터 조절 ────────────────────────────────────────────────
    // [ / ]: AlphaBlend 모드 → alpha ±1% (Shift: ±10%)
    //        그 외 diff 모드 → amplify ±0.5 (Shift: threshold ±1)
    // \    : AlphaBlend 모드 → alpha = 0.5 (Ctrl+\: threshold 0)
    //        그 외 → amplify = 1.0
    case SDL_SCANCODE_LEFTBRACKET:
        if (state.diff.mode == DiffState::Mode::AlphaBlend) {
            float step = shift ? 0.10f : 0.01f;
            state.diff.alpha = std::max(0.0f, state.diff.alpha - step);
        } else if (shift) {
            state.diff.threshold = std::max(0, state.diff.threshold - 1);
        } else {
            state.diff.amplify = std::max(0.5f, state.diff.amplify - 0.5f);
        }
        break;
    case SDL_SCANCODE_RIGHTBRACKET:
        if (state.diff.mode == DiffState::Mode::AlphaBlend) {
            float step = shift ? 0.10f : 0.01f;
            state.diff.alpha = std::min(1.0f, state.diff.alpha + step);
        } else if (shift) {
            state.diff.threshold = std::min(255, state.diff.threshold + 1);
        } else {
            state.diff.amplify = std::min(100.0f, state.diff.amplify + 0.5f);
        }
        break;
    case SDL_SCANCODE_BACKSLASH:
        if (ctrl) {
            state.diff.threshold = 0;
        } else if (state.diff.mode == DiffState::Mode::AlphaBlend) {
            state.diff.alpha = 0.5f;
        } else {
            state.diff.amplify = 1.0f;
        }
        break;

    // ── Open Image (native dialog) / Overlay 모드 ────────────────────────────
    case SDL_SCANCODE_O:
        if (shift && (ctrl || gui)) {
            open_open_file_dialog(state, state.active_panel);
        } else if (!ctrl && !shift && !gui) {
            // O 단독: Overlay/Blend 모드 토글
            state.overlay.active = !state.overlay.active;
        }
        break;

    // ── Viewport sync / Statistics / Save ────────────────────────────────────
    case SDL_SCANCODE_S:
        if (shift && (ctrl || gui)) state.show_save_dialog = !state.show_save_dialog;
        else if (ctrl) {
            state.show_stats = !state.show_stats;
            if (state.show_stats) {
                state.show_histogram  = false;
                state.show_hline_cut  = false;
                state.show_vline_cut  = false;
            }
        } else {
            state.sync_viewports = !state.sync_viewports;
        }
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
            for (auto& img : state.images) if (img.loaded) {
                rotate_image_ccw(img);
                img.content_version = ++state.content_version_counter;  // 회전=콘텐츠 변경
            }
            for (auto& v : state.views) { v.fit = true; v.pan_x = 0; v.pan_y = 0; }
        } else {
            for (auto& img : state.images) if (img.loaded) {
                rotate_image_cw(img);
                img.content_version = ++state.content_version_counter;
            }
            for (auto& v : state.views) { v.fit = true; v.pan_x = 0; v.pan_y = 0; }
        }
        break;
    case SDL_SCANCODE_B:
        if (shift) state.channel_mode = ChannelMode::Blue;
        else if (!ctrl && !gui) state.show_borders = !state.show_borders;
        break;
    case SDL_SCANCODE_C:
        if (shift) state.channel_mode = ChannelMode::RGB;
        else if ((ctrl || gui) && !alt && !ImGui::GetIO().WantTextInput) {
            // Ctrl/Cmd+C: enter image copy mode (hint overlay + awaits 1/2/3)
            // WantTextInput (not WantCaptureKeyboard): only block when an actual
            // text-input widget is focused. Otherwise diff-mode panel focus would
            // silently swallow the entry.
            state.copy_mode.active = true;
            state.copy_mode.started_at = ImGui::GetTime();
        }
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
        // PSNR은 Image Info 창에서 자동 표시되므로 'p'는 기존 pathfinder 전용으로 복귀.
        if (ctrl || gui) {
            state.pathfinder_mode = (state.pathfinder_mode == 2) ? 0 : 2;
        } else {
            state.pathfinder_mode = (state.pathfinder_mode == 1) ? 0 : 1;
        }
        break;
    case SDL_SCANCODE_Y:
        if (ctrl) {
            state.show_vline_cut = !state.show_vline_cut;
            if (state.show_vline_cut) {
                state.show_histogram  = false;
                state.show_hline_cut  = false;
                state.show_stats      = false;
            }
        }
        break;

    // ── ROI 모드 (Ctrl+E) ─────────────────────────────────────────────────────
    case SDL_SCANCODE_E:
        if (ctrl) {
            state.roi.active = !state.roi.active;
            if (!state.roi.active) {
                // ROI 모드 해제 시 ROI 초기화
                state.roi.has_roi  = false;
                state.roi.dragging = false;
            }
        }
        break;

    // ── Scatter Plot (Ctrl+T) ─────────────────────────────────────────────────
    case SDL_SCANCODE_T:
        if (ctrl) {
            state.show_scatter_plot = !state.show_scatter_plot;
        }
        break;

    // ── Windowed mode 토글 (W) ──────────────────────────────────────────────────
    case SDL_SCANCODE_W:
        if (!ctrl && !shift && !gui) {
            state.windowed_mode = !state.windowed_mode;
            if (state.window) {
                if (state.windowed_mode) {
                    SDL_SetWindowBordered(state.window, true);
                    SDL_RestoreWindow(state.window);
                    SDL_DisplayID disp = SDL_GetDisplayForWindow(state.window);
                    SDL_Rect usable;
                    if (disp && SDL_GetDisplayUsableBounds(disp, &usable)) {
                        int w = state.cli.win_w;
                        int h = state.cli.win_h;
                        if (w >= usable.w || h >= usable.h) {
                            w = (int)(usable.w * 0.8f);
                            h = (int)(usable.h * 0.8f);
                        }
                        SDL_SetWindowSize(state.window, w, h);
                        SDL_SetWindowPosition(state.window,
                                              SDL_WINDOWPOS_CENTERED,
                                              SDL_WINDOWPOS_CENTERED);
                    }
                } else {
                    SDL_SetWindowBordered(state.window, false);
                    SDL_MaximizeWindow(state.window);
                }
            }
        }
        break;

    // ── Crosshair overlay 토글 (M) / Magnifier 토글 (Ctrl+M) ─────────────────
    case SDL_SCANCODE_M:
        if (ctrl && !shift && !gui) {
            state.magnifier_active = !state.magnifier_active;
            if (!state.magnifier_active && state.mouse_constrained) {
                SDL_SetWindowMouseRect(state.window, nullptr);
                state.mouse_constrained = false;
            }
        } else if (!ctrl && !shift && !gui) {
            state.show_crosshair = !state.show_crosshair;
        }
        break;

    // ── 시퀀스 탐색 prev (A) / 슬라이드쇼 토글 (Shift+A) ──────────────────────
    case SDL_SCANCODE_A:
        if (!ctrl && !gui) {
            if (shift) {
                state.slideshow.active = !state.slideshow.active;
                if (state.slideshow.active) {
                    state.slideshow.countdown = state.slideshow.interval;
                    state.slideshow.panel = state.active_panel;
                }
            } else {
                sequence_navigate(state, state.active_panel, -1);
            }
        }
        break;

    // ── 시퀀스 탐색 next (;) ─────────────────────────────────────────────────
    case SDL_SCANCODE_SEMICOLON:
        if (!ctrl && !shift && !gui) {
            sequence_navigate(state, state.active_panel, +1);
        }
        break;

    // ── 시퀀스 탐색 legacy 핫키 (N = 다음, Shift+N = 이전) ────────────────────
    case SDL_SCANCODE_N:
        if (!ctrl && !gui) {
            sequence_navigate(state, state.active_panel, shift ? -1 : +1);
        }
        break;

    // ── 블링크 비교기 (, 토글 / < = Shift+, 간격 감소) ────────────────────────
    case SDL_SCANCODE_COMMA:
        if (shift) {
            if (state.blink.active) {
                state.blink.interval  = std::max(0.1f, state.blink.interval - 0.1f);
                state.blink.countdown = std::min(state.blink.countdown, state.blink.interval);
            }
        } else if (!ctrl && !gui) {
            if (state.images[0].loaded && state.images[1].loaded) {
                state.blink.active = !state.blink.active;
                if (state.blink.active) {
                    // Lockstep alignment: A/B share one viewport; force sync while blinking.
                    state.views[0] = state.views[state.active_panel];
                    state.views[1] = state.views[state.active_panel];
                    state.blink.saved_sync = state.sync_viewports;
                    state.sync_viewports   = true;
                    state.blink.countdown  = state.blink.interval;
                    state.blink.show_b     = false;
                } else {
                    state.sync_viewports = state.blink.saved_sync;
                }
            }
        }
        break;

    // ── 블링크 간격 증가 (> = Shift+.) ────────────────────────────────────────
    case SDL_SCANCODE_PERIOD:
        if (shift && state.blink.active) {
            state.blink.interval  = std::min(2.0f, state.blink.interval + 0.1f);
            state.blink.countdown = std::min(state.blink.countdown, state.blink.interval);
        }
        break;

    default:
        break;
    }
}

// ─── INI load / save ─────────────────────────────────────────────────────────

static std::string ini_path() {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.av.ini";
}

void load_app_ini(AppState& state) {
    std::ifstream f(ini_path());
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if      (key == "show_borders")      state.show_borders      = (val != "0");
        else if (key == "pixel_format")      state.pixel_format      = static_cast<PixelFormat>(std::stoi(val));
        else if (key == "magnifier_active")  state.magnifier_active  = (val != "0");
        else if (key == "show_crosshair")    state.show_crosshair    = (val != "0");
        else if (key == "sync_viewports")    state.sync_viewports    = (val != "0");
        else if (key == "show_ui")           state.show_ui           = (val != "0");
        else if (key == "show_info")         state.show_info         = (val != "0");
        else if (key == "show_pixel_info")   state.show_pixel_info   = (val != "0");
        else if (key == "show_histogram")    state.show_histogram    = (val != "0");
        else if (key == "show_hline_cut")    state.show_hline_cut    = (val != "0");
        else if (key == "show_vline_cut")    state.show_vline_cut    = (val != "0");
        else if (key == "show_stats")        state.show_stats        = (val != "0");
        else if (key == "show_hotkey_help")  state.show_hotkey_help  = (val != "0");
        else if (key == "show_scatter_plot") state.show_scatter_plot = (val != "0");
        else if (key == "histogram_compare") state.histogram_compare = (val != "0");
        else if (key == "channel_mode")      state.channel_mode      = static_cast<ChannelMode>(std::stoi(val));
        else if (key == "windowed_mode")     state.windowed_mode     = (val != "0");
        else if (key == "overlay_active")    state.overlay.active    = (val != "0");
        else if (key == "zoom")              state.zoom_setting      = std::stof(val);
    }
}

void save_app_ini(const AppState& state) {
    std::ofstream f(ini_path());
    if (!f.is_open()) return;
    f << "# av configuration\n";
    f << "show_borders="      << (state.show_borders      ? "1" : "0") << "\n";
    f << "pixel_format="      << static_cast<int>(state.pixel_format)  << "\n";
    f << "magnifier_active="  << (state.magnifier_active  ? "1" : "0") << "\n";
    f << "show_crosshair="    << (state.show_crosshair    ? "1" : "0") << "\n";
    f << "sync_viewports="    << (state.sync_viewports    ? "1" : "0") << "\n";
    f << "show_ui="           << (state.show_ui           ? "1" : "0") << "\n";
    f << "show_info="         << (state.show_info         ? "1" : "0") << "\n";
    f << "show_pixel_info="   << (state.show_pixel_info   ? "1" : "0") << "\n";
    f << "show_histogram="    << (state.show_histogram    ? "1" : "0") << "\n";
    f << "show_hline_cut="    << (state.show_hline_cut    ? "1" : "0") << "\n";
    f << "show_vline_cut="    << (state.show_vline_cut    ? "1" : "0") << "\n";
    f << "show_stats="        << (state.show_stats        ? "1" : "0") << "\n";
    f << "show_hotkey_help="  << (state.show_hotkey_help  ? "1" : "0") << "\n";
    f << "show_scatter_plot=" << (state.show_scatter_plot ? "1" : "0") << "\n";
    f << "histogram_compare=" << (state.histogram_compare ? "1" : "0") << "\n";
    f << "channel_mode="      << static_cast<int>(state.channel_mode)  << "\n";
    f << "windowed_mode="     << (state.windowed_mode     ? "1" : "0") << "\n";
    f << "overlay_active="    << (state.overlay.active    ? "1" : "0") << "\n";
    f << "zoom="              << state.zoom_setting << "\n";   // 초기 zoom 옵션(0=fit, >0=배율)
}
