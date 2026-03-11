#pragma once

#include "../app.h"
#include "../gl_texture.h"
#include "../diff_engine.h"

#include <imgui.h>

// ─── ImagePanel ───────────────────────────────────────────────────────────────
// Renders one image into an off-screen FBO, then displays it via ImGui::Image.
// Handles pan/zoom via ViewportState and mouse interaction.

class ImagePanel {
public:
    ImagePanel() = default;
    ~ImagePanel() = default;

    // Call after GL context + shaders are ready.
    bool init();

    // Render the panel for images[panel_idx].
    // diff_renderer: used when diff mode is active (panel_idx == 0 uses both images).
    // force_single: when true, always render the single image (ignore diff mode).
    void render(AppState& state, int panel_idx, DiffRenderer& diff_renderer,
                bool force_single = false);

    bool valid() const { return inited_; }

private:
    void render_single(AppState& state, int panel_idx);
    void render_diff  (AppState& state, DiffRenderer& diff_renderer);

    void handle_mouse_pan(AppState& state, int panel_idx);
    void handle_mouse_right_select(AppState& state, int panel_idx,
                                   ImVec2 widget_pos, int view_w, int view_h,
                                   int img_w, int img_h);
    void render_pixel_values(const AppState& state, int panel_idx,
                             ImVec2 widget_pos, int view_w, int view_h);
    void render_diff_pixel_values(const AppState& state,
                                  ImVec2 widget_pos, int view_w, int view_h);
    void render_pathfinder(const AppState& state, int panel_idx,
                           ImVec2 widget_pos, int view_w, int view_h);

    ShaderProgram image_shader_;
    ShaderProgram blend_shader_;  // Overlay/Blend 모드용 셰이더
    ScreenQuad    quad_;
    FBO           fbo_;
    bool          inited_         = false;

    // Right-click drag-to-zoom state
    bool   drag_selecting_  = false;
    ImVec2 drag_start_      = {0.0f, 0.0f};
    int    drag_panel_idx_  = 0;

    // ROI drag state - stored in AppState.roi
    ImVec2 roi_drag_start_  = {0.0f, 0.0f};

    // Helpers
    void handle_roi_drag(AppState& state, int panel_idx,
                         ImVec2 widget_pos, int view_w, int view_h,
                         int img_w, int img_h);
    void render_roi_overlay(const AppState& state, int panel_idx,
                            ImVec2 widget_pos, int view_w, int view_h,
                            int img_w, int img_h);
    void render_overlay(AppState& state, DiffRenderer& diff_renderer);
};
