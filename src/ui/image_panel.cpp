#include "image_panel.h"
#include "../shader_sources.h"
#include "../viewport.h"
#include "../render_backend.h"
#include "../image_save.h"

#include <imgui.h>
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <memory>

// ─── init ─────────────────────────────────────────────────────────────────────

bool ImagePanel::init() {
    if (is_software_mode()) {
        // No shaders needed in software mode
        inited_ = true;
        return true;
    }

    if (!image_shader_.compile(shaders::VERTEX_SRC, shaders::IMAGE_FRAG_SRC)) {
        std::cerr << "[ImagePanel] image shader compile failed\n";
        return false;
    }
    // Bind attribute locations for #version 150
    glBindAttribLocation(image_shader_.id, 0, "a_pos");
    glBindAttribLocation(image_shader_.id, 1, "a_uv");
    glLinkProgram(image_shader_.id);

    if (!blend_shader_.compile(shaders::VERTEX_SRC, shaders::BLEND_FRAG_SRC)) {
        std::cerr << "[ImagePanel] blend shader compile failed\n";
        return false;
    }
    glBindAttribLocation(blend_shader_.id, 0, "a_pos");
    glBindAttribLocation(blend_shader_.id, 1, "a_uv");
    glLinkProgram(blend_shader_.id);

    quad_.init();
    inited_ = true;
    return true;
}

// Forward declaration
static void draw_image_border(ImDrawList* dl, ImVec2 widget_pos,
                               float view_w, float view_h,
                               float img_w,  float img_h,
                               float pan_x,  float pan_y, float zoom,
                               ImU32 border_col);

// ─── Software renderer helpers ─────────────────────────────────────────────────

SDL_Texture* ImagePanel::ensure_soft_texture(int w, int h) {
    if (soft_texture_ && soft_w_ == w && soft_h_ == h) return soft_texture_;
    if (soft_texture_) SDL_DestroyTexture(soft_texture_);

    soft_texture_ = SDL_CreateTexture(g_render_ctx.sdl_renderer,
                                       SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       w, h);
    soft_w_ = w;
    soft_h_ = h;
    soft_buf_.resize(static_cast<size_t>(w) * h * 4);
    return soft_texture_;
}

// ─── falsecolor: shared by diff + SSIM ──────────────────────────────────────
static void falsecolor(float t, uint8_t* rgb) {
    t = std::clamp(t, 0.0f, 1.0f);
    float r, g, b;
    if      (t < 0.25f) { float s = t * 4.0f;        r = 0;            g = 0;            b = 0.5f + 0.5f * s; }
    else if (t < 0.50f) { float s = (t - 0.25f) * 4; r = 0;            g = s;            b = 1.0f - s; }
    else if (t < 0.75f) { float s = (t - 0.50f) * 4; r = s;            g = 1.0f;         b = 0; }
    else                { float s = (t - 0.75f) * 4; r = 1.0f;         g = 1.0f - s;     b = 0; }
    rgb[0] = static_cast<uint8_t>(r * 255.0f);
    rgb[1] = static_cast<uint8_t>(g * 255.0f);
    rgb[2] = static_cast<uint8_t>(b * 255.0f);
}

// ─── apply channel filter + pixel grid to a rendered pixel ───────────────────
static inline void apply_channel_grid(uint8_t* dst, float img_fx, float img_fy,
                                       float zoom, ChannelMode channel) {
    // Channel filter (same as shader u_channel logic)
    if (channel == ChannelMode::Red)   { dst[1] = dst[0]; dst[2] = dst[0]; }
    if (channel == ChannelMode::Green) { dst[0] = dst[1]; dst[2] = dst[1]; }
    if (channel == ChannelMode::Blue)  { dst[0] = dst[2]; dst[1] = dst[2]; }

    // Pixel grid at high zoom (>= 16x) — matches shader smoothstep logic
    if (zoom >= 16.0f) {
        float frac_x = img_fx - std::floor(img_fx);
        float frac_y = img_fy - std::floor(img_fy);
        float gx = std::min(frac_x, 1.0f - frac_x);
        float gy = std::min(frac_y, 1.0f - frac_y);
        float grid_dist = std::min(gx, gy);
        float line_w = 1.0f / zoom;
        // smoothstep(line_w, 0, grid_dist)
        float grid = (grid_dist >= line_w) ? 0.0f :
                     (grid_dist <= 0.0f)   ? 1.0f :
                     1.0f - grid_dist / line_w;  // linear approx of smoothstep
        grid = grid * grid * (3.0f - 2.0f * grid); // actual smoothstep
        float mix_a = grid * 0.4f;
        for (int c = 0; c < 3; ++c) {
            float v = dst[c] / 255.0f;
            v = v * (1.0f - mix_a) + 0.5f * mix_a;
            dst[c] = static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
        }
    }
}

