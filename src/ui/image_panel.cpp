#include "image_panel.h"
#include "../shader_sources.h"
#include "../viewport.h"

#include <imgui.h>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

// ─── init ─────────────────────────────────────────────────────────────────────

bool ImagePanel::init() {
    if (!image_shader_.compile(shaders::VERTEX_SRC, shaders::IMAGE_FRAG_SRC)) {
        std::cerr << "[ImagePanel] image shader compile failed\n";
        return false;
    }
    // Bind attribute locations for #version 150
    glBindAttribLocation(image_shader_.id, 0, "a_pos");
    glBindAttribLocation(image_shader_.id, 1, "a_uv");
    glLinkProgram(image_shader_.id);

    quad_.init();
    inited_ = true;
    return true;
}

// ─── Mouse pan helper ─────────────────────────────────────────────────────────

void ImagePanel::handle_mouse_pan(AppState& state, int panel_idx) {
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

        // Convert screen-pixel drag to image-pixel pan
        float dx = delta.x;
        float dy = delta.y;

        auto& v = state.views[panel_idx];
        v.pan_x = std::round(v.pan_x + dx / v.zoom);
        v.pan_y = std::round(v.pan_y + dy / v.zoom);
        v.fit    = false;

        if (state.sync_viewports) {
            auto& ov = state.views[1 - panel_idx];
            ov.pan_x = v.pan_x;
            ov.pan_y = v.pan_y;
        }
    }

    // Scroll to zoom
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            auto& v = state.views[panel_idx];
            if (wheel > 0) viewport_zoom_in(v);
            else           viewport_zoom_out(v);
            if (state.sync_viewports) {
                auto& ov = state.views[1 - panel_idx];
                ov.zoom = v.zoom;
                ov.fit  = false;
            }
        }
    }
}

// ─── Right-click drag-to-zoom ─────────────────────────────────────────────────
// Allows the user to drag-select a region with the right mouse button.
// On release the view snaps to the largest 2^n zoom that fits the selection.

void ImagePanel::handle_mouse_right_select(AppState& state, int panel_idx,
                                            ImVec2 widget_pos,
                                            int view_w, int view_h,
                                            int img_w, int img_h) {
    // Begin drag when right-click starts on this panel's image widget
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        drag_selecting_ = true;
        drag_start_     = ImGui::GetMousePos();
        drag_panel_idx_ = panel_idx;
    }

    // Only process/draw for the panel that owns the drag
    if (!drag_selecting_ || panel_idx != drag_panel_idx_) return;

    ImVec2 curr = ImGui::GetMousePos();
    float rx0 = std::min(drag_start_.x, curr.x);
    float ry0 = std::min(drag_start_.y, curr.y);
    float rx1 = std::max(drag_start_.x, curr.x);
    float ry1 = std::max(drag_start_.y, curr.y);

    // Draw selection overlay on foreground layer
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                      IM_COL32(255, 255, 255, 25));
    dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                IM_COL32(255, 220, 50, 220), 0.0f, 0, 1.5f);

    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Right)) return;

    drag_selecting_ = false;

    // Ignore tiny drags
    if ((rx1 - rx0) < 5.0f || (ry1 - ry0) < 5.0f) return;

    ViewportState& v = state.views[panel_idx];
    float zoom    = v.zoom;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    // Screen (absolute) → image-pixel coordinate
    // img_px = (abs_screen - widget_pos - half_view) / zoom - pan + half_img
    auto s2ix = [&](float sx) {
        return (sx - widget_pos.x - half_vw) / zoom - v.pan_x + half_iw;
    };
    auto s2iy = [&](float sy) {
        return (sy - widget_pos.y - half_vh) / zoom - v.pan_y + half_ih;
    };

    float ix0 = s2ix(rx0), iy0 = s2iy(ry0);
    float ix1 = s2ix(rx1), iy1 = s2iy(ry1);
    float sel_w = ix1 - ix0;
    float sel_h = iy1 - iy0;
    if (sel_w < 1.0f || sel_h < 1.0f) return;

    // Largest power-of-2 zoom that fits the selection
    float need_zoom = std::min(static_cast<float>(view_w) / sel_w,
                               static_cast<float>(view_h) / sel_h);
    float new_zoom  = std::pow(2.0f, std::floor(std::log2(need_zoom)));
    new_zoom = std::max(ZOOM_LEVELS[0],
               std::min(ZOOM_LEVELS[NUM_ZOOM_LEVELS - 1], new_zoom));

    // Pan so selection centre maps to viewport centre
    float sel_cx = (ix0 + ix1) * 0.5f;
    float sel_cy = (iy0 + iy1) * 0.5f;

    viewport_set_zoom(v, new_zoom);
    v.pan_x = half_iw - sel_cx;
    v.pan_y = half_ih - sel_cy;
    v.fit   = false;

    if (state.sync_viewports) {
        ViewportState& ov = state.views[1 - panel_idx];
        viewport_set_zoom(ov, new_zoom);
        ov.pan_x = v.pan_x;
        ov.pan_y = v.pan_y;
        ov.fit   = false;
    }
}

