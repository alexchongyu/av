#include "main_window.h"
#include "image_panel.h"
#include "statusbar.h"
#include "../image_loader.h"
#include "../viewport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <iostream>
#include <cstring>

// ─── Static panel instances ───────────────────────────────────────────────────
static ImagePanel s_panel_left;
static ImagePanel s_panel_right;
static ImagePanel s_panel_diff;
static StatusBar  s_statusbar;

// ─── SSIM computer ────────────────────────────────────────────────────────────
static SSIMComputer s_ssim_computer;
static bool          s_ssim_ready  = false;
static SSIMResult    s_ssim_result;

// ─── init ─────────────────────────────────────────────────────────────────────

bool MainWindow::init() {
    if (!diff_renderer_.init()) {
        std::cerr << "[MainWindow] DiffRenderer init failed\n";
        return false;
    }
    if (!s_panel_left.init()) {
        std::cerr << "[MainWindow] left panel init failed\n";
        return false;
    }
    if (!s_panel_right.init()) {
        std::cerr << "[MainWindow] right panel init failed\n";
        return false;
    }
    if (!s_panel_diff.init()) {
        std::cerr << "[MainWindow] diff panel init failed\n";
        return false;
    }
    inited_ = true;
    return true;
}

// ─── render_menubar ───────────────────────────────────────────────────────────

void MainWindow::render_menubar(AppState& state) {
    if (!ImGui::BeginMenuBar()) return;

    // ── File ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Image A…", "O")) {
            // TODO: SDL_ShowOpenFileDialog
        }
        if (ImGui::MenuItem("Open Image B…", "Shift+O")) {
            // TODO
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Q")) {
            state.quit = true;
        }
        ImGui::EndMenu();
    }

    // ── View ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit to Window", "0")) {
            auto& a = state.images[0]; auto& b = state.images[1];
            if (a.loaded) state.views[0].fit = true;
            if (b.loaded) state.views[1].fit = true;
        }
        if (ImGui::MenuItem("1:1 Pixel", "1")) {
            state.views[0].zoom = 1.0f;
            state.views[1].zoom = 1.0f;
            viewport_center(state.views[0]);
            viewport_center(state.views[1]);
        }
        ImGui::Separator();
        ImGui::MenuItem("Sync Viewports", "S", &state.sync_viewports);
        ImGui::Separator();
        ImGui::MenuItem("Show Pixel Info", "P", &state.show_pixel_info);
        ImGui::MenuItem("Show Image Info", "I", &state.show_info);
        ImGui::EndMenu();
    }

    // ── Diff ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("Diff")) {
        bool none = state.diff.mode == DiffState::Mode::None;
        bool abs_ = state.diff.mode == DiffState::Mode::PixelAbsolute;
        bool rel_ = state.diff.mode == DiffState::Mode::PixelRelative;
        bool fc_  = state.diff.mode == DiffState::Mode::FalseColor;
        bool ssim = state.diff.mode == DiffState::Mode::SSIM;

        if (ImGui::MenuItem("Off",        "Ctrl+D", none)) state.diff.mode = DiffState::Mode::None;
        if (ImGui::MenuItem("Absolute",   "Ctrl+3", abs_)) state.diff.mode = DiffState::Mode::PixelAbsolute;
        if (ImGui::MenuItem("Relative",   "Ctrl+4", rel_)) state.diff.mode = DiffState::Mode::PixelRelative;
        if (ImGui::MenuItem("FalseColor", "Ctrl+5", fc_ )) state.diff.mode = DiffState::Mode::FalseColor;
        if (ImGui::MenuItem("SSIM",       "Ctrl+6", ssim)) state.diff.mode = DiffState::Mode::SSIM;
        ImGui::Separator();
        ImGui::SliderFloat("Amplify", &state.diff.amplify, 0.5f, 50.0f, "%.1f");
        ImGui::EndMenu();
    }

    // FPS in menu bar right side
    char fps[32];
    std::snprintf(fps, sizeof(fps), "%.0f fps  |  U: hide UI", ImGui::GetIO().Framerate);
    float fps_w = ImGui::CalcTextSize(fps).x;
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - fps_w - 8.0f);
    ImGui::TextDisabled("%s", fps);

    ImGui::EndMenuBar();
}

// ─── render ───────────────────────────────────────────────────────────────────