void ImagePanel::cpu_render_image(const ImageEntry& img, const ViewportState& vp,
                                   uint8_t* buf, int view_w, int view_h,
                                   ChannelMode channel) {
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img.width * 0.5f;
    float half_ih = img.height * 0.5f;

    const uint8_t* src = img.pixels.data();
    int iw = img.width, ih = img.height;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;

            uint8_t* dst = buf + (sy * view_w + sx) * 4;

            if (img_fx < 0.0f || img_fy < 0.0f ||
                img_fx >= iw || img_fy >= ih) {
                dst[0] = 38; dst[1] = 38; dst[2] = 38; dst[3] = 255;
                continue;
            }

            if (vp.zoom >= 1.0f) {
                int ix = static_cast<int>(img_fx);
                int iy = static_cast<int>(img_fy);
                ix = std::clamp(ix, 0, iw - 1);
                iy = std::clamp(iy, 0, ih - 1);
                const uint8_t* p = src + (iy * iw + ix) * 4;
                dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2]; dst[3] = p[3];
            } else {
                float fx = img_fx - 0.5f;
                float fy = img_fy - 0.5f;
                int x0 = static_cast<int>(std::floor(fx));
                int y0 = static_cast<int>(std::floor(fy));
                int x1 = x0 + 1, y1 = y0 + 1;
                float tx = fx - x0, ty = fy - y0;
                x0 = std::clamp(x0, 0, iw - 1); x1 = std::clamp(x1, 0, iw - 1);
                y0 = std::clamp(y0, 0, ih - 1); y1 = std::clamp(y1, 0, ih - 1);
                const uint8_t* p00 = src + (y0 * iw + x0) * 4;
                const uint8_t* p10 = src + (y0 * iw + x1) * 4;
                const uint8_t* p01 = src + (y1 * iw + x0) * 4;
                const uint8_t* p11 = src + (y1 * iw + x1) * 4;
                for (int c = 0; c < 4; ++c) {
                    float top = p00[c] * (1 - tx) + p10[c] * tx;
                    float bot = p01[c] * (1 - tx) + p11[c] * tx;
                    dst[c] = static_cast<uint8_t>(std::clamp(top * (1 - ty) + bot * ty, 0.0f, 255.0f));
                }
            }

            apply_channel_grid(dst, img_fx, img_fy, vp.zoom, channel);
        }
    }
}

void ImagePanel::render_single_software(AppState& state, int panel_idx) {
    int actual_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
    const ImageEntry& img = state.images[actual_idx];
    ViewportState&    vp  = state.views[panel_idx];

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int    pw    = std::max(1, static_cast<int>(avail.x));
    int    ph    = std::max(1, static_cast<int>(avail.y));

    if (!img.loaded || img.texture_id == 0 || img.pixels.empty()) {
        ImGui::TextDisabled("(no image)");
        return;
    }

    if (vp.fit) {
        viewport_fit(vp, img.width, img.height, pw, ph);
    }
    viewport_clamp_pan(vp, img.width, img.height, pw, ph);

    // CPU render image into buffer
    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    cpu_render_image(img, vp, soft_buf_.data(), pw, ph, state.channel_mode);

    // Upload to SDL texture
    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    // Display via ImGui
    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, panel_idx, widget_pos, pw, ph,
                        img.width, img.height);
    } else {
        handle_mouse_pan(state, panel_idx);
        handle_mouse_right_select(state, panel_idx, widget_pos, pw, ph,
                                   img.width, img.height, panel_idx);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          static_cast<float>(pw), static_cast<float>(ph),
                          static_cast<float>(img.width), static_cast<float>(img.height),
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[panel_idx]));

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, panel_idx, widget_pos, pw, ph,
                           img.width, img.height);
    }

    if (vp.zoom >= 32.0f && img.loaded) {
        render_pixel_values(state, panel_idx, widget_pos, pw, ph);
    }

    render_crosshair(state, panel_idx, widget_pos, pw, ph, img.width, img.height);

    if (panel_idx == 0) {
        render_pathfinder(state, panel_idx, widget_pos, pw, ph);
    }
}

// ─── cpu_render_diff ──────────────────────────────────────────────────────────
// CPU version of DIFF_FRAG_SRC shader logic.

static inline void sample_pixel(const ImageEntry& img, float img_fx, float img_fy,
                                 float zoom, float out[4]) {
    int iw = img.width, ih = img.height;
    if (img_fx < 0 || img_fy < 0 || img_fx >= iw || img_fy >= ih) {
        out[0] = out[1] = out[2] = 0; out[3] = 1; return;
    }
    const uint8_t* src = img.pixels.data();
    if (zoom >= 1.0f) {
        int ix = std::clamp((int)img_fx, 0, iw - 1);
        int iy = std::clamp((int)img_fy, 0, ih - 1);
        const uint8_t* p = src + (iy * iw + ix) * 4;
        for (int c = 0; c < 4; ++c) out[c] = p[c] / 255.0f;
    } else {
        float fx = img_fx - 0.5f, fy = img_fy - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        int x1 = x0 + 1, y1 = y0 + 1;
        float tx = fx - x0, ty = fy - y0;
        x0 = std::clamp(x0, 0, iw-1); x1 = std::clamp(x1, 0, iw-1);
        y0 = std::clamp(y0, 0, ih-1); y1 = std::clamp(y1, 0, ih-1);
        const uint8_t* p00 = src + (y0*iw+x0)*4;
        const uint8_t* p10 = src + (y0*iw+x1)*4;
        const uint8_t* p01 = src + (y1*iw+x0)*4;
        const uint8_t* p11 = src + (y1*iw+x1)*4;
        for (int c = 0; c < 4; ++c) {
            float top = p00[c]/255.0f * (1-tx) + p10[c]/255.0f * tx;
            float bot = p01[c]/255.0f * (1-tx) + p11[c]/255.0f * tx;
            out[c] = top * (1-ty) + bot * ty;
        }
    }
}

