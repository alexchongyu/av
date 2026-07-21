#include "app.h"
#include "image_loader.h"
#include "viewport.h"
#include "render_backend.h"
#include "ui/main_window.h"

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <iostream>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <system_error>

// X11 error handler for SSH forwarding (suppress MIT-SHM errors)
#if defined(__linux__) || defined(__unix__)
#include <X11/Xlib.h>
#undef None      // X11 #define None 0L conflicts with DiffState::Mode::None
#undef Status    // X11 #define Status int
#undef Bool      // X11 #define Bool int
static int x11_error_handler(Display* /*dpy*/, XErrorEvent* /*ev*/) {
    // Suppress all X11 errors in software mode (MIT-SHM, GLX, etc.)
    return 0;
}
#endif

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr const char* APP_TITLE   = "Advanced Pixel Lens";
static constexpr int         GL_MAJOR    = 3;
static constexpr int         GL_MINOR    = 3;
static constexpr const char* GLSL_VER   = "#version 150";

// ─── Procedural app icon (128×128 RGBA) ───────────────────────────────────────

static SDL_Surface* create_app_icon() {
    constexpr int SIZE = 128;
    constexpr int CR   = 14;   // corner radius

    SDL_Surface* surf = SDL_CreateSurface(SIZE, SIZE, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) return nullptr;

    SDL_LockSurface(surf);

    const uint8_t BG_R = 26, BG_G = 31, BG_B = 46;  // #1a1f2e dark navy
    const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surf->format);

    auto set_px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) return;
        auto* row = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(surf->pixels) + y * surf->pitch);
        row[x] = SDL_MapRGBA(fmt, nullptr, r, g, b, a);
    };

    // 1. Fill background
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < SIZE; ++x)
            set_px(x, y, BG_R, BG_G, BG_B, 255);

    // 2. Round corners (mask to transparent)
    for (int y = 0; y < CR; ++y) {
        for (int x = 0; x < CR; ++x) {
            float dx = static_cast<float>(x - CR), dy = static_cast<float>(y - CR);
            if (dx*dx + dy*dy > static_cast<float>(CR*CR)) set_px(x, y, 0, 0, 0, 0);
        }
        for (int x = SIZE-CR; x < SIZE; ++x) {
            float dx = static_cast<float>(x - (SIZE-1-CR)), dy = static_cast<float>(y - CR);
            if (dx*dx + dy*dy > static_cast<float>(CR*CR)) set_px(x, y, 0, 0, 0, 0);
        }
    }
    for (int y = SIZE-CR; y < SIZE; ++y) {
        for (int x = 0; x < CR; ++x) {
            float dx = static_cast<float>(x - CR), dy = static_cast<float>(y - (SIZE-1-CR));
            if (dx*dx + dy*dy > static_cast<float>(CR*CR)) set_px(x, y, 0, 0, 0, 0);
        }
        for (int x = SIZE-CR; x < SIZE; ++x) {
            float dx = static_cast<float>(x - (SIZE-1-CR)), dy = static_cast<float>(y - (SIZE-1-CR));
            if (dx*dx + dy*dy > static_cast<float>(CR*CR)) set_px(x, y, 0, 0, 0, 0);
        }
    }

    // 3. 3×3 colour grid
    constexpr int MARGIN = 10;
    constexpr int GAP    = 2;
    constexpr int CELL   = (SIZE - 2*MARGIN - 2*GAP) / 3;  // ~34
    const uint8_t COLORS[9][3] = {
        {255,  60, 200},  // magenta
        { 40, 220, 255},  // cyan
        {255, 220,  30},  // yellow
        {255,  70,  70},  // red
        { 70, 220,  90},  // green
        { 70, 120, 255},  // blue
        {255, 155,  30},  // orange
        { 30, 220, 175},  // mint
        {190,  70, 255},  // violet
    };
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            int ci  = row * 3 + col;
            int gx0 = MARGIN + col * (CELL + GAP);
            int gy0 = MARGIN + row * (CELL + GAP);
            for (int dy = 0; dy < CELL; ++dy)
                for (int dx = 0; dx < CELL; ++dx)
                    set_px(gx0+dx, gy0+dy,
                           COLORS[ci][0], COLORS[ci][1], COLORS[ci][2], 230);
        }
    }

    // 4. Magnifier glass (bottom-right overlay)
    constexpr int LX = 80, LY = 80, LR = 28;
    // Lens ring (white, ~3px thick)
    for (int y = LY-LR-2; y <= LY+LR+2; ++y) {
        for (int x = LX-LR-2; x <= LX+LR+2; ++x) {
            float dx = static_cast<float>(x - LX), dy2 = static_cast<float>(y - LY);
            float d  = std::sqrt(dx*dx + dy2*dy2);
            if (d >= static_cast<float>(LR-1) && d <= static_cast<float>(LR+1))
                set_px(x, y, 255, 255, 255, 210);
        }
    }
    // Handle (thick line, ~45° toward bottom-right)
    int hx_start = LX + static_cast<int>(LR * 0.707f);
    int hy_start = LY + static_cast<int>(LR * 0.707f);
    for (int i = 0; i <= 12; ++i) {
        int hx = hx_start + i;
        int hy = hy_start + i;
        for (int t = -1; t <= 1; ++t) {
            set_px(hx+t, hy,   255, 255, 255, 220);
            set_px(hx,   hy+t, 255, 255, 255, 220);
        }
    }

    SDL_UnlockSurface(surf);
    return surf;
}

