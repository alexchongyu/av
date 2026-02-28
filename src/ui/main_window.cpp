#include "main_window.h"
#include "image_panel.h"
#include "statusbar.h"
#include "chart_windows.h"
#include "../image_loader.h"
#include "../viewport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <iostream>
#include <cstring>
#include <cmath>
#include <cstdio>

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
        ImGui::MenuItem("Show Pathfinder", "P", &state.show_pathfinder);
        ImGui::MenuItem("Show Image Info", "I", &state.show_info);
        ImGui::MenuItem("Show Pixel Info", "V", &state.show_pixel_info);
        ImGui::Separator();
        ImGui::MenuItem("Show Histogram",  "Ctrl+H", &state.show_histogram);
        ImGui::MenuItem("Show H-Line Cut",  "Ctrl+L", &state.show_hline_cut);
        ImGui::MenuItem("Show V-Line Cut",  "Ctrl+Y", &state.show_vline_cut);
        ImGui::MenuItem("Show Statistics",  "Ctrl+S", &state.show_stats);
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

    // Capture panels area origin for pixel balloon (used after ImGui::End())
    ImVec2 panels_origin = ImGui::GetCursorScreenPos();

    ImGuiWindowFlags child_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    // Panel screen-space rects for always-visible borders (drawn after all children)
    struct PanelBorderRect { ImVec2 min, max; ImU32 col; };
    PanelBorderRect panel_rects[3];
    int panel_rect_count = 0;

    if (two_images && diff_mode) {
        // ── 3-panel: A | B | Diff ────────────────────────────────────────────
        float third_w = std::floor(content.x / 3.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(third_w, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + third_w, p.y + panel_h), (ImU32)state.border_colors[0]}; }
        s_panel_left.render(state, 0, diff_renderer_, true);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelRight", ImVec2(third_w, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + third_w, p.y + panel_h), (ImU32)state.border_colors[1]}; }
        s_panel_right.render(state, 1, diff_renderer_, true);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelDiff", ImVec2(0.0f, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); float dw = content.x - 2.0f * third_w; panel_rects[panel_rect_count++] = {p, ImVec2(p.x + dw, p.y + panel_h), (ImU32)state.border_colors[2]}; }
        s_panel_diff.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else if (two_images) {
        // ── Side-by-side: A | B ──────────────────────────────────────────────
        float half_w = std::floor(content.x * 0.5f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(half_w, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + half_w, p.y + panel_h), (ImU32)state.border_colors[0]}; }
        s_panel_left.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelRight", ImVec2(0.0f, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); float rw = content.x - half_w; panel_rects[panel_rect_count++] = {p, ImVec2(p.x + rw, p.y + panel_h), (ImU32)state.border_colors[1]}; }
        s_panel_right.render(state, 1, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else {
        // ── Single image: full width ─────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(0.0f, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + content.x, p.y + panel_h), (ImU32)state.border_colors[0]}; }
        s_panel_left.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ── Always-visible panel borders (foreground → rendered above all children) ──
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 shadow = IM_COL32(0, 0, 0, 160);
        for (int i = 0; i < panel_rect_count; ++i) {
            const auto& pr = panel_rects[i];
            dl->AddRect(ImVec2(pr.min.x + 1, pr.min.y + 1),
                        ImVec2(pr.max.x + 1, pr.max.y + 1), shadow, 0.0f, 0, 3.0f);
            dl->AddRect(pr.min, pr.max, pr.col, 0.0f, 0, 2.0f);
        }
    }

    // ── Statusbar ─────────────────────────────────────────────────────────────
    if (state.show_ui) {
        s_statusbar.render(state);
    }

    // ── Image info popup ──────────────────────────────────────────────────────
    if (state.show_info && (state.images[0].loaded || state.images[1].loaded)) {
        ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("Image Info", &state.show_info)) {
            if (state.font_large) ImGui::PushFont(state.font_large);
            for (int i = 0; i < 2; ++i) {
                const auto& img = state.images[i];
                if (img.loaded) {
                    ImVec4 label_col = (i == 0) ? ImVec4(1,0.4f,1,1) : ImVec4(1,1,0.3f,1);
                    ImGui::TextColored(label_col, "%s:", i == 0 ? "A(Left)" : "B(Right)");
                    ImGui::SameLine();
                    ImGui::Text("%s", img.path.empty() ? "(loaded)" : img.path.c_str());
                    ImGui::Text("   %d x %d  ch=%d%s",
                                img.width, img.height, img.channels,
                                img.is_hdr ? "  [HDR]" : "");
                }
            }
            ImGui::Separator();
            for (int i = 0; i < 2; ++i) {
                const auto& v   = state.views[i];
                const auto& img = state.images[i];
                if (img.loaded) {
                    ImVec4 label_col = (i == 0) ? ImVec4(1,0.4f,1,1) : ImVec4(1,1,0.3f,1);
                    ImVec4 zoom_col  = ImVec4(0.3f, 1.0f, 1.0f, 1.0f);
                    ImGui::TextColored(label_col, "%s", i == 0 ? "A" : "B");
                    ImGui::SameLine();
                    if (v.fit) {
                        ImGui::TextColored(zoom_col, "Zoom: Fit-to-Window");
                    } else {
                        ImGui::TextColored(zoom_col, "Zoom: %.1f%%", v.zoom * 100.0f);
                        ImGui::SameLine();
                        ImGui::Text("Pan: (%.0f, %.0f)", v.pan_x, v.pan_y);
                    }
                }
            }
            if (state.font_large) ImGui::PopFont();
        }
        ImGui::End();
    }

    // ── Chart windows (floating) ───────────────────────────────────────────────
    render_histogram_window(state);
    render_hline_cut_window(state);
    render_vline_cut_window(state);
    render_stats_window(state);

    ImGui::End(); // ##Host

    // ── Pixel value balloon ───────────────────────────────────────────────────
    if (state.show_pixel_info) {
        if (state.font_large) ImGui::PushFont(state.font_large);
        do {
            ImVec2 mouse = ImGui::GetMousePos();
            const ImGuiViewport* mvp = ImGui::GetMainViewport();
            float total_w = mvp->WorkSize.x;

            float px = mouse.x - panels_origin.x;
            float py = mouse.y - panels_origin.y;

            if (py < 0.0f || py >= panel_h || px < 0.0f || px >= total_w) break;

            // Determine which panel the mouse is over
            int   panel_idx    = 0;
            float panel_x_start = 0.0f;
            float panel_w_local = total_w;
            bool  is_diff_panel = false;

            char line_pos[32];
            char line_r[24], line_g[24], line_b[24];

            if (two_images && diff_mode) {
                float third_w = std::floor(total_w / 3.0f);
                if (px < third_w) {
                    panel_idx = 0; panel_x_start = 0.0f;          panel_w_local = third_w;
                } else if (px < 2.0f * third_w) {
                    panel_idx = 1; panel_x_start = third_w;        panel_w_local = third_w;
                } else {
                    is_diff_panel = true;
                    panel_x_start = 2.0f * third_w;
                    panel_w_local = total_w - 2.0f * third_w;
                }
            } else if (two_images) {
                float half_w = std::floor(total_w * 0.5f);
                if (px < half_w) {
                    panel_idx = 0; panel_x_start = 0.0f;    panel_w_local = half_w;
                } else {
                    panel_idx = 1; panel_x_start = half_w;  panel_w_local = total_w - half_w;
                }
            }

            if (is_diff_panel) {
                // diff panel: compute abs(A - B), using views[0] for coordinate mapping
                const ViewportState& bvp = state.views[0];
                const ImageEntry& imgA = state.images[state.swap_images ? 1 : 0];
                const ImageEntry& imgB = state.images[state.swap_images ? 0 : 1];
                if (!imgA.loaded || !imgB.loaded) break;

                float view_w  = panel_w_local;
                float view_h  = static_cast<float>(panel_h);
                float half_vw = view_w * 0.5f;
                float half_vh = view_h * 0.5f;
                float half_iw = imgA.width  * 0.5f;
                float half_ih = imgA.height * 0.5f;

                float local_x = px - panel_x_start;
                float local_y = py;
                float img_fx = (local_x - half_vw) / bvp.zoom - bvp.pan_x + half_iw;
                float img_fy = (local_y - half_vh) / bvp.zoom - bvp.pan_y + half_ih;

                int ix = static_cast<int>(std::floor(img_fx));
                int iy = static_cast<int>(std::floor(img_fy));
                if (ix < 0 || ix >= imgA.width || iy < 0 || iy >= imgA.height) break;
                if (ix >= imgB.width || iy >= imgB.height) break;

                int pidx_a = (iy * imgA.width + ix) * 4;
                int pidx_b = (iy * imgB.width + ix) * 4;

                std::snprintf(line_pos, sizeof(line_pos), "(%d, %d)", ix, iy);

                bool is_hdr  = imgA.is_hdr && imgB.is_hdr;
                bool has_f32 = !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
                bool has_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

                if (is_hdr && has_f32) {
                    std::snprintf(line_r, sizeof(line_r), "R:%.3f",
                        std::fabsf(imgA.pixels_f32[pidx_a + 0] - imgB.pixels_f32[pidx_b + 0]));
                    std::snprintf(line_g, sizeof(line_g), "G:%.3f",
                        std::fabsf(imgA.pixels_f32[pidx_a + 1] - imgB.pixels_f32[pidx_b + 1]));
                    std::snprintf(line_b, sizeof(line_b), "B:%.3f",
                        std::fabsf(imgA.pixels_f32[pidx_a + 2] - imgB.pixels_f32[pidx_b + 2]));
                } else if (has_u8) {
                    std::snprintf(line_r, sizeof(line_r), "R:%d",
                        std::abs((int)imgA.pixels[pidx_a + 0] - (int)imgB.pixels[pidx_b + 0]));
                    std::snprintf(line_g, sizeof(line_g), "G:%d",
                        std::abs((int)imgA.pixels[pidx_a + 1] - (int)imgB.pixels[pidx_b + 1]));
                    std::snprintf(line_b, sizeof(line_b), "B:%d",
                        std::abs((int)imgA.pixels[pidx_a + 2] - (int)imgB.pixels[pidx_b + 2]));
                } else {
                    break;
                }
            } else {
                int img_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
                if (img_idx < 0 || img_idx > 1) break;
                const ImageEntry&    bimg = state.images[img_idx];
                if (!bimg.loaded) break;

                const ViewportState& bvp = state.views[panel_idx];

                float view_w  = panel_w_local;
                float view_h  = static_cast<float>(panel_h);
                float half_vw = view_w * 0.5f;
                float half_vh = view_h * 0.5f;
                float half_iw = bimg.width  * 0.5f;
                float half_ih = bimg.height * 0.5f;

                // Screen → image pixel coordinates
                float local_x = px - panel_x_start;
                float local_y = py;
                float img_fx = (local_x - half_vw) / bvp.zoom - bvp.pan_x + half_iw;
                float img_fy = (local_y - half_vh) / bvp.zoom - bvp.pan_y + half_ih;

                int ix = static_cast<int>(std::floor(img_fx));
                int iy = static_cast<int>(std::floor(img_fy));
                if (ix < 0 || ix >= bimg.width || iy < 0 || iy >= bimg.height) break;

                int pidx = (iy * bimg.width + ix) * 4;

                std::snprintf(line_pos, sizeof(line_pos), "(%d, %d)", ix, iy);

                if (bimg.is_hdr && !bimg.pixels_f32.empty()) {
                    std::snprintf(line_r, sizeof(line_r), "R:%.3f", bimg.pixels_f32[pidx + 0]);
                    std::snprintf(line_g, sizeof(line_g), "G:%.3f", bimg.pixels_f32[pidx + 1]);
                    std::snprintf(line_b, sizeof(line_b), "B:%.3f", bimg.pixels_f32[pidx + 2]);
                } else if (!bimg.pixels.empty()) {
                    std::snprintf(line_r, sizeof(line_r), "R:%d", (int)bimg.pixels[pidx + 0]);
                    std::snprintf(line_g, sizeof(line_g), "G:%d", (int)bimg.pixels[pidx + 1]);
                    std::snprintf(line_b, sizeof(line_b), "B:%d", (int)bimg.pixels[pidx + 2]);
                } else {
                    break;
                }
            }

            // Measure text for balloon sizing
            ImVec2 sz_pos = ImGui::CalcTextSize(line_pos);
            ImVec2 sz_r   = ImGui::CalcTextSize(line_r);
            ImVec2 sz_g   = ImGui::CalcTextSize(line_g);
            ImVec2 sz_b   = ImGui::CalcTextSize(line_b);

            float pad     = 12.0f;
            float gap_y   = 6.0f;
            float rgb_gap = 8.0f;
            float font_h  = ImGui::GetFontSize();

            float line2_w = sz_r.x + rgb_gap + sz_g.x + rgb_gap + sz_b.x;
            float content_w = (sz_pos.x > line2_w ? sz_pos.x : line2_w);
            float box_w   = content_w + pad * 2.0f;
            float box_h   = font_h + gap_y + font_h + pad * 2.0f;

            // Position: upper-right of cursor; clamp to screen edges
            float bx = mouse.x + 16.0f;
            float by = mouse.y - 40.0f;
            if (bx + box_w > mvp->WorkPos.x + mvp->WorkSize.x)
                bx = mouse.x - box_w - 8.0f;
            if (by < mvp->WorkPos.y)
                by = mouse.y + 16.0f;
            if (by + box_h > mvp->WorkPos.y + mvp->WorkSize.y)
                by = mvp->WorkPos.y + mvp->WorkSize.y - box_h - 4.0f;

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + box_w, by + box_h),
                              IM_COL32(20, 20, 20, 200), 6.0f);
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + box_w, by + box_h),
                        IM_COL32(80, 80, 80, 180), 6.0f);

            ImFont* cur_font = ImGui::GetFont();

            // Line 1: coordinates
            dl->AddText(cur_font, font_h, ImVec2(bx + pad, by + pad),
                        IM_COL32(220, 220, 220, 255), line_pos);

            // Line 2: R G B in channel colours
            float y2 = by + pad + font_h + gap_y;
            float x2 = bx + pad;
            dl->AddText(cur_font, font_h, ImVec2(x2, y2), IM_COL32(255, 100, 100, 255), line_r);
            x2 += sz_r.x + rgb_gap;
            dl->AddText(cur_font, font_h, ImVec2(x2, y2), IM_COL32(100, 255, 100, 255), line_g);
            x2 += sz_g.x + rgb_gap;
            dl->AddText(cur_font, font_h, ImVec2(x2, y2), IM_COL32(100, 130, 255, 255), line_b);
        } while (false);
        if (state.font_large) ImGui::PopFont();
    }

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