void ImagePanel::cpu_render_diff(const ImageEntry& imgA, const ImageEntry& imgB,
                                  const ViewportState& vp, uint8_t* buf,
                                  int view_w, int view_h,
                                  DiffState::Mode mode, float amplify,
                                  ChannelMode channel) {
    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f, half_ih = imgA.height * 0.5f;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;
            uint8_t* dst = buf + (sy * view_w + sx) * 4;

            if (img_fx < 0 || img_fy < 0 || img_fx >= imgA.width || img_fy >= imgA.height) {
                dst[0] = 26; dst[1] = 26; dst[2] = 26; dst[3] = 255;
                continue;
            }

            float a[4], b_val[4];
            sample_pixel(imgA, img_fx, img_fy, vp.zoom, a);
            sample_pixel(imgB, img_fx, img_fy, vp.zoom, b_val);

            float diff[3] = { std::fabs(a[0]-b_val[0]), std::fabs(a[1]-b_val[1]), std::fabs(a[2]-b_val[2]) };

            if (mode == DiffState::Mode::PixelRelative) {
                constexpr float eps = 0.001f;
                for (int c = 0; c < 3; ++c)
                    diff[c] = diff[c] / std::max(a[c] + eps, eps);
            }

            float r, g, b_out;
            if (mode == DiffState::Mode::FalseColor) {
                float intensity;
                if      (channel == ChannelMode::Red)   intensity = diff[0];
                else if (channel == ChannelMode::Green) intensity = diff[1];
                else if (channel == ChannelMode::Blue)  intensity = diff[2];
                else    intensity = (diff[0] + diff[1] + diff[2]) / 3.0f;
                uint8_t fc[3];
                falsecolor(intensity * amplify, fc);
                dst[0] = fc[0]; dst[1] = fc[1]; dst[2] = fc[2]; dst[3] = 255;
            } else {
                float d[3] = { diff[0]*amplify, diff[1]*amplify, diff[2]*amplify };
                if      (channel == ChannelMode::Red)   { r = g = b_out = std::clamp(d[0], 0.0f, 1.0f); }
                else if (channel == ChannelMode::Green) { r = g = b_out = std::clamp(d[1], 0.0f, 1.0f); }
                else if (channel == ChannelMode::Blue)  { r = g = b_out = std::clamp(d[2], 0.0f, 1.0f); }
                else { r = std::clamp(d[0], 0.0f, 1.0f); g = std::clamp(d[1], 0.0f, 1.0f); b_out = std::clamp(d[2], 0.0f, 1.0f); }
                dst[0] = (uint8_t)(r*255); dst[1] = (uint8_t)(g*255); dst[2] = (uint8_t)(b_out*255); dst[3] = 255;
            }

            // Grid overlay (no channel filter needed — already applied in diff logic)
            if (vp.zoom >= 16.0f) {
                float frac_x = img_fx - std::floor(img_fx);
                float frac_y = img_fy - std::floor(img_fy);
                float gx = std::min(frac_x, 1.0f - frac_x);
                float gy = std::min(frac_y, 1.0f - frac_y);
                float grid_dist = std::min(gx, gy);
                float line_w = 1.0f / vp.zoom;
                float grid = (grid_dist >= line_w) ? 0.0f :
                             (grid_dist <= 0.0f)   ? 1.0f :
                             1.0f - grid_dist / line_w;
                grid = grid * grid * (3.0f - 2.0f * grid);
                float mix_a = grid * 0.4f;
                for (int c = 0; c < 3; ++c) {
                    float v = dst[c] / 255.0f;
                    v = v * (1.0f - mix_a) + 0.5f * mix_a;
                    dst[c] = (uint8_t)std::clamp(v * 255.0f, 0.0f, 255.0f);
                }
            }
        }
    }
}

// ─── apply_threshold_to_diff ─────────────────────────────────────────────────
// Post-process diff buffer: pixels with max channel diff <= threshold become dark gray.
// Returns (exceed_count, total_count).

static std::pair<int,int> apply_threshold_to_diff(
    const ImageEntry& imgA, const ImageEntry& imgB,
    uint8_t* buf, int view_w, int view_h,
    const ViewportState& vp, int threshold)
{
    if (threshold <= 0) return {0, 0};

    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f, half_ih = imgA.height * 0.5f;
    float th_norm = threshold / 255.0f;
    int exceed = 0, total = 0;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;

            if (img_fx < 0 || img_fy < 0 || img_fx >= imgA.width || img_fy >= imgA.height)
                continue;

            // Get original pixel diff (before amplification)
            float a[4], b_val[4];
            sample_pixel(imgA, img_fx, img_fy, vp.zoom, a);
            sample_pixel(imgB, img_fx, img_fy, vp.zoom, b_val);
            float max_diff = 0.0f;
            for (int c = 0; c < 3; ++c) {
                float d = std::fabs(a[c] - b_val[c]);
                if (d > max_diff) max_diff = d;
            }

            ++total;
            if (max_diff > th_norm) {
                ++exceed;
            } else {
                // Below threshold: dark gray
                uint8_t* dst = buf + (sy * view_w + sx) * 4;
                dst[0] = 40; dst[1] = 40; dst[2] = 40; dst[3] = 255;
            }
        }
    }
    return {exceed, total};
}

// ─── cpu_render_overlay ─────────────────────────────────────────────────────