// ─── draw_image_border ────────────────────────────────────────────────────────
// Draws a thick border rect around the image extent in screen space.
// Transform (matches shader): screen_px = (img_px + pan - half_img) * zoom + half_view

static void draw_image_border(ImDrawList* dl, ImVec2 widget_pos,
                               float view_w, float view_h,
                               float img_w,  float img_h,
                               float pan_x,  float pan_y, float zoom,
                               ImU32 border_col) {
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    float x0 = widget_pos.x + (pan_x - half_iw) * zoom + half_vw;
    float y0 = widget_pos.y + (pan_y - half_ih) * zoom + half_vh;
    float x1 = widget_pos.x + (img_w + pan_x - half_iw) * zoom + half_vw;
    float y1 = widget_pos.y + (img_h + pan_y - half_ih) * zoom + half_vh;

    // Viewport bounds for clipping each edge independently
    float vx0 = widget_pos.x,          vy0 = widget_pos.y;
    float vx1 = widget_pos.x + view_w, vy1 = widget_pos.y + view_h;

    ImU32 shadow_col = IM_COL32(0, 0, 0, 160);

    // Vertical edge: draw only if x is inside viewport; clamp y range
    auto draw_v_edge = [&](float ex, float ey0, float ey1) {
        if (ex < vx0 || ex > vx1) return;
        float cy0 = std::max(ey0, vy0), cy1 = std::min(ey1, vy1);
        if (cy0 >= cy1) return;
        dl->AddLine(ImVec2(ex + 1.0f, cy0 + 1.0f), ImVec2(ex + 1.0f, cy1 + 1.0f), shadow_col, 3.0f);
        dl->AddLine(ImVec2(ex,         cy0),         ImVec2(ex,         cy1),         border_col, 2.0f);
    };
    // Horizontal edge: draw only if y is inside viewport; clamp x range
    auto draw_h_edge = [&](float ey, float ex0, float ex1) {
        if (ey < vy0 || ey > vy1) return;
        float cx0 = std::max(ex0, vx0), cx1 = std::min(ex1, vx1);
        if (cx0 >= cx1) return;
        dl->AddLine(ImVec2(cx0 + 1.0f, ey + 1.0f), ImVec2(cx1 + 1.0f, ey + 1.0f), shadow_col, 3.0f);
        dl->AddLine(ImVec2(cx0,         ey),         ImVec2(cx1,         ey),         border_col, 2.0f);
    };

    draw_v_edge(x0, y0, y1);  // left edge
    draw_v_edge(x1, y0, y1);  // right edge
    draw_h_edge(y0, x0, x1);  // top edge
    draw_h_edge(y1, x0, x1);  // bottom edge
}

// ─── render_pathfinder ────────────────────────────────────────────────────────
// 왼쪽 패널 하단에 미니맵 + 현재 뷰포트 인디케이터를 표시.
// vp.fit 상태이거나 panel_idx != 0이면 표시하지 않음.

