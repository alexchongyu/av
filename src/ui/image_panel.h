#pragma once

#include "../app.h"
#include "../gl_texture.h"
#include "../diff_engine.h"
#include "../render_backend.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <vector>
#include <cstdint>

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
    void handle_mouse_double_click(AppState& state, int panel_idx,
                                   ImVec2 widget_pos, int view_w, int view_h,
                                   int img_w, int img_h);
    void handle_mouse_right_select(AppState& state, int panel_idx,
                                   ImVec2 widget_pos, int view_w, int view_h,
                                   int img_w, int img_h,
                                   int context_type = -1);
    void render_context_popup(AppState& state, int context_type);
    void copy_panel_to_clipboard(AppState& state, int target_type);
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

    // Software renderer state
    std::vector<uint8_t> soft_buf_;      // CPU render buffer (RGBA8)
    SDL_Texture*         soft_texture_ = nullptr;
    int                  soft_w_ = 0;
    int                  soft_h_ = 0;

    void render_single_software(AppState& state, int panel_idx);
    void render_diff_software(AppState& state);
    void render_overlay_software(AppState& state);
    void render_ssim_software(AppState& state);

    void cpu_render_image(const ImageEntry& img, const ViewportState& vp,
                          uint8_t* buf, int view_w, int view_h,
                          ChannelMode channel = ChannelMode::RGB);
    void cpu_render_diff(const ImageEntry& imgA, const ImageEntry& imgB,
                         const ViewportState& vp, uint8_t* buf,
                         int view_w, int view_h,
                         DiffState::Mode mode, float amplify,
                         ChannelMode channel,
                         float enh_min = 0.0f, float enh_max = 0.0f);
    void cpu_render_overlay(const ImageEntry& imgA, const ImageEntry& imgB,
                            const ViewportState& vp, uint8_t* buf,
                            int view_w, int view_h,
                            const OverlayState& overlay, ChannelMode channel);

    SDL_Texture* ensure_soft_texture(int w, int h);

    // Right-click drag-to-zoom state
    bool   drag_selecting_  = false;
    ImVec2 drag_start_      = {0.0f, 0.0f};
    int    drag_panel_idx_  = 0;
    int    context_panel_type_ = -1;  // last context menu target (-1=none, 0=A, 1=B, 2=Diff)
    double right_press_time_   = 0.0; // right-click press timestamp for long-press detection

    void render_crosshair(const AppState& state, int panel_idx,
                          ImVec2 widget_pos, int view_w, int view_h,
                          int img_w, int img_h,
                          bool is_diff_panel = false);

    void render_magnifier(const AppState& state, int panel_idx,
                          ImVec2 widget_pos, int view_w, int view_h,
                          int img_w, int img_h, bool is_diff_panel,
                          bool img_hovered);

    void update_mouse_constraint(AppState& state, int panel_idx,
                                 ImVec2 widget_pos, int view_w, int view_h,
                                 int img_w, int img_h, bool img_hovered);

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