void ImagePanel::cpu_render_overlay(const ImageEntry& imgA, const ImageEntry& imgB,
                                     const ViewportState& vp, uint8_t* buf,
                                     int view_w, int view_h,
                                     const OverlayState& overlay, ChannelMode channel) {
    float half_vw = view_w * 0.5f, half_vh = view_h * 0.5f;
    float half_iw = imgA.width * 0.5f, half_ih = imgA.height * 0.5f;

    for (int sy = 0; sy < view_h; ++sy) {
        for (int sx = 0; sx < view_w; ++sx) {
            float img_fx = (sx - half_vw) / vp.zoom - vp.pan_x + half_iw;
            float img_fy = (sy - half_vh) / vp.zoom - vp.pan_y + half_ih;
            uint8_t* dst = buf + (sy * view_w + sx) * 4;

            if (img_fx < 0 || img_fy < 0 || img_fx >= imgA.width || img_fy >= imgA.height) {
                dst[0] = 38; dst[1] = 38; dst[2] = 38; dst[3] = 255;
                continue;
            }

            float a[4], b_val[4];
            sample_pixel(imgA, img_fx, img_fy, vp.zoom, a);
            sample_pixel(imgB, img_fx, img_fy, vp.zoom, b_val);

            float result[4];
            if (overlay.mode == OverlayState::Mode::Curtain) {
                float uv_x = static_cast<float>(sx) / view_w;
                const float* pick = (uv_x < overlay.curtain_x) ? a : b_val;
                for (int c = 0; c < 4; ++c) result[c] = pick[c];
            } else {
                float alpha = overlay.alpha;
                for (int c = 0; c < 4; ++c)
                    result[c] = a[c] * (1.0f - alpha) + b_val[c] * alpha;
            }

            for (int c = 0; c < 4; ++c)
                dst[c] = (uint8_t)std::clamp(result[c] * 255.0f, 0.0f, 255.0f);

            apply_channel_grid(dst, img_fx, img_fy, vp.zoom, channel);
        }
    }
}

// ─── render_diff_software ─────────────────────────────────────────────────────

void ImagePanel::render_diff_software(AppState& state) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState& vp = state.views[0];

    if (!imgA.loaded || !imgB.loaded || imgA.pixels.empty() || imgB.pixels.empty()) {
        ImGui::TextDisabled("(need two images for diff)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    cpu_render_diff(imgA, imgB, vp, soft_buf_.data(), pw, ph,
                    state.diff.mode, state.diff.amplify, state.channel_mode);

    // Apply tolerance threshold post-processing
    if (state.diff.threshold > 0) {
        auto [exceed, total] = apply_threshold_to_diff(
            imgA, imgB, soft_buf_.data(), pw, ph, vp, state.diff.threshold);
        state.diff.threshold_exceed_count = exceed;
        state.diff.threshold_total_count  = total;
    }

    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    } else {
        handle_mouse_pan(state, 0);
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height, 2);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[2]));

    if (state.roi.has_roi || state.roi.dragging)
        render_roi_overlay(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);

    if (vp.zoom >= 32.0f && imgA.loaded && imgB.loaded)
        render_diff_pixel_values(state, widget_pos, pw, ph);

    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
}

// ─── render_overlay_software ──────────────────────────────────────────────────

void ImagePanel::render_overlay_software(AppState& state) {
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState& vp = state.views[0];

    if (!imgA.loaded || !imgB.loaded || imgA.pixels.empty() || imgB.pixels.empty()) {
        ImGui::TextDisabled("(overlay: need two images)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    cpu_render_overlay(imgA, imgB, vp, soft_buf_.data(), pw, ph,
                       state.overlay, state.channel_mode);

    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();

    // Curtain mode: draw divider line + drag handling
    if (state.overlay.mode == OverlayState::Mode::Curtain) {
        float curtain_sx = widget_pos.x + state.overlay.curtain_x * pw;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(curtain_sx, widget_pos.y),
                    ImVec2(curtain_sx, widget_pos.y + ph),
                    IM_COL32(255, 255, 255, 200), 2.0f);
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "A | B");
        dl->AddText(ImVec2(curtain_sx + 4, widget_pos.y + 4),
                    IM_COL32(255, 255, 255, 200), lbl);

        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float mx = ImGui::GetMousePos().x;
            state.overlay.curtain_x = std::clamp((mx - widget_pos.x) / pw, 0.0f, 1.0f);
        }
    } else {
        handle_mouse_pan(state, 0);
    }

    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    } else if (state.overlay.mode != OverlayState::Mode::Curtain) {
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          IM_COL32(150, 255, 150, 200));

    if (state.roi.has_roi || state.roi.dragging)
        render_roi_overlay(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);

    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    render_pathfinder(state, 0, widget_pos, pw, ph);
}

// ─── render_ssim_software ─────────────────────────────────────────────────────