void ImagePanel::render_pathfinder(const AppState& state, int panel_idx,
                                    ImVec2 widget_pos, int view_w, int view_h) {
    if (state.pathfinder_mode == 0) return;
    if (panel_idx != 0) return;

    int actual_idx = state.swap_images ? 1 : 0;
    const ImageEntry&    img = state.images[actual_idx];
    const ViewportState& vp  = state.views[panel_idx];

    if (!img.loaded || img.texture_id == 0) return;
    if (vp.fit) return;

    // Minimap size: max 150px wide, proportional height, max 30% panel height
    float mini_w = std::min(150.0f, view_w * 0.2f);
    float aspect = (img.width > 0) ? static_cast<float>(img.height) / img.width : 1.0f;
    float mini_h = mini_w * aspect;
    float max_h  = view_h * 0.3f;
    if (mini_h > max_h) {
        mini_h = max_h;
        mini_w = (aspect > 0.0f) ? mini_h / aspect : mini_h;
    }

    // Position: bottom-left, 8px margin
    constexpr float MARGIN = 8.0f;
    float mx0 = widget_pos.x + MARGIN;
    float my0 = widget_pos.y + view_h - mini_h - MARGIN;
    float mx1 = mx0 + mini_w;
    float my1 = my0 + mini_h;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 두 모드 모두 불투명 gray(32) 배경으로 잔상 방지
    dl->AddRectFilled(ImVec2(mx0, my0), ImVec2(mx1, my1),
                      IM_COL32(32, 32, 32, 255), 4.0f);
    if (state.pathfinder_mode == 1) {
        // Image 모드: gray 배경 위에 이미지 썸네일 추가
        ImTextureID tex_id = static_cast<ImTextureID>(img.texture_id);
        dl->AddImage(tex_id, ImVec2(mx0, my0), ImVec2(mx1, my1));
    }

    // Viewport indicator: compute normalised [0,1] visible image region
    float half_vw = view_w  * 0.5f;
    float half_vh = view_h  * 0.5f;
    float iw      = static_cast<float>(img.width);
    float ih      = static_cast<float>(img.height);
    float half_iw = iw * 0.5f;
    float half_ih = ih * 0.5f;

    // Inverse of shader: img_px = (screen_px - half_view) / zoom - pan + half_img
    float vis_x0 = ((0.0f   - half_vw) / vp.zoom - vp.pan_x + half_iw) / iw;
    float vis_y0 = ((0.0f   - half_vh) / vp.zoom - vp.pan_y + half_ih) / ih;
    float vis_x1 = ((view_w - half_vw) / vp.zoom - vp.pan_x + half_iw) / iw;
    float vis_y1 = ((view_h - half_vh) / vp.zoom - vp.pan_y + half_ih) / ih;

    float cx0 = std::max(0.0f, std::min(1.0f, vis_x0));
    float cy0 = std::max(0.0f, std::min(1.0f, vis_y0));
    float cx1 = std::max(0.0f, std::min(1.0f, vis_x1));
    float cy1 = std::max(0.0f, std::min(1.0f, vis_y1));

    // Map to minimap screen coordinates
    float vx0 = mx0 + cx0 * mini_w;
    float vy0 = my0 + cy0 * mini_h;
    float vx1 = mx0 + cx1 * mini_w;
    float vy1 = my0 + cy1 * mini_h;

    if (vx1 > vx0 + 1.0f && vy1 > vy0 + 1.0f) {
        if (state.pathfinder_mode == 2) {
            // Schematic: 반투명 노란색 채움
            dl->AddRectFilled(ImVec2(vx0, vy0), ImVec2(vx1, vy1),
                              IM_COL32(255, 220, 0, 80));
        }
        // 양쪽 모드 공통: 노란색 외곽선
        dl->AddRect(ImVec2(vx0, vy0), ImVec2(vx1, vy1),
                    IM_COL32(255, 220, 0, 220), 0.0f, 0, 1.5f);
    }

    // Minimap border
    dl->AddRect(ImVec2(mx0, my0), ImVec2(mx1, my1),
                IM_COL32(255, 255, 255, 100), 4.0f, 0, 1.0f);
}

// ─── render_single ────────────────────────────────────────────────────────────