void MainWindow::render(AppState& state) {
    if (!inited_) return;

    // ── Upload SSIM result on main thread if ready ────────────────────────────
    if (s_ssim_ready) {
        s_ssim_ready = false;
        if (state.diff.ssim_texture_id) {
            GLuint id = state.diff.ssim_texture_id;
            glDeleteTextures(1, &id);
            state.diff.ssim_texture_id = 0;
        }
        if (s_ssim_result.success && !s_ssim_result.heatmap.empty()) {
            state.diff.ssim_texture_id = gl_upload_texture_r32f(
                s_ssim_result.heatmap.data(),
                s_ssim_result.w,
                s_ssim_result.h);
        }
        state.diff.ssim_score     = s_ssim_result.score;
        state.diff.ssim_computing = false;
    }

    // ── Trigger SSIM computation when mode switches ───────────────────────────
    static DiffState::Mode prev_diff_mode = DiffState::Mode::None;
    if (state.diff.mode == DiffState::Mode::SSIM &&
        prev_diff_mode != DiffState::Mode::SSIM &&
        state.images[0].loaded && state.images[1].loaded) {
        state.diff.ssim_computing = true;
        state.diff.ssim_score     = -1.0f;
        s_ssim_computer.compute(state.images[0], state.images[1],
            [](SSIMResult result) {
                s_ssim_result = std::move(result);
                s_ssim_ready  = true;
            });
    }
    prev_diff_mode = state.diff.mode;

    // ── Full-screen host window ───────────────────────────────────────────────
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoCollapse      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus      |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (state.show_ui) {
        host_flags |= ImGuiWindowFlags_MenuBar;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
    ImGui::Begin("##Host", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    if (state.show_ui) {
        render_menubar(state);
    }

    // ── Layout: determine panel sizes ─────────────────────────────────────────
    bool two_images = state.images[0].loaded && state.images[1].loaded;
    bool diff_mode  = (state.diff.mode != DiffState::Mode::None);

    // Reserve statusbar height when UI is visible
    static constexpr float STATUSBAR_H = 24.0f;
    ImVec2 content = ImGui::GetContentRegionAvail();
    float panel_h = state.show_ui ? (content.y - STATUSBAR_H) : content.y;
    if (panel_h < 1.0f) panel_h = 1.0f;

    ImGuiWindowFlags child_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (two_images && diff_mode) {
        // ── 3-panel: A | B | Diff ────────────────────────────────────────────
        float third_w = std::floor(content.x / 3.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(third_w, panel_h), false, child_flags);
        s_panel_left.render(state, 0, diff_renderer_, true);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelRight", ImVec2(third_w, panel_h), false, child_flags);
        s_panel_right.render(state, 1, diff_renderer_, true);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelDiff", ImVec2(0.0f, panel_h), false, child_flags);
        s_panel_diff.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else if (two_images) {
        // ── Side-by-side: A | B ──────────────────────────────────────────────
        float half_w = std::floor(content.x * 0.5f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(half_w, panel_h), false, child_flags);
        s_panel_left.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelRight", ImVec2(0.0f, panel_h), false, child_flags);
        s_panel_right.render(state, 1, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else {
        // ── Single image: full width ─────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(0.0f, panel_h), false, child_flags);
        s_panel_left.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ── Statusbar ─────────────────────────────────────────────────────────────
    if (state.show_ui) {
        s_statusbar.render(state);
    }

    // ── Image info popup ──────────────────────────────────────────────────────
    if (state.show_info && (state.images[0].loaded || state.images[1].loaded)) {
        ImGui::SetNextWindowSize(ImVec2(340, 120), ImGuiCond_Always);
        if (ImGui::Begin("Image Info", &state.show_info)) {
            for (int i = 0; i < 2; ++i) {
                const auto& img = state.images[i];
                if (img.loaded) {
                    ImGui::Text("%s: %s", i == 0 ? "A" : "B",
                                img.path.empty() ? "(loaded)" : img.path.c_str());
                    ImGui::Text("   %d x %d  ch=%d%s",
                                img.width, img.height, img.channels,
                                img.is_hdr ? "  [HDR]" : "");
                }
            }
        }
        ImGui::End();
    }

    ImGui::End(); // ##Host

    // ── Zoom HUD ──────────────────────────────────────────────────────────────
    {
        float dt = ImGui::GetIO().DeltaTime;

        // Detect zoom changes (ignore fit-mode changes from window resize)
        bool zoom_changed = false;
        if (!state.views[0].fit && state.views[0].zoom != state.last_zoom_a) zoom_changed = true;
        if (!state.views[1].fit && state.views[1].zoom != state.last_zoom_b) zoom_changed = true;
        state.last_zoom_a = state.views[0].zoom;
        state.last_zoom_b = state.views[1].zoom;

        if (zoom_changed) state.zoom_hud_timer = 5.0f;

        if (state.zoom_hud_timer > 0.0f) {
            state.zoom_hud_timer -= dt;
            if (state.zoom_hud_timer < 0.0f) state.zoom_hud_timer = 0.0f;

            float alpha = (state.zoom_hud_timer > 1.0f) ? 1.0f : state.zoom_hud_timer;

            float zoom_val = state.views[state.active_panel].zoom;
            char buf[32];
            if (zoom_val >= 1.0f)
                std::snprintf(buf, sizeof(buf), "%.0fx", zoom_val);
            else
                std::snprintf(buf, sizeof(buf), "%.2fx", zoom_val);

            ImVec2 text_size = ImGui::CalcTextSize(buf);
            float pad_x = 16.0f, pad_y = 8.0f;
            float pill_w = text_size.x + pad_x * 2.0f;
            float pill_h = text_size.y + pad_y * 2.0f;

            const ImGuiViewport* mvp = ImGui::GetMainViewport();
            ImVec2 pill_pos(mvp->WorkPos.x + (mvp->WorkSize.x - pill_w) * 0.5f,
                            mvp->WorkPos.y + 12.0f);

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(pill_pos,
                              ImVec2(pill_pos.x + pill_w, pill_pos.y + pill_h),
                              IM_COL32(0, 0, 0, (int)(180 * alpha)),
                              pill_h * 0.5f);
            dl->AddText(ImVec2(pill_pos.x + pad_x, pill_pos.y + pad_y),
                        IM_COL32(255, 255, 255, (int)(255 * alpha)),
                        buf);
        }
    }
}