void ImagePanel::render_ssim_software(AppState& state) {
    const ImageEntry& imgA = state.images[0];
    ViewportState& vp = state.views[0];

    if (!imgA.loaded || state.diff.ssim_texture_id == 0) {
        ImGui::TextDisabled("(SSIM heatmap not ready)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    SDL_Texture* tex = ensure_soft_texture(pw, ph);
    if (!tex) return;

    // Use stored falsecolor RGBA pixels with cpu_render_image for viewport transform
    if (!state.diff.ssim_pixels.empty() && state.diff.ssim_w > 0) {
        ImageEntry fake{};
        fake.loaded  = true;
        fake.width   = state.diff.ssim_w;
        fake.height  = state.diff.ssim_h;
        fake.pixels.swap(state.diff.ssim_pixels);  // zero-copy swap
        cpu_render_image(fake, vp, soft_buf_.data(), pw, ph);
        fake.pixels.swap(state.diff.ssim_pixels);  // swap back
    } else {
        // Fallback: clear to dark
        std::memset(soft_buf_.data(), 26, soft_buf_.size());
    }

    SDL_UpdateTexture(tex, nullptr, soft_buf_.data(), pw * 4);

    ImTextureID imgui_tex = reinterpret_cast<ImTextureID>(tex);
    ImGui::Image(imgui_tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
    } else {
        handle_mouse_pan(state, 0);
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height, 2);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[2]));

    if (state.roi.has_roi || state.roi.dragging)
        render_roi_overlay(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);

    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
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

// ─── Right-click context menu ─────────────────────────────────────────────────

static void stbi_mem_write_func(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

static std::shared_ptr<std::vector<uint8_t>> s_clipboard_png;

static const void* SDLCALL clipboard_data_callback(void* /*userdata*/,
                                                     const char* mime_type,
                                                     size_t* size) {
    if (s_clipboard_png && std::strcmp(mime_type, "image/png") == 0) {
        *size = s_clipboard_png->size();
        return s_clipboard_png->data();
    }
    *size = 0;
    return nullptr;
}

static void SDLCALL clipboard_cleanup_callback(void* /*userdata*/) {
    s_clipboard_png.reset();
}

void ImagePanel::render_context_popup(AppState& state, int /*context_type*/) {
    if (!ImGui::BeginPopup("##PanelContextMenu")) return;

    if (ImGui::MenuItem("Save As...")) {
        open_context_save_dialog(state, context_panel_type_);
    }
    if (ImGui::MenuItem("Copy to Clipboard")) {
        copy_panel_to_clipboard(state, context_panel_type_);
    }
    ImGui::EndPopup();
}

void ImagePanel::copy_panel_to_clipboard(AppState& state, int target_type) {
    const uint8_t* rgba_data = nullptr;
    std::vector<uint8_t> rgba_buf;
    int w = 0, h = 0;

    if (target_type == 0 || target_type == 1) {
        int idx = state.swap_images ? (1 - target_type) : target_type;
        const ImageEntry& img = state.images[idx];
        if (!img.loaded) return;
        w = img.width;
        h = img.height;

        if (!img.pixels.empty()) {
            rgba_data = img.pixels.data();
        } else if (img.is_hdr && !img.pixels_f32.empty()) {
            rgba_buf.resize(static_cast<size_t>(w) * h * 4);
            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                for (int c = 0; c < 4; ++c) {
                    float v = std::clamp(img.pixels_f32[i * 4 + c], 0.0f, 1.0f);
                    rgba_buf[i * 4 + c] = static_cast<uint8_t>(v * 255.0f + 0.5f);
                }
            }
            rgba_data = rgba_buf.data();
        } else {
            return;
        }
    } else if (target_type == 2) {
        int idx_a = state.swap_images ? 1 : 0;
        int idx_b = state.swap_images ? 0 : 1;
        const auto& imgA = state.images[idx_a];
        const auto& imgB = state.images[idx_b];
        if (!imgA.loaded || !imgB.loaded) return;
        rgba_buf = compute_diff_cpu(imgA, imgB, state.diff);
        w = std::min(imgA.width, imgB.width);
        h = std::min(imgA.height, imgB.height);
        rgba_data = rgba_buf.data();
    } else {
        return;
    }

    // Encode to PNG in memory
    auto png = std::make_shared<std::vector<uint8_t>>();
    stbi_write_png_to_func(stbi_mem_write_func, png.get(), w, h, 4,
                           rgba_data, w * 4);
    if (png->empty()) return;

    s_clipboard_png = png;
    const char* mime_types[] = {"image/png"};
    SDL_SetClipboardData(clipboard_data_callback, clipboard_cleanup_callback,
                         nullptr, mime_types, 1);
}

// ─── Right-click drag-to-zoom ─────────────────────────────────────────────────
// Allows the user to drag-select a region with the right mouse button.
// On release the view snaps to the largest 2^n zoom that fits the selection.

void ImagePanel::handle_mouse_right_select(AppState& state, int panel_idx,
                                            ImVec2 widget_pos,
                                            int view_w, int view_h,
                                            int img_w, int img_h,
                                            int context_type) {
    // ★ Always render popup (before any early return)
    if (context_type >= 0)
        render_context_popup(state, context_type);

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

    // Tiny drag → context menu instead of zoom
    if ((rx1 - rx0) < 5.0f || (ry1 - ry0) < 5.0f) {
        if (context_type >= 0) {
            context_panel_type_ = context_type;
            ImGui::OpenPopup("##PanelContextMenu");
        }
        return;
    }

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

// ─── render_crosshair ─────────────────────────────────────────────────────────

void ImagePanel::render_crosshair(const AppState& state, int panel_idx,
                                   ImVec2 widget_pos, int view_w, int view_h,
                                   int img_w, int img_h) {
    if (!state.show_crosshair) return;

    ImVec2 mouse = ImGui::GetMousePos();
    // Only draw if mouse is within this panel
    if (mouse.x < widget_pos.x || mouse.x > widget_pos.x + view_w ||
        mouse.y < widget_pos.y || mouse.y > widget_pos.y + view_h)
        return;

    const ViewportState& vp = state.views[panel_idx];
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w * 0.5f;
    float half_ih = img_h * 0.5f;

    // Screen → image coordinate
    float screen_x = mouse.x - widget_pos.x;
    float screen_y = mouse.y - widget_pos.y;
    float img_fx = (screen_x - half_vw) / vp.zoom - vp.pan_x + half_iw;
    float img_fy = (screen_y - half_vh) / vp.zoom - vp.pan_y + half_ih;

    int img_x = static_cast<int>(std::floor(img_fx));
    int img_y = static_cast<int>(std::floor(img_fy));

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Crosshair lines (full viewport span)
    ImU32 line_col = IM_COL32(255, 255, 0, 160);
    dl->AddLine(ImVec2(widget_pos.x, mouse.y),
                ImVec2(widget_pos.x + view_w, mouse.y), line_col, 1.0f);
    dl->AddLine(ImVec2(mouse.x, widget_pos.y),
                ImVec2(mouse.x, widget_pos.y + view_h), line_col, 1.0f);

    // Coordinate label near cursor
    if (img_x >= 0 && img_x < img_w && img_y >= 0 && img_y < img_h) {
        char coord_buf[32];
        std::snprintf(coord_buf, sizeof(coord_buf), "(%d, %d)", img_x, img_y);
        ImVec2 text_size = ImGui::CalcTextSize(coord_buf);
        float tx = mouse.x + 12.0f;
        float ty = mouse.y - text_size.y - 6.0f;
        // Keep label inside viewport
        if (tx + text_size.x + 4 > widget_pos.x + view_w)
            tx = mouse.x - text_size.x - 16.0f;
        if (ty < widget_pos.y)
            ty = mouse.y + 6.0f;
        dl->AddRectFilled(ImVec2(tx - 3, ty - 2),
                          ImVec2(tx + text_size.x + 3, ty + text_size.y + 2),
                          IM_COL32(0, 0, 0, 200), 3.0f);
        dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 0, 240), coord_buf);
    }

    // If sync viewports, draw crosshair on OTHER panel too (using same image coords)
    if (state.sync_viewports && state.images[0].loaded && state.images[1].loaded) {
        // The other panel's crosshair will be drawn when render_crosshair is called
        // for the other panel, but we need to show the crosshair at the same IMAGE
        // position (not screen position). This is handled by checking whether the
        // mouse is hovering a different panel and converting via image coords.
        // For simplicity, we rely on the foreground draw list being shared — the
        // calling code handles both panels.
    }
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
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(img.texture_id));
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
    if (state.roi.active) {
        handle_roi_drag(state, panel_idx, widget_pos, pw, ph,
                        img.width, img.height);
    } else {
        handle_mouse_pan(state, panel_idx);
        handle_mouse_right_select(state, panel_idx, widget_pos, pw, ph,
                                   img.width, img.height, panel_idx);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          static_cast<float>(pw), static_cast<float>(ph),
                          static_cast<float>(img.width), static_cast<float>(img.height),
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[panel_idx]));

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, panel_idx, widget_pos, pw, ph,
                           img.width, img.height);
    }

    if (vp.zoom >= 32.0f && img.loaded) {
        render_pixel_values(state, panel_idx, widget_pos, pw, ph);
    }

    render_crosshair(state, panel_idx, widget_pos, pw, ph, img.width, img.height);

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

            char sr[16], sg[16], sb[16];

            if (img.ppm_maxval > 0 && !img.pixels_orig.empty()) {
                int oidx = (py * img.width + px) * 3;
                std::snprintf(sr, sizeof(sr), "%d", (int)img.pixels_orig[oidx + 0]);
                std::snprintf(sg, sizeof(sg), "%d", (int)img.pixels_orig[oidx + 1]);
                std::snprintf(sb, sizeof(sb), "%d", (int)img.pixels_orig[oidx + 2]);
            } else if (img.is_hdr && !img.pixels_f32.empty()) {
                int pidx = (py * img.width + px) * 4;
                std::snprintf(sr, sizeof(sr), "%.2f", img.pixels_f32[pidx + 0]);
                std::snprintf(sg, sizeof(sg), "%.2f", img.pixels_f32[pidx + 1]);
                std::snprintf(sb, sizeof(sb), "%.2f", img.pixels_f32[pidx + 2]);
            } else if (!img.pixels.empty()) {
                int pidx = (py * img.width + px) * 4;
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
                         state.channel_mode,
                         state.diff.threshold);
    fbo_.unbind();

    ImTextureID tex = static_cast<ImTextureID>(fbo_.tex_id);
    ImGui::Image(tex, avail, ImVec2(0, 0), ImVec2(1, 1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();
    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph,
                        imgA.width, imgA.height);
    } else {
        handle_mouse_pan(state, 0);
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height, 2);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          static_cast<float>(pw), static_cast<float>(ph),
                          static_cast<float>(imgA.width), static_cast<float>(imgA.height),
                          vp.pan_x, vp.pan_y, vp.zoom,
                          static_cast<ImU32>(state.border_colors[2]));

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, 0, widget_pos, pw, ph,
                           imgA.width, imgA.height);
    }

    if (vp.zoom >= 32.0f && imgA.loaded && imgB.loaded) {
        render_diff_pixel_values(state, widget_pos, pw, ph);
    }

    render_crosshair(state, 0, widget_pos, pw, ph, imgA.width, imgA.height);
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
                    std::fabs(imgA.pixels_f32[pidx_a + 0] - imgB.pixels_f32[pidx_b + 0]));
                std::snprintf(sg, sizeof(sg), "%.2f",
                    std::fabs(imgA.pixels_f32[pidx_a + 1] - imgB.pixels_f32[pidx_b + 1]));
                std::snprintf(sb, sizeof(sb), "%.2f",
                    std::fabs(imgA.pixels_f32[pidx_a + 2] - imgB.pixels_f32[pidx_b + 2]));
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

    // Software mode: full feature rendering via CPU
    if (is_software_mode()) {
        if (!force_single) {
            bool both_loaded = state.images[0].loaded && state.images[1].loaded;
            // Overlay mode
            if (state.overlay.active && panel_idx == 0 && both_loaded) {
                render_overlay_software(state);
                return;
            }
            // Diff modes (abs/rel/falsecolor)
            bool is_diff_mode = (state.diff.mode != DiffState::Mode::None &&
                                 state.diff.mode != DiffState::Mode::SSIM);
            if (is_diff_mode && panel_idx == 0) {
                render_diff_software(state);
                return;
            }
            // SSIM heatmap
            if (state.diff.mode == DiffState::Mode::SSIM &&
                panel_idx == 0 && state.diff.ssim_texture_id != 0) {
                render_ssim_software(state);
                return;
            }
        }
        render_single_software(state, panel_idx);
        return;
    }

    if (!force_single) {
        // Overlay 모드: panel_idx==0에서만 blend/curtain 렌더링
        if (state.overlay.active && panel_idx == 0 &&
            state.images[0].loaded && state.images[1].loaded) {
            render_overlay(state, diff_renderer);
            return;
        }

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

// ─── ROI drag selection ────────────────────────────────────────────────────────

void ImagePanel::handle_roi_drag(AppState& state, int panel_idx,
                                  ImVec2 widget_pos, int view_w, int view_h,
                                  int img_w, int img_h)
{
    auto& roi = state.roi;

    // Scroll wheel still zooms in ROI mode
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

    // 드래그 시작
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        roi.dragging   = true;
        roi.drag_panel = panel_idx;
        ImVec2 mp = ImGui::GetMousePos();
        roi.drag_sx = mp.x;
        roi.drag_sy = mp.y;
    }

    if (!roi.dragging || roi.drag_panel != panel_idx) return;

    ImVec2 curr = ImGui::GetMousePos();
    float rx0 = std::min(roi.drag_sx, curr.x);
    float ry0 = std::min(roi.drag_sy, curr.y);
    float rx1 = std::max(roi.drag_sx, curr.x);
    float ry1 = std::max(roi.drag_sy, curr.y);

    // 드래그 중 사각형 표시
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                      IM_COL32(100, 200, 255, 30));
    dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                IM_COL32(100, 200, 255, 220), 0.0f, 0, 1.5f);

    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;

    roi.dragging = false;

    // 너무 작은 드래그 무시
    if ((rx1 - rx0) < 3.0f || (ry1 - ry0) < 3.0f) return;

    // 화면 좌표 → 이미지 픽셀 좌표 변환
    const ViewportState& vp = state.views[panel_idx];
    float zoom    = vp.zoom;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    auto s2ix = [&](float sx) {
        return (sx - widget_pos.x - half_vw) / zoom - vp.pan_x + half_iw;
    };
    auto s2iy = [&](float sy) {
        return (sy - widget_pos.y - half_vh) / zoom - vp.pan_y + half_ih;
    };

    float ix0 = s2ix(rx0), iy0 = s2iy(ry0);
    float ix1 = s2ix(rx1), iy1 = s2iy(ry1);

    roi.x = std::max(0, (int)std::floor(ix0));
    roi.y = std::max(0, (int)std::floor(iy0));
    roi.w = std::min((int)std::ceil(ix1), img_w) - roi.x;
    roi.h = std::min((int)std::ceil(iy1), img_h) - roi.y;

    if (roi.w > 0 && roi.h > 0) {
        roi.has_roi   = true;
        roi.panel_idx = panel_idx;
        state.show_roi_stats = true;
    }
}