void ImagePanel::render_single(AppState& state, int panel_idx) {
    int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual_idx];
    ViewportState&    vp  = state.views[panel_idx];

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int    pw    = std::max(1, static_cast<int>(avail.x));
    int    ph    = std::max(1, static_cast<int>(avail.y));

    if (!img.loaded || img.texture_id == 0) {
        ImGui::TextDisabled("(no image)");
        return;
    }

    if (vp.fit) {
        viewport_fit(vp, img.width, img.height, pw, ph);
    }

    // Clamp pan before shader uses it (ensure shader and border use same values)
    viewport_clamp_pan(vp, img.width, img.height, pw, ph);

    // Ensure FBO matches panel size
    if (!fbo_.ensure(pw, ph)) return;

    // Render image into FBO
    fbo_.bind();
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, img.texture_id);
    // Nearest neighbor for ≥1× zoom, linear+mipmap for zoom-out
    if (vp.zoom >= 1.0f) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    image_shader_.use();
    image_shader_.set_int  ("u_tex",        0);
    image_shader_.set_vec2 ("u_image_size", static_cast<float>(img.width),
                                            static_cast<float>(img.height));
    image_shader_.set_vec2 ("u_view_size",  static_cast<float>(pw),
                                            static_cast<float>(ph));
    image_shader_.set_float("u_zoom",       vp.zoom);
    image_shader_.set_vec2 ("u_pan",        vp.pan_x, vp.pan_y);
    image_shader_.set_int  ("u_channel",   static_cast<int>(state.channel_mode));

    quad_.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    fbo_.unbind();

    // Display FBO texture.
    // FBO bottom (GL t=0) = image top (shader maps screen_px.y=0 → img_px.y=0).
    // ImGui top → UV(0,0) → FBO bottom → image top.  No flip needed.
    ImTextureID tex = static_cast<ImTextureID>(fbo_.tex_id);
    ImGui::Image(tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    handle_mouse_pan(state, panel_idx);
    handle_mouse_right_select(state, panel_idx, widget_pos, pw, ph,
                               img.width, img.height);

    draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                      static_cast<float>(pw), static_cast<float>(ph),
                      static_cast<float>(img.width), static_cast<float>(img.height),
                      vp.pan_x, vp.pan_y, vp.zoom,
                      static_cast<ImU32>(state.border_colors[panel_idx]));

    if (vp.zoom >= 32.0f && img.loaded) {
        render_pixel_values(state, panel_idx, widget_pos, pw, ph);
    }

    if (panel_idx == 0) {
        render_pathfinder(state, panel_idx, widget_pos, pw, ph);
    }
}

// ─── render_pixel_values ─────────────────────────────────────────────────