// ─── Cleanup helper ───────────────────────────────────────────────────────────
struct SdlCleanup {
    SDL_Window*   window       = nullptr;
    SDL_GLContext  gl_ctx       = nullptr;
    SDL_Renderer*  sdl_renderer = nullptr;
    bool           imgui_gl     = false;
    bool           imgui_sw     = false;
    bool           imgui_sdl    = false;

    ~SdlCleanup() {
        if (imgui_gl)  { ImGui_ImplOpenGL3_Shutdown(); }
        if (imgui_sw)  { ImGui_ImplSDLRenderer3_Shutdown(); }
        if (imgui_sdl) { ImGui_ImplSDL3_Shutdown(); }
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }
        if (gl_ctx)       { SDL_GL_DestroyContext(gl_ctx); }
        if (sdl_renderer) { SDL_DestroyRenderer(sdl_renderer); }
        if (window)       { SDL_DestroyWindow(window); }
        SDL_Quit();
    }
};

// --pair 인자 검증 + 비교 디렉토리 계산. 유효하면 dir_b_out 채우고 true.
// 문제 시 stderr 출력 후 false (호출부에서 창 열기 전 fail-fast 종료).
static bool validate_pair_args(const CliOptions& cli, std::string& dir_b_out) {
    namespace fs = std::filesystem;
    if (cli.image_a.empty() || cli.image_b.empty()) {
        std::cerr << "--pair requires two paths: <imageA> <imageB-or-dirB>\n";
        return false;
    }
    std::error_code ec;
    fs::path dirA = fs::path(cli.image_a).parent_path();
    if (dirA.empty()) dirA = ".";
    fs::path pb   = cli.image_b;
    fs::path dirB = fs::is_directory(pb, ec) ? pb : pb.parent_path();
    if (dirB.empty()) dirB = ".";
    // 같은 디렉토리면 비교 의미 없음 → 경고 후 실패
    std::error_code e1, e2;
    fs::path cA = fs::weakly_canonical(dirA, e1);
    fs::path cB = fs::weakly_canonical(dirB, e2);
    bool same = (!e1 && !e2) ? (cA == cB) : (dirA == dirB);
    if (same) {
        std::cerr << "--pair: both directories are the same ("
                  << dirA.string() << "). Nothing to compare.\n";
        return false;
    }
    dir_b_out = dirB.string();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Parse CLI ─────────────────────────────────────────────────────────────
    CliOptions cli = parse_cli(argc, argv);

    // ── --pair 검증 (fail-fast: 창 열기 전에 인자/디렉토리 확인) ────────────────
    std::string pair_dir_b;
    if (cli.pair && !validate_pair_args(cli, pair_dir_b))
        return 1;

    // ── Determine software mode BEFORE SDL_Init ───────────────────────────────
    bool use_software = cli.software;
    if (!use_software) {
        const char* ssh = getenv("SSH_CONNECTION");
        if (ssh && ssh[0]) {
            use_software = true;
            std::cout << "[av] X11 forwarding detected (SSH) — using software renderer\n";
        }
    }

    // Install X11 error handler to survive MIT-SHM and GLX errors over SSH
#if defined(__linux__) || defined(__unix__)
    if (use_software) {
        XSetErrorHandler(x11_error_handler);
    }
#endif

    // Prevent SDL from probing GLX when we know we need software mode
    if (use_software) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint("SDL_VIDEO_X11_FORCE_EGL", "0");
        SDL_SetHint("SDL_OPENGL_ES_DRIVER", "0");
    }

    // ── SDL3 init ─────────────────────────────────────────────────────────────
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 2;
    }

    SdlCleanup cleanup;

    // ── Try OpenGL path ───────────────────────────────────────────────────────
    SDL_Window* window = nullptr;

    if (!use_software) {
        // OpenGL context attributes
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_MAJOR);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_MINOR);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                            SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

        // Create window — borderless by default (U key reveals UI overlay)
        SDL_WindowFlags win_flags = SDL_WINDOW_OPENGL |
                                    SDL_WINDOW_RESIZABLE |
                                    SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (!cli.windowed) {
            win_flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_MAXIMIZED;
        }
        if (cli.fullscreen) {
            win_flags |= SDL_WINDOW_FULLSCREEN;
        }

        window = SDL_CreateWindow(APP_TITLE, cli.win_w, cli.win_h, win_flags);
        if (window) {
            SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
            if (gl_ctx) {
                SDL_GL_MakeCurrent(window, gl_ctx);
                if (gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
                    // OpenGL success
                    SDL_GL_SetSwapInterval(1);
                    cleanup.gl_ctx = gl_ctx;
                    g_render_ctx.backend    = RenderBackend::OpenGL;
                    g_render_ctx.gl_context = gl_ctx;

                    std::cout << "OpenGL " << glGetString(GL_VERSION)
                              << "  GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
                    std::cout << "Renderer: " << glGetString(GL_RENDERER) << "\n";
                } else {
                    std::cerr << "[av] gladLoadGL failed — falling back to software renderer\n";
                    SDL_GL_DestroyContext(gl_ctx);
                    SDL_DestroyWindow(window);
                    window = nullptr;
                    use_software = true;
                }
            } else {
                std::cerr << "[av] GL context creation failed: " << SDL_GetError()
                          << " — falling back to software renderer\n";
                SDL_DestroyWindow(window);
                window = nullptr;
                use_software = true;
            }
        } else {
            std::cerr << "[av] GL window creation failed: " << SDL_GetError()
                      << " — falling back to software renderer\n";
            use_software = true;
        }
    }

    // ── Software renderer fallback ────────────────────────────────────────────
    if (use_software) {
        SDL_WindowFlags win_flags = SDL_WINDOW_RESIZABLE |
                                    SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (!cli.windowed) {
            win_flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_MAXIMIZED;
        }
        if (cli.fullscreen) {
            win_flags |= SDL_WINDOW_FULLSCREEN;
        }

        window = SDL_CreateWindow(APP_TITLE, cli.win_w, cli.win_h, win_flags);
        if (!window) {
            std::cerr << "SDL_CreateWindow (software) failed: " << SDL_GetError() << "\n";
            return 2;
        }

        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_Renderer* renderer = SDL_CreateRenderer(window, "software");
        if (!renderer) {
            std::cerr << "SDL_CreateRenderer (software) failed: " << SDL_GetError() << "\n";
            return 2;
        }

        cleanup.sdl_renderer     = renderer;
        g_render_ctx.backend     = RenderBackend::Software;
        g_render_ctx.sdl_renderer = renderer;

        std::cout << "Using SDL software renderer (no OpenGL)\n";
    }
    cleanup.window = window;

    // Set procedural app icon
    if (SDL_Surface* icon = create_app_icon()) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = ".av_imgui.ini";

    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding     = 4.0f;
    style.FrameRounding      = 3.0f;
    style.ScrollbarRounding  = 3.0f;
    style.GrabRounding       = 3.0f;
    style.TabRounding        = 3.0f;
    style.WindowBorderSize   = 0.0f;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (is_software_mode()) {
        if (!ImGui_ImplSDL3_InitForSDLRenderer(window, g_render_ctx.sdl_renderer)) {
            std::cerr << "ImGui_ImplSDL3_InitForSDLRenderer failed\n";
            return 2;
        }
        cleanup.imgui_sdl = true;

        if (!ImGui_ImplSDLRenderer3_Init(g_render_ctx.sdl_renderer)) {
            std::cerr << "ImGui_ImplSDLRenderer3_Init failed\n";
            return 2;
        }
        cleanup.imgui_sw = true;
    } else {
        if (!ImGui_ImplSDL3_InitForOpenGL(window, g_render_ctx.gl_context)) {
            std::cerr << "ImGui_ImplSDL3_InitForOpenGL failed\n";
            return 2;
        }
        cleanup.imgui_sdl = true;

        if (!ImGui_ImplOpenGL3_Init(GLSL_VER)) {
            std::cerr << "ImGui_ImplOpenGL3_Init failed\n";
            return 2;
        }
        cleanup.imgui_gl = true;
    }

    // ── Application state ─────────────────────────────────────────────────────
    AppState state;
    load_app_ini(state);
    apply_cli_options(state, cli);
    state.window = window;

    // ── Fonts ─────────────────────────────────────────────────────────────────
    // Keep ProggyClean 13px as default (for existing UI elements)
    io.Fonts->AddFontDefault();
    // Roboto-Medium 26px for Image Info popup and Pixel Balloon
    state.font_large = io.Fonts->AddFontFromFileTTF(
        IMGUI_FONT_DIR "/Roboto-Medium.ttf", 26.0f);
    if (!state.font_large)
        state.font_large = io.Fonts->Fonts[0];  // fallback to default
    // Roboto-Medium 18px for Save/Open dialogs
    state.font_medium = io.Fonts->AddFontFromFileTTF(
        IMGUI_FONT_DIR "/Roboto-Medium.ttf", 18.0f);
    if (!state.font_medium)
        state.font_medium = io.Fonts->Fonts[0];  // fallback to default

    // Load images from CLI (공용 헬퍼 경유 — 시퀀스/토스트/viewport 일괄 처리)
    if (cli.pair) {
        // ── --pair 모드: A 로드 후 같은 파일명을 dirB에서 B로 미러 ─────────────
        // (인자/디렉토리 검증은 위 validate_pair_args 에서 이미 통과)
        state.pair_mode  = true;
        state.pair_dir_b = pair_dir_b;
        if (!load_image_and_populate_sequence(state, 0, cli.image_a)) {
            std::cerr << "Failed to load image A: " << cli.image_a << "\n";
        }
        pair_mirror_b(state, cli.image_a);
    } else {
        if (!cli.image_a.empty()) {
            if (!load_image_and_populate_sequence(state, 0, cli.image_a)) {
                std::cerr << "Failed to load image A: " << cli.image_a << "\n";
            }
        }
        if (!cli.image_b.empty()) {
            if (!load_image_and_populate_sequence(state, 1, cli.image_b)) {
                std::cerr << "Failed to load image B: " << cli.image_b << "\n";
            }
        }
    }

    // ── 초기 zoom 적용 (--zoom / .av.ini) ─────────────────────────────────────
    // 0=fit(기본, views 기본 fit=true 유지), >0=고정 배율. 로드가 매번 fit=true 로
    // 리셋하므로 최초 로드 직후 1회만 적용한다. (메뉴 "1:1 Pixel" 과 동일 경로)
    if (state.zoom_setting > 0.0f) {
        for (int i = 0; i < 2; ++i) {
            if (state.images[i].loaded) {
                viewport_set_zoom(state.views[i], state.zoom_setting);  // fit=false + clamp
                viewport_center(state.views[i]);
            }
        }
    }

    // ── Main window setup ─────────────────────────────────────────────────────
    MainWindow main_window;
    if (!main_window.init()) {
        std::cerr << "MainWindow init failed\n";
        return 2;
    }

    // ── Event loop ────────────────────────────────────────────────────────────
    while (!state.quit) {
        // Process events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                state.quit = true;
            } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    state.quit = true;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                bool ctrl  = (event.key.mod & SDL_KMOD_CTRL)  != 0;
                bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                bool alt   = (event.key.mod & SDL_KMOD_ALT)   != 0;
                bool gui   = (event.key.mod & SDL_KMOD_GUI)   != 0;
                // Global shortcuts (Cmd/Ctrl combos) always pass through;
                // plain keys deferred to ImGui when it wants keyboard
                if (!io.WantCaptureKeyboard || ctrl || gui) {
                    handle_keyboard(state,
                                    static_cast<int>(event.key.scancode),
                                    ctrl, shift, alt, gui);
                }
            } else if (event.type == SDL_EVENT_DROP_FILE) {
                const char* dropped = event.drop.data;
                if (dropped) {
                    int target = state.images[0].loaded ? 1 : 0;
                    load_image_and_populate_sequence(state, target, dropped);
                }
            } else if (event.type == SDL_EVENT_WINDOW_MOVED) {
                state.window_moving = true;
                state.last_window_move_tick = SDL_GetTicksNS();
            }
        }

        // 100ms간 WINDOW_MOVED 없으면 드래그 종료로 판정
        if (state.window_moving) {
            uint64_t now = SDL_GetTicksNS();
            if (now - state.last_window_move_tick > 100'000'000ULL) {
                state.window_moving = false;
            }
        }

        // ── Slideshow countdown ──────────────────────────────────────────────
        if (state.slideshow.active) {
            float dt = io.DeltaTime;
            state.slideshow.countdown -= dt;
            if (state.slideshow.countdown <= 0.0f) {
                sequence_navigate(state, state.slideshow.panel, +1);
                state.slideshow.countdown = state.slideshow.interval;
            }
        }

        // ── Skip rendering while window is being dragged ─────────────────
        if (state.window_moving) continue;

        // ── Window size ───────────────────────────────────────────────────────
        int fb_w = 0, fb_h = 0;
        SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);

        // ── Fit viewports if flagged ───────────────────────────────────────────
        for (int i = 0; i < 2; ++i) {
            auto& vp  = state.views[i];
            auto& img = state.images[i];
            if (vp.fit && img.loaded && fb_w > 0 && fb_h > 0) {
                int pw;
                bool both = state.images[0].loaded && state.images[1].loaded;
                if (both && state.diff.mode != DiffState::Mode::None)
                    pw = fb_w / 3;   // 3-panel: A | B | Diff
                else if (both)
                    pw = fb_w / 2;   // 2-panel: A | B
                else
                    pw = fb_w;       // 1-panel
                viewport_fit(vp, img.width, img.height, pw, fb_h);
            }
        }

        // ── ImGui frame ───────────────────────────────────────────────────────
        if (is_software_mode()) {
            ImGui_ImplSDLRenderer3_NewFrame();
        } else {
            ImGui_ImplOpenGL3_NewFrame();
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        main_window.render(state);

        // ── Render ────────────────────────────────────────────────────────────
        ImGui::Render();

        if (is_software_mode()) {
            SDL_SetRenderDrawColor(g_render_ctx.sdl_renderer, 25, 25, 25, 255);
            SDL_RenderClear(g_render_ctx.sdl_renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_render_ctx.sdl_renderer);
            SDL_RenderPresent(g_render_ctx.sdl_renderer);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fb_w, fb_h);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window);
        }
    }

    save_app_ini(state);

    // Cleanup happens via SdlCleanup RAII destructor
    return 0;
}