// ─── ROI overlay drawing ──────────────────────────────────────────────────────

void ImagePanel::render_roi_overlay(const AppState& state, int panel_idx,
                                     ImVec2 widget_pos, int view_w, int view_h,
                                     int img_w, int img_h)
{
    const auto& roi = state.roi;
    if (!roi.has_roi && !roi.dragging) return;

    const ViewportState& vp = state.views[panel_idx];
    float zoom    = vp.zoom;
    float half_vw = view_w * 0.5f;
    float half_vh = view_h * 0.5f;
    float half_iw = img_w  * 0.5f;
    float half_ih = img_h  * 0.5f;

    // 이미지 픽셀 → 화면 좌표
    auto ix2s = [&](float ix) {
        return widget_pos.x + (ix + vp.pan_x - half_iw) * zoom + half_vw;
    };
    auto iy2s = [&](float iy) {
        return widget_pos.y + (iy + vp.pan_y - half_ih) * zoom + half_vh;
    };

    if (roi.has_roi) {
        float sx0 = ix2s(roi.x);
        float sy0 = iy2s(roi.y);
        float sx1 = ix2s(roi.x + roi.w);
        float sy1 = iy2s(roi.y + roi.h);

        // Clip to widget bounds
        float wx0 = widget_pos.x, wy0 = widget_pos.y;
        float wx1 = wx0 + view_w,  wy1 = wy0 + view_h;
        float cx0 = std::max(sx0, wx0), cy0 = std::max(sy0, wy0);
        float cx1 = std::min(sx1, wx1), cy1 = std::min(sy1, wy1);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (cx1 > cx0 && cy1 > cy0) {
            dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx1, cy1),
                              IM_COL32(100, 200, 255, 25));
        }
        dl->AddRect(ImVec2(sx0, sy0), ImVec2(sx1, sy1),
                    IM_COL32(100, 200, 255, 220), 0.0f, 0, 1.5f);

        // ROI 크기 레이블 표시
        char label[64];
        std::snprintf(label, sizeof(label), "%d x %d", roi.w, roi.h);
        dl->AddText(ImVec2(sx0 + 3, sy0 + 3), IM_COL32(100, 200, 255, 220), label);
    }
}

