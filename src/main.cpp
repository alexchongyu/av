#include "app.h"
#include "image_loader.h"
#include "viewport.h"
#include "ui/main_window.h"

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <cstdio>

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr const char* APP_TITLE   = "av — Alex's Viewer";
static constexpr int         GL_MAJOR    = 3;
static constexpr int         GL_MINOR    = 3;
static constexpr const char* GLSL_VER   = "#version 150";

// ─── Cleanup helper ───────────────────────────────────────────────────────────
struct SdlCleanup {
    SDL_Window*   window   = nullptr;
    SDL_GLContext gl_ctx   = nullptr;
    bool          imgui_gl = false;
    bool          imgui_sdl = false;

    ~SdlCleanup() {
        if (imgui_gl)  { ImGui_ImplOpenGL3_Shutdown(); }
        if (imgui_sdl) { ImGui_ImplSDL3_Shutdown(); }
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }
        if (gl_ctx)  { SDL_GL_DestroyContext(gl_ctx); }
        if (window)  { SDL_DestroyWindow(window); }
        SDL_Quit();
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Parse CLI ─────────────────────────────────────────────────────────────
    CliOptions cli = parse_cli(argc, argv);

    // ── SDL3 init ─────────────────────────────────────────────────────────────
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 2;
    }

    SdlCleanup cleanup;

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
                                SDL_WINDOW_BORDERLESS |
                                SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (cli.fullscreen) {
        win_flags |= SDL_WINDOW_FULLSCREEN;
    }

    SDL_Window* window = SDL_CreateWindow(APP_TITLE,
                                          cli.win_w, cli.win_h,
                                          win_flags);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return 2;
    }
    cleanup.window = window;

    // ── OpenGL context ────────────────────────────────────────────────────────
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        return 2;
    }
    cleanup.gl_ctx = gl_ctx;

    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1);  // vsync

    // ── glad ──────────────────────────────────────────────────────────────────
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
        std::cerr << "gladLoadGL failed\n";
        return 2;
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION)
              << "  GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << "\n";

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = "av_imgui.ini";

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

    if (!ImGui_ImplSDL3_InitForOpenGL(window, gl_ctx)) {
        std::cerr << "ImGui_ImplSDL3_InitForOpenGL failed\n";
        return 2;
    }
    cleanup.imgui_sdl = true;

    if (!ImGui_ImplOpenGL3_Init(GLSL_VER)) {
        std::cerr << "ImGui_ImplOpenGL3_Init failed\n";
        return 2;
    }
    cleanup.imgui_gl = true;

    // ── Application state ─────────────────────────────────────────────────────
    AppState state;
    apply_cli_options(state, cli);

    // Load images from CLI
    if (!cli.image_a.empty()) {
        if (!load_image(cli.image_a, state.images[0])) {
            std::cerr << "Failed to load image A: " << cli.image_a << "\n";
        }
    }
    if (!cli.image_b.empty()) {
        if (!load_image(cli.image_b, state.images[1])) {
            std::cerr << "Failed to load image B: " << cli.image_b << "\n";
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
                // Don't pass to app if ImGui captured it
                if (!io.WantCaptureKeyboard) {
                    bool ctrl  = (event.key.mod & SDL_KMOD_CTRL)  != 0;
                    bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                    bool alt   = (event.key.mod & SDL_KMOD_ALT)   != 0;
                    handle_keyboard(state,
                                    static_cast<int>(event.key.scancode),
                                    ctrl, shift, alt);
                }
            } else if (event.type == SDL_EVENT_DROP_FILE) {
                const char* dropped = event.drop.data;
                if (dropped) {
                    if (!state.images[0].loaded) {
                        load_image(dropped, state.images[0]);
                    } else {
                        load_image(dropped, state.images[1]);
                    }
                }
            }
        }

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
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        main_window.render(state);

        // ── Render ────────────────────────────────────────────────────────────
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    // Cleanup happens via SdlCleanup RAII destructor
    return 0;
}