void ImagePanel::render_pixel_values(const AppState& state, int panel_idx,
                                     ImVec2 widget_pos, int view_w, int view_h) {
    int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual_idx];
    const ViewportState& vp = state.views[panel_idx];

    if (!img.loaded) return;

    float zoom = vp.zoom;

    // Shader transform: img_px = (screen_px - view_size*0.5) / zoom - u_pan + image_size*0.5
    // Inverse:          screen_px = (img_px + u_pan - image_size*0.5) * zoom + view_size*0.5
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img.width * 0.5f;
    float half_ih = img.height * 0.5f;

    // screen_px = 0 -> img_px, screen_px = view_w -> img_px
    float img_x0 = (0.0f - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y0 = (0.0f - half_vh) / zoom - vp.pan_y + half_ih;
    float img_x1 = (view_w - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y1 = (view_h - half_vh) / zoom - vp.pan_y + half_ih;

    int px_start_x = std::max(0, (int)std::floor(img_x0));
    int px_start_y = std::max(0, (int)std::floor(img_y0));
    int px_end_x   = std::min(img.width,  (int)std::ceil(img_x1));
    int px_end_y   = std::min(img.height, (int)std::ceil(img_y1));

    if (px_start_x >= px_end_x || px_start_y >= px_end_y) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Pixel grid lines ──────────────────────────────────────────────────
    ImU32 grid_col = IM_COL32(255, 255, 255, 50);
    float clip_x0 = widget_pos.x;
    float clip_y0 = widget_pos.y;
    float clip_x1 = widget_pos.x + view_w;
    float clip_y1 = widget_pos.y + view_h;

    // Inverse transform: screen_px = (img_px + pan - half_iw) * zoom + half_vw
    for (int px = px_start_x; px <= px_end_x; ++px) {
        float sx = (px + vp.pan_x - half_iw) * zoom + half_vw + widget_pos.x;
        if (sx >= clip_x0 && sx <= clip_x1)
            dl->AddLine(ImVec2(sx, clip_y0), ImVec2(sx, clip_y1), grid_col);
    }
    for (int py = px_start_y; py <= px_end_y; ++py) {
        float sy = (py + vp.pan_y - half_ih) * zoom + half_vh + widget_pos.y;
        if (sy >= clip_y0 && sy <= clip_y1)
            dl->AddLine(ImVec2(clip_x0, sy), ImVec2(clip_x1, sy), grid_col);
    }

    // ── RGB value text ────────────────────────────────────────────────────
    float font_size = std::min(zoom * 0.25f, 20.0f);
    if (font_size < 6.0f) return;

    float scale = font_size / ImGui::GetFontSize();
    ImU32 col_r  = IM_COL32(255, 100, 100, 230);
    ImU32 col_g  = IM_COL32(100, 255, 100, 230);
    ImU32 col_b  = IM_COL32(100, 130, 255, 230);
    ImU32 shadow = IM_COL32(0, 0, 0, 140);

    for (int py = px_start_y; py < px_end_y; ++py) {
        for (int px = px_start_x; px < px_end_x; ++px) {
            // Pixel center (px+0.5, py+0.5) -> screen position
            float scr_x = (px + 0.5f + vp.pan_x - half_iw) * zoom + half_vw;
            float scr_y = (py + 0.5f + vp.pan_y - half_ih) * zoom + half_vh;
            float abs_x = widget_pos.x + scr_x;
            float abs_y = widget_pos.y + scr_y;

            int pidx = (py * img.width + px) * 4;
            char sr[16], sg[16], sb[16];

            if (img.is_hdr && !img.pixels_f32.empty()) {
                std::snprintf(sr, sizeof(sr), "%.2f", img.pixels_f32[pidx + 0]);
                std::snprintf(sg, sizeof(sg), "%.2f", img.pixels_f32[pidx + 1]);
                std::snprintf(sb, sizeof(sb), "%.2f", img.pixels_f32[pidx + 2]);
            } else if (!img.pixels.empty()) {
                std::snprintf(sr, sizeof(sr), "%d", img.pixels[pidx + 0]);
                std::snprintf(sg, sizeof(sg), "%d", img.pixels[pidx + 1]);
                std::snprintf(sb, sizeof(sb), "%d", img.pixels[pidx + 2]);
            } else {
                continue;
            }

            if (state.channel_mode == ChannelMode::RGB) {
                ImVec2 szr = ImGui::CalcTextSize(sr);
                ImVec2 szg = ImGui::CalcTextSize(sg);
                ImVec2 szb = ImGui::CalcTextSize(sb);
                szr.x *= scale; szr.y *= scale;
                szg.x *= scale; szg.y *= scale;
                szb.x *= scale; szb.y *= scale;

                float gap = 1.0f * scale;
                float total_h = szr.y + szg.y + szb.y + 2.0f * gap;
                float ry = abs_y - total_h * 0.5f;
                float gy = ry + szr.y + gap;
                float by = gy + szg.y + gap;

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f + 1, ry + 1), shadow, sr);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f, ry), col_r, sr);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f + 1, gy + 1), shadow, sg);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f, gy), col_g, sg);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f + 1, by + 1), shadow, sb);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f, by), col_b, sb);
            } else {
                const char* s_ch  = (state.channel_mode == ChannelMode::Red)   ? sr :
                                    (state.channel_mode == ChannelMode::Green)  ? sg : sb;
                ImU32       col_ch = (state.channel_mode == ChannelMode::Red)   ? col_r :
                                    (state.channel_mode == ChannelMode::Green)  ? col_g : col_b;
                ImVec2 sz = ImGui::CalcTextSize(s_ch);
                sz.x *= scale; sz.y *= scale;
                float ty = abs_y - sz.y * 0.5f;
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f + 1, ty + 1), shadow, s_ch);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f, ty), col_ch, s_ch);
            }
        }
    }
}

// ─── render_diff ──────────────────────────────────────────────────────────────