// ─── Overlay/Blend render ─────────────────────────────────────────────────────

void ImagePanel::render_overlay(AppState& state, DiffRenderer& /*diff_renderer*/)
{
    int idx_a = state.swap_images ? 1 : 0;
    int idx_b = state.swap_images ? 0 : 1;
    const ImageEntry& imgA = state.images[idx_a];
    const ImageEntry& imgB = state.images[idx_b];
    ViewportState&    vp   = state.views[0];

    if (!imgA.loaded || !imgB.loaded) {
        ImGui::TextDisabled("(overlay: need two images)");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x);
    int ph = std::max(1, (int)avail.y);

    if (vp.fit) viewport_fit(vp, imgA.width, imgA.height, pw, ph);
    viewport_clamp_pan(vp, imgA.width, imgA.height, pw, ph);

    if (!fbo_.ensure(pw, ph)) return;

    fbo_.bind();
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Bind both textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(imgA.texture_id));
    if (vp.zoom >= 1.0f) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(imgB.texture_id));
    if (vp.zoom >= 1.0f) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    int blend_mode = (state.overlay.mode == OverlayState::Mode::Curtain) ? 1 : 0;

    blend_shader_.use();
    blend_shader_.set_int  ("u_texA",        0);
    blend_shader_.set_int  ("u_texB",        1);
    blend_shader_.set_vec2 ("u_image_size",  (float)imgA.width, (float)imgA.height);
    blend_shader_.set_vec2 ("u_view_size",   (float)pw, (float)ph);
    blend_shader_.set_float("u_zoom",        vp.zoom);
    blend_shader_.set_vec2 ("u_pan",         vp.pan_x, vp.pan_y);
    blend_shader_.set_int  ("u_channel",     (int)state.channel_mode);
    blend_shader_.set_int  ("u_blend_mode",  blend_mode);
    blend_shader_.set_float("u_alpha",       state.overlay.alpha);
    blend_shader_.set_float("u_curtain_x",   state.overlay.curtain_x);

    quad_.draw();
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    fbo_.unbind();

    ImTextureID tex = static_cast<ImTextureID>(fbo_.tex_id);
    ImGui::Image(tex, avail, ImVec2(0,0), ImVec2(1,1));

    ImVec2 widget_pos = ImGui::GetItemRectMin();

    // Curtain 모드: 마우스 드래그로 구분선 조정
    if (state.overlay.mode == OverlayState::Mode::Curtain) {
        float curtain_sx = widget_pos.x + state.overlay.curtain_x * pw;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(curtain_sx, widget_pos.y),
                    ImVec2(curtain_sx, widget_pos.y + ph),
                    IM_COL32(255, 255, 255, 200), 2.0f);

        // 구분선 레이블
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "A | B");
        dl->AddText(ImVec2(curtain_sx + 4, widget_pos.y + 4),
                    IM_COL32(255, 255, 255, 200), lbl);

        // 마우스로 커튼 드래그
        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float mx = ImGui::GetMousePos().x;
            state.overlay.curtain_x = std::clamp((mx - widget_pos.x) / pw, 0.0f, 1.0f);
        }
    } else {
        // Blend 모드: 마우스 팬
        handle_mouse_pan(state, 0);
    }

    if (state.roi.active) {
        handle_roi_drag(state, 0, widget_pos, pw, ph,
                        imgA.width, imgA.height);
    } else if (state.overlay.mode != OverlayState::Mode::Curtain) {
        handle_mouse_right_select(state, 0, widget_pos, pw, ph,
                                   imgA.width, imgA.height);
    }

    if (state.show_borders)
        draw_image_border(ImGui::GetWindowDrawList(), widget_pos,
                          (float)pw, (float)ph,
                          (float)imgA.width, (float)imgA.height,
                          vp.pan_x, vp.pan_y, vp.zoom,
                          IM_COL32(150, 255, 150, 200));

    if (state.roi.has_roi || state.roi.dragging) {
        render_roi_overlay(state, 0, widget_pos, pw, ph,
                           imgA.width, imgA.height);
    }

    render_pathfinder(state, 0, widget_pos, pw, ph);
}