void ImagePanel::render_diff(AppState& state, DiffRenderer& diff_renderer) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;

    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState&    vp   = state.views[0];

    if (!imgA.loaded || !imgB.loaded) {
        ImGui::TextDisabled("(need two images for diff)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int    pw    = std::max(1, static_cast<int>(avail.x));
    int    ph    = std::max(1, static_cast<int>(avail.y));

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);

    // Clamp pan before shader uses it (ensure shader and border use same values)
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    if (!fbo_.ensure(pw, ph)) return;

    fbo_.bind();
    diff_renderer.render(imgA.texture_id, imgB.texture_id,
                         vp,
                         imgA.width, imgA.height,
                         pw, ph,
                         state.diff.mode, state.diff.amplify,
                         state.channel_mode);
    fbo_.unbind();

    ImTextureID tex = static_cast<ImTextureID>(fbo_.tex_id);
    ImGui::Image(tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    handle_mouse_pan(state, 0);
    handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                               imgA.width, imgA.height);

    draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                      static_cast<float>(pw), static_cast<float>(ph),
                      static_cast<float>(imgA.width), static_cast<float>(imgA.height),
                      vp.pan_x, vp.pan_y, vp.zoom,
                      static_cast<ImU32>(state.border_colors[2]));

    if (vp.zoom >= 32.0f && imgA.loaded && imgB.loaded) {
        render_diff_pixel_values(state, widget_pos, pw, ph);
    }
}

// ─── render_diff_pixel_values ─────────────────────────────────────────────

void ImagePanel::render_diff_pixel_values(const AppState& state,
                                          ImVec2 widget_pos, int view_w, int view_h) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    const ViewportState& vp = state.views[0];

    if (!imgA.loaded || !imgB.loaded) return;

    float zoom = vp.zoom;

    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f;
    float half_ih = imgA.height * 0.5f;

    float img_x0 = (0.0f - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y0 = (0.0f - half_vh) / zoom - vp.pan_y + half_ih;
    float img_x1 = (view_w - half_vw) / zoom - vp.pan_x + half_iw;
    float img_y1 = (view_h - half_vh) / zoom - vp.pan_y + half_ih;

    int px_start_x = std::max(0, (int)std::floor(img_x0));
    int px_start_y = std::max(0, (int)std::floor(img_y0));
    int px_end_x   = std::min(imgA.width,  (int)std::ceil(img_x1));
    int px_end_y   = std::min(imgA.height, (int)std::ceil(img_y1));

    if (px_start_x >= px_end_x || px_start_y >= px_end_y) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // ── Pixel grid lines ──────────────────────────────────────────────────
    ImU32 grid_col = IM_COL32(255, 255, 255, 50);
    float clip_x0 = widget_pos.x;
    float clip_y0 = widget_pos.y;
    float clip_x1 = widget_pos.x + view_w;
    float clip_y1 = widget_pos.y + view_h;

    for (int px = px_start_x; px <= px_end_x; ++px) {
        float sx = (px + vp.pan_x - half_iw) * zoom + half_vw + widget_pos.x;
        if (sx >= clip_x0 && sx <= clip_x1)
            dl->AddLine(ImVec2(sx, clip_y0), ImVec2(sx, clip_y1), grid_col);
    }
    for (int py = px_start_y; py <= px_end_y; ++py) {
        float sy = (py + vp.pan_y - half_ih) * zoom + half_vh + widget_pos.y;
        if (sy >= clip_y0 && sy <= clip_y1)
            dl->AddLine(ImVec2(clip_x0, sy), ImVec2(clip_x1, sy), grid_col);
    }

    // ── Diff value text ────────────────────────────────────────────────────
    float font_size = std::min(zoom * 0.25f, 20.0f);
    if (font_size < 6.0f) return;

    float scale = font_size / ImGui::GetFontSize();
    ImU32 col_r  = IM_COL32(255, 100, 100, 230);
    ImU32 col_g  = IM_COL32(100, 255, 100, 230);
    ImU32 col_b  = IM_COL32(100, 130, 255, 230);
    ImU32 shadow = IM_COL32(0, 0, 0, 140);

    bool is_hdr = imgA.is_hdr && imgB.is_hdr;
    bool has_f32 = !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
    bool has_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

    for (int py = px_start_y; py < px_end_y; ++py) {
        for (int px = px_start_x; px < px_end_x; ++px) {
            float scr_x = (px + 0.5f + vp.pan_x - half_iw) * zoom + half_vw;
            float scr_y = (py + 0.5f + vp.pan_y - half_ih) * zoom + half_vh;
            float abs_x = widget_pos.x + scr_x;
            float abs_y = widget_pos.y + scr_y;

            int pidx_a = (py * imgA.width + px) * 4;
            int pidx_b = (py * imgB.width + px) * 4;
            char sr[16], sg[16], sb[16];

            if (is_hdr && has_f32) {
                std::snprintf(sr, sizeof(sr), "%.2f",
                    std::fabsf(imgA.pixels_f32[pidx_a + 0] - imgB.pixels_f32[pidx_b + 0]));
                std::snprintf(sg, sizeof(sg), "%.2f",
                    std::fabsf(imgA.pixels_f32[pidx_a + 1] - imgB.pixels_f32[pidx_b + 1]));
                std::snprintf(sb, sizeof(sb), "%.2f",
                    std::fabsf(imgA.pixels_f32[pidx_a + 2] - imgB.pixels_f32[pidx_b + 2]));
            } else if (has_u8) {
                std::snprintf(sr, sizeof(sr), "%d",
                    std::abs((int)imgA.pixels[pidx_a + 0] - (int)imgB.pixels[pidx_b + 0]));
                std::snprintf(sg, sizeof(sg), "%d",
                    std::abs((int)imgA.pixels[pidx_a + 1] - (int)imgB.pixels[pidx_b + 1]));
                std::snprintf(sb, sizeof(sb), "%d",
                    std::abs((int)imgA.pixels[pidx_a + 2] - (int)imgB.pixels[pidx_b + 2]));
            } else {
                continue;
            }

            if (state.channel_mode == ChannelMode::RGB) {
                ImVec2 szr = ImGui::CalcTextSize(sr);
                ImVec2 szg = ImGui::CalcTextSize(sg);
                ImVec2 szb = ImGui::CalcTextSize(sb);
                szr.x *= scale; szr.y *= scale;
                szg.x *= scale; szg.y *= scale;
                szb.x *= scale; szb.y *= scale;

                float gap = 1.0f * scale;
                float total_h = szr.y + szg.y + szb.y + 2.0f * gap;
                float ry = abs_y - total_h * 0.5f;
                float gy = ry + szr.y + gap;
                float by = gy + szg.y + gap;

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f + 1, ry + 1), shadow, sr);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szr.x * 0.5f, ry), col_r, sr);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f + 1, gy + 1), shadow, sg);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szg.x * 0.5f, gy), col_g, sg);

                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f + 1, by + 1), shadow, sb);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - szb.x * 0.5f, by), col_b, sb);
            } else {
                const char* s_ch  = (state.channel_mode == ChannelMode::Red)   ? sr :
                                    (state.channel_mode == ChannelMode::Green)  ? sg : sb;
                ImU32       col_ch = (state.channel_mode == ChannelMode::Red)   ? col_r :
                                    (state.channel_mode == ChannelMode::Green)  ? col_g : col_b;
                ImVec2 sz = ImGui::CalcTextSize(s_ch);
                sz.x *= scale; sz.y *= scale;
                float ty = abs_y - sz.y * 0.5f;
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f + 1, ty + 1), shadow, s_ch);
                dl->AddText(ImGui::GetFont(), font_size,
                            ImVec2(abs_x - sz.x * 0.5f, ty), col_ch, s_ch);
            }
        }
    }
}

// ─── render ───────────────────────────────────────────────────────────────────

void ImagePanel::render(AppState& state, int panel_idx, DiffRenderer& diff_renderer,
                        bool force_single) {
    if (!inited_) return;

    if (!force_single) {
        // Diff modes take over panel 0 and show both images
        bool is_diff_mode = (state.diff.mode != DiffState::Mode::None &&
                             state.diff.mode != DiffState::Mode::SSIM);

        if (is_diff_mode && panel_idx == 0) {
            render_diff(state, diff_renderer);
            return;
        }

        // SSIM heatmap: display precomputed texture in panel 0
        if (state.diff.mode == DiffState::Mode::SSIM &&
            panel_idx == 0 &&
            state.diff.ssim_texture_id != 0) {
            ImageEntry fake{};
            fake.loaded     = true;
            fake.texture_id = state.diff.ssim_texture_id;
            fake.width      = state.images[0].width;
            fake.height     = state.images[0].height;
            auto saved = state.images[0];
            state.images[0] = fake;
            render_single(state, 0);
            state.images[0] = saved;
            return;
        }
    }

    render_single(state, panel_idx);
}
